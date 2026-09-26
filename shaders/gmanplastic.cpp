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
 * surface plastic(float Ka=1, Kd=.5, Ks=.5, roughness=.1;
 *                 color specularcolor=1)
 * {
 *   normal Nf = faceforward(normalize(N), I);
 *   vector Vf = -normalize(I);
 *   Oi = Os;
 *   Ci = Os * (Cs*(Ka*ambient() + Kd*diffuse(Nf))
 *              + specularcolor*Ks*specular(Nf, Vf, roughness));
 * }
 *
 * The standard RenderMan plastic shader: a diffuse base tinted by Cs, plus
 * a specular highlight tinted by specularcolor (not Cs -- this is what
 * makes plastic look like plastic instead of metal).
 */
namespace gmanshader {

class plastic : public GMANSurfaceShader {
public:
  explicit plastic(GMANParameterList const& parameters)
      : ka(getFloatParam(parameters, RI_KA, 1.0)), kd(getFloatParam(parameters, RI_KD, 0.5)),
        ks(getFloatParam(parameters, RI_KS, 0.5)), roughness(getFloatParam(parameters, RI_ROUGHNESS, 0.1)),
        specularcolor(
            getColorParam(parameters, RI_SPECULARCOLOR, GMANColor((RtFloat)1.0, (RtFloat)1.0, (RtFloat)1.0))) {}

  RtVoid illuminance(RtInt i, GMANVector L, GMANColor Cl, GMANColor Ol);

  GMANColor computeCi(GMANSurfaceEnv const& se) const override;
  GMANColor computeOi(GMANSurfaceEnv const& se) const override;
  gman::BSDF bsdf(GMANSurfaceEnv const& se) const override;

private:
  RtFloat const ka;
  RtFloat const kd;
  RtFloat const ks;
  RtFloat const roughness;
  GMANColor const specularcolor;
};

RtVoid plastic::illuminance(RtInt /*i*/, GMANVector /*L*/, GMANColor /*Cl*/, GMANColor /*Ol*/) {
  // Unused: computeCi sums lights itself via env.ambient()/diffuse()/
  // specular(), the C++-shader equivalent of an SL illuminance() loop.
}

GMANColor plastic::computeCi(GMANSurfaceEnv const& se) const {
  GMANVector nf = se.faceforward(se.N, se.I, se.Ng);
  GMANVector vf(-se.I.getX(), -se.I.getY(), -se.I.getZ());
  vf.normalize();

  GMANColor diffuseTerm = se.ambient();
  diffuseTerm.scale(ka);
  GMANColor d = se.diffuse(nf);
  d.scale(kd);
  diffuseTerm += d;

  GMANColor specularTerm = se.specular(nf, vf, roughness);
  specularTerm.scale(ks);
  GMANColor tintedSpecular(specularcolor.getRed() * specularTerm.getRed(),
                           specularcolor.getGreen() * specularTerm.getGreen(),
                           specularcolor.getBlue() * specularTerm.getBlue());

  GMANColor lit(se.Cs.getRed() * diffuseTerm.getRed() + tintedSpecular.getRed(),
                se.Cs.getGreen() * diffuseTerm.getGreen() + tintedSpecular.getGreen(),
                se.Cs.getBlue() * diffuseTerm.getBlue() + tintedSpecular.getBlue());

  return GMANColor(se.Os.getRed() * lit.getRed(), se.Os.getGreen() * lit.getGreen(), se.Os.getBlue() * lit.getBlue());
}

GMANColor plastic::computeOi(GMANSurfaceEnv const& se) const { return se.Os; }

// A Lambert lobe of Kd*Cs plus a GGX lobe of the specular headroom that
// leaves: the two never exceed 1 combined, so the highlight vanishes
// rather than break energy conservation once Kd*Cs alone reaches 1 in a
// channel. albedo (the base default's bsdf(se).rhoD()) therefore still
// answers Kd*Cs exactly, unaffected by the specular term.
gman::BSDF plastic::bsdf(GMANSurfaceEnv const& se) const {
  gman::BSDF closure(se.N);
  GMANColor const d = clampAlbedo(GMANColor(kd * se.Cs.getRed(), kd * se.Cs.getGreen(), kd * se.Cs.getBlue()));
  GMANColor const s =
      clampAlbedo(GMANColor(ks * specularcolor.getRed(), ks * specularcolor.getGreen(), ks * specularcolor.getBlue()));
  RtFloat const k = ggxHeadroom(d, s);
  closure.addLambert(d);
  closure.addGGX(GMANColor(k * s.getRed(), k * s.getGreen(), k * s.getBlue()), alphaFromRoughness(roughness));
  return closure;
}

} // namespace gmanshader

static GMANLoadableObjectInfo loadableInfo = {
    "Plastic surface shader",
    "John Cairns <john@2ad.com>",
    "A GMAN SurfaceShader for plastic surfaces: diffuse base plus a "
    "specularcolor-tinted specular highlight.",
};

extern "C" GMAN_EXPORT GMANLoadableObjectInfo* GMANGetLoadableInfo(void) { return &loadableInfo; }

extern "C" GMAN_EXPORT GMANShader* GMANLoadShader(GMANParameterList const& parameters) {
  return new gmanshader::plastic(parameters);
}

extern "C" GMAN_EXPORT void GMANDestroyShader(GMANShader* shader) { delete shader; }
