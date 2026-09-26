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

#include "gmanbsdf.h"
#include "gmanloadable.h"
#include "gmanshaderparams.h"
#include "gmansurfaceshader.h"

/*
 * surface metal(float Ka=1, Ks=1, roughness=.1; color specularcolor=1)
 * {
 *   normal Nf = faceforward(normalize(N), I);
 *   vector Vf = -normalize(I);
 *   Oi = Os;
 *   Ci = Os * Cs * (Ka*ambient() + specularcolor*Ks*specular(Nf, Vf, roughness));
 * }
 *
 * The standard RenderMan metal shader: no diffuse term -- a metal's color
 * comes entirely from its (Cs-tinted) specular response, unlike plastic's
 * Cs-tinted diffuse base plus separately-tinted highlight.
 */
namespace gmanshader {

class metal : public GMANSurfaceShader {
public:
  explicit metal(GMANParameterList const& parameters)
      : ka(getFloatParam(parameters, RI_KA, 1.0)), ks(getFloatParam(parameters, RI_KS, 1.0)),
        roughness(getFloatParam(parameters, RI_ROUGHNESS, 0.1)),
        specularcolor(
            getColorParam(parameters, RI_SPECULARCOLOR, GMANColor((RtFloat)1.0, (RtFloat)1.0, (RtFloat)1.0))) {}

  RtVoid illuminance(RtInt i, GMANVector L, GMANColor Cl, GMANColor Ol);

  GMANColor computeCi(GMANSurfaceEnv const& se) const override;
  GMANColor computeOi(GMANSurfaceEnv const& se) const override;
  gman::BSDF bsdf(GMANSurfaceEnv const& se) const override;

private:
  RtFloat const ka;
  RtFloat const ks;
  RtFloat const roughness;
  GMANColor const specularcolor;
};

RtVoid metal::illuminance(RtInt /*i*/, GMANVector /*L*/, GMANColor /*Cl*/, GMANColor /*Ol*/) {
  // Unused: computeCi sums lights itself via env.ambient()/specular(),
  // the C++-shader equivalent of an SL illuminance() loop.
}

GMANColor metal::computeCi(GMANSurfaceEnv const& se) const {
  GMANVector nf = se.faceforward(se.N, se.I, se.Ng);
  GMANVector vf(-se.I.getX(), -se.I.getY(), -se.I.getZ());
  vf.normalize();

  GMANColor lit = se.ambient();
  lit.scale(ka);

  GMANColor specularTerm = se.specular(nf, vf, roughness);
  specularTerm.scale(ks);
  lit += GMANColor(specularcolor.getRed() * specularTerm.getRed(), specularcolor.getGreen() * specularTerm.getGreen(),
                   specularcolor.getBlue() * specularTerm.getBlue());

  return GMANColor(se.Os.getRed() * se.Cs.getRed() * lit.getRed(), se.Os.getGreen() * se.Cs.getGreen() * lit.getGreen(),
                   se.Os.getBlue() * se.Cs.getBlue() * lit.getBlue());
}

GMANColor metal::computeOi(GMANSurfaceEnv const& se) const { return se.Os; }

// One GGX lobe of Ks*specularcolor*Cs, clamped: no diffuse term, so
// albedo (the base default's bsdf(se).rhoD()) is exactly black.
gman::BSDF metal::bsdf(GMANSurfaceEnv const& se) const {
  gman::BSDF closure(se.N);
  GMANColor const weight = clampAlbedo(GMANColor(ks * specularcolor.getRed() * se.Cs.getRed(),
                                                 ks * specularcolor.getGreen() * se.Cs.getGreen(),
                                                 ks * specularcolor.getBlue() * se.Cs.getBlue()));
  closure.addGGX(weight, alphaFromRoughness(roughness));
  return closure;
}

} // namespace gmanshader

static GMANLoadableObjectInfo loadableInfo = {
    "Metal surface shader",
    "John Cairns <john@2ad.com>",
    "A GMAN SurfaceShader for metal surfaces: Cs-tinted specular response, "
    "no diffuse term.",
};

extern "C" GMAN_EXPORT GMANLoadableObjectInfo* GMANGetLoadableInfo(void) { return &loadableInfo; }

extern "C" GMAN_EXPORT GMANShader* GMANLoadShader(GMANParameterList const& parameters) {
  return new gmanshader::metal(parameters);
}

extern "C" GMAN_EXPORT void GMANDestroyShader(GMANShader* shader) { delete shader; }
