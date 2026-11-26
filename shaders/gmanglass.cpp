/* SPDX-License-Identifier: LGPL-2.1-or-later */

/* This is part of GMAN, a RenderMan-compatible renderer.
 *
 * Copyright (c) 2026 John Cairns <john@2ad.com>
 */

/*
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
 */

#include <cmath>

#include "gman/export.h"
#include "gmanbsdf.h"
#include "gmancolor.h"
#include "gmanloadable.h"
#include "gmanmath.h"
#include "gmanparameterlist.h"
#include "gmanshader.h"
#include "gmanshaderenvironment.h"
#include "gmanshaderparams.h"
#include "gmansurfaceshader.h"
#include "gmanvector.h"
#include "ri.h"

/**
 * surface glass(float eta=1.5, Kr=1, Kt=1)
 * {
 *   normal Nf = faceforward(normalize(N), I);
 *   float e = (Ng . I < 0) ? 1/eta : eta;
 *   vector Rr = reflect(I, Nf);
 *   vector Rt = refract(I, Nf, e);
 *   float kr, kt;
 *   fresnel(I, Nf, e, kr, kt);   // kr=1, kt=0 at TIR
 *   kr *= Kr;
 *   kt *= Kt;
 *   Oi = Os;
 *   Ci = Os * (kr*trace(P, Rr) + kt*trace(P, Rt));
 * }
 *
 * The parameters and their defaults are those of BMRT's glass shader: eta
 * is the index of refraction, Kr scales the reflected ray and Kt the
 * refracted one. Kr is a standard token; eta and Kt are not, so a scene
 * declares them inline:
 *
 *   Surface "glass" "float eta" [1.33] "float Kt" [0.5] "Kr" [0.3]
 *
 * Kr and Kt clamp to [0, 1], a NaN to 0. An eta that is not finite or not
 * above 0 becomes 1.5; any other eta clamps to [0.01, 100]. The ray tracer
 * and the path tracer read the same three values: the path tracer samples
 * one dielectric lobe of reflection weight Kr, transmission weight Kt and
 * index eta.
 *
 * A shadow ray crossing glass passes Kt * (1 - F) of the light at each
 * face, F the Fresnel reflectance at the ray's angle to the face with
 * index eta behind it. Kr carries no light through the glass.
 *
 * No branch for total internal reflection: fresnel() already returns
 * kr=1, kt=0 there, so kt*trace(Rt) contributes exactly zero regardless
 * of what refract()'s own zero-vector return finds, and Kr alone scales
 * the reflected ray.
 */
namespace gmanshader {

constexpr RtFloat kDefaultEta = 1.5f;
constexpr RtFloat kMinEta = 0.01f;
constexpr RtFloat kMaxEta = 100.0f;

class glass : public GMANSurfaceShader {
public:
  explicit glass(GMANParameterList const& parameters);

  GMANColor computeCi(GMANSurfaceEnv const& se) const override;
  GMANColor computeOi(GMANSurfaceEnv const& se) const override;
  gman::BSDF bsdf(GMANSurfaceEnv const& se) const override;

private:
  RtFloat const indexOfRefraction;
  RtFloat const reflectScale;
  RtFloat const transmitScale;
};

namespace {

RtFloat sanitizedEta(RtFloat eta) {
  if (!(std::isfinite(eta) && eta > 0.0f)) {
    return kDefaultEta;
  }
  return GMANClamp(eta, kMinEta, kMaxEta);
}

RtFloat sanitizedScale(RtFloat scale) { return GMANClamp(scale, 0.0f, 1.0f); }

} // namespace

glass::glass(GMANParameterList const& parameters)
    : indexOfRefraction(sanitizedEta(getFloatParam(parameters, "eta", kDefaultEta))),
      reflectScale(sanitizedScale(getFloatParam(parameters, RI_KR, 1.0f))),
      transmitScale(sanitizedScale(getFloatParam(parameters, "Kt", 1.0f))) {}

GMANColor glass::computeCi(GMANSurfaceEnv const& se) const {
  GMANVector n(se.N.getX(), se.N.getY(), se.N.getZ());
  n.normalize();
  GMANVector const nf = se.faceforward(n, se.I, se.Ng);

  GMANVector const ng(se.Ng.getX(), se.Ng.getY(), se.Ng.getZ());
  RtFloat const eta = (ng.dot(se.I) < (RtFloat)0.0) ? ((RtFloat)1.0 / indexOfRefraction) : indexOfRefraction;

  GMANVector const rr = se.reflect(se.I, nf);
  GMANVector const rt = se.refract(se.I, nf, eta);

  RtFloat kr = 0.0, kt = 0.0;
  se.fresnel(se.I, nf, eta, kr, kt);
  kr *= reflectScale;
  kt *= transmitScale;

  GMANColor const tracedR = se.trace(rr);
  GMANColor const tracedT = se.trace(rt);

  GMANColor const combined(kr * tracedR.getRed() + kt * tracedT.getRed(),
                           kr * tracedR.getGreen() + kt * tracedT.getGreen(),
                           kr * tracedR.getBlue() + kt * tracedT.getBlue());
  return GMANColor(se.Os.getRed() * combined.getRed(), se.Os.getGreen() * combined.getGreen(),
                   se.Os.getBlue() * combined.getBlue());
}

GMANColor glass::computeOi(GMANSurfaceEnv const& se) const { return se.Os; }

// One dielectric lobe of reflection weight Kr, transmission weight Kt and
// index eta: glass reads no Cs, so the lobe carries no tint of its own. The
// normal faces the outside, Ng's side, since computeCi enters when Ng . I <
// 0; negating N where N and Ng disagree keeps the ray tracer and the path
// tracer agreeing about which side is which. An env with Ng zero leaves N as
// it is. albedo (the base default's bsdf(se).rhoD()) is exactly black, since
// rhoD counts only Lambert lobes.
gman::BSDF glass::bsdf(GMANSurfaceEnv const& se) const {
  GMANVector const n(se.N.getX(), se.N.getY(), se.N.getZ());
  GMANVector const ng(se.Ng.getX(), se.Ng.getY(), se.Ng.getZ());
  gman::BSDF closure(n.dot(ng) < 0.0f ? -n : n);
  closure.addDielectric(GMANColor(reflectScale, reflectScale, reflectScale),
                        GMANColor(transmitScale, transmitScale, transmitScale), indexOfRefraction);
  return closure;
}

} // namespace gmanshader

static GMANLoadableObjectInfo loadableInfo = {
    "Glass surface shader",
    "John Cairns <john@2ad.com>",
    "A GMAN SurfaceShader for real refraction and reflection: fresnel() "
    "weights trace() along the reflected and refracted rays; eta sets the "
    "index of refraction and Kr and Kt scale the reflected and refracted "
    "rays.",
};

extern "C" GMAN_EXPORT GMANLoadableObjectInfo* GMANGetLoadableInfo(void) { return &loadableInfo; }

extern "C" GMAN_EXPORT GMANShader* GMANLoadShader(GMANParameterList const& parameters) {
  return new gmanshader::glass(parameters);
}

extern "C" GMAN_EXPORT void GMANDestroyShader(GMANShader* shader) { delete shader; }
