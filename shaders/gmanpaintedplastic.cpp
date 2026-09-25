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

#include <string>

#include "gmanloadable.h"
#include "gmanshaderparams.h"
#include "gmansurfaceshader.h"

/*
 * surface paintedplastic(float Ka=1, Kd=.5, Ks=.5, roughness=.1;
 *                        color specularcolor=1; string texturename="")
 * {
 *   normal Nf = faceforward(normalize(N), I);
 *   Oi = Os;
 *   Ci = Os * (color texture(texturename) * Cs *
 *              (Ka*ambient() + Kd*diffuse(Nf))
 *              + specularcolor*Ks*specular(Nf, -normalize(I), roughness));
 * }
 *
 * plastic (gmanplastic.cpp) with texturename tinting the diffuse base. An
 * empty texturename multiplies by white, degrading to plastic rather than
 * to black.
 */
namespace gmanshader {

class paintedplastic : public GMANSurfaceShader {
public:
  explicit paintedplastic(GMANParameterList const& parameters)
      : ka(getFloatParam(parameters, RI_KA, 1.0)), kd(getFloatParam(parameters, RI_KD, 0.5)),
        ks(getFloatParam(parameters, RI_KS, 0.5)), roughness(getFloatParam(parameters, RI_ROUGHNESS, 0.1)),
        specularcolor(getColorParam(parameters, RI_SPECULARCOLOR, GMANColor((RtFloat)1.0, (RtFloat)1.0, (RtFloat)1.0))),
        texturename(getStringParam(parameters, RI_TEXTURENAME, std::string())) {}

  RtVoid illuminance(RtInt i, GMANVector L, GMANColor Cl, GMANColor Ol);

  GMANColor computeCi(GMANSurfaceEnv const& se) const override;
  GMANColor computeOi(GMANSurfaceEnv const& se) const override;
  GMANColor albedo(GMANSurfaceEnv const& se) const override;

private:
  RtFloat const ka;
  RtFloat const kd;
  RtFloat const ks;
  RtFloat const roughness;
  GMANColor const specularcolor;
  std::string const texturename;
};

RtVoid paintedplastic::illuminance(RtInt /*i*/, GMANVector /*L*/, GMANColor /*Cl*/, GMANColor /*Ol*/) {
  // Unused: computeCi sums lights itself via env.ambient()/diffuse()/
  // specular(), the C++-shader equivalent of an SL illuminance() loop.
}

GMANColor paintedplastic::computeCi(GMANSurfaceEnv const& se) const {
  GMANColor tex =
      texturename.empty() ? GMANColor((RtFloat)1.0, (RtFloat)1.0, (RtFloat)1.0) : se.texture(texturename, se.s, se.t);

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

  GMANColor paint(se.Cs.getRed() * tex.getRed(), se.Cs.getGreen() * tex.getGreen(), se.Cs.getBlue() * tex.getBlue());

  GMANColor lit(paint.getRed() * diffuseTerm.getRed() + tintedSpecular.getRed(),
                paint.getGreen() * diffuseTerm.getGreen() + tintedSpecular.getGreen(),
                paint.getBlue() * diffuseTerm.getBlue() + tintedSpecular.getBlue());

  return GMANColor(se.Os.getRed() * lit.getRed(), se.Os.getGreen() * lit.getGreen(), se.Os.getBlue() * lit.getBlue());
}

GMANColor paintedplastic::computeOi(GMANSurfaceEnv const& se) const { return se.Os; }

// Kd*Cs*texture, white when texturename is empty: the texture tints the
// diffuse base exactly as it does in computeCi.
GMANColor paintedplastic::albedo(GMANSurfaceEnv const& se) const {
  GMANColor const tex =
      texturename.empty() ? GMANColor((RtFloat)1.0, (RtFloat)1.0, (RtFloat)1.0) : se.texture(texturename, se.s, se.t);
  return clampAlbedo(GMANColor(kd * se.Cs.getRed() * tex.getRed(), kd * se.Cs.getGreen() * tex.getGreen(),
                               kd * se.Cs.getBlue() * tex.getBlue()));
}

} // namespace gmanshader

static GMANLoadableObjectInfo loadableInfo = {
    "Painted plastic surface shader",
    "John Cairns <john@2ad.com>",
    "A GMAN SurfaceShader for plastic surfaces with a texture-mapped diffuse "
    "colour: the RISpec's own paintedplastic.",
};

extern "C" GMAN_EXPORT GMANLoadableObjectInfo* GMANGetLoadableInfo(void) { return &loadableInfo; }

extern "C" GMAN_EXPORT GMANShader* GMANLoadShader(GMANParameterList const& parameters) {
  return new gmanshader::paintedplastic(parameters);
}

extern "C" GMAN_EXPORT void GMANDestroyShader(GMANShader* shader) { delete shader; }
