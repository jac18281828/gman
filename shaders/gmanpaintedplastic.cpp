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

#include <optional>
#include <string>

#include "gman/export.h"
#include "gmanbsdf.h"
#include "gmanloadable.h"
#include "gmanparameterlist.h"
#include "gmanshader.h"
#include "gmanshaderenvironment.h"
#include "gmanshaderparams.h"
#include "gmansurfaceshader.h"
#include "gmanvector.h"
#include "ri.h"

/**
 * surface paintedplastic(float Ka=1, Kd=.5, Ks=.5, roughness=.1;
 *                        color specularcolor=1; string texturename="";
 *                        string opacityname="", normalname="";
 *                        float normalgreendown=0)
 * {
 *   normal Nn = (normalname == "") ? N : mapnormal(N, texture(normalname));
 *   normal Nf = faceforward(normalize(Nn), I);
 *   Oi = (opacityname == "") ? Os : Os * float texture(opacityname);
 *   Ci = Oi * (color texture(texturename) * Cs *
 *              (Ka*ambient() + Kd*diffuse(Nf))
 *              + specularcolor*Ks*specular(Nf, -normalize(I), roughness));
 * }
 *
 * plastic (gmanplastic.cpp) with texturename tinting the diffuse base. An
 * empty texturename multiplies by white, degrading to plastic rather than
 * to black. A named opacityname cuts the surface out: the map's first
 * channel scales Os, and Ci scales with it. An empty opacityname, the
 * default, leaves Oi = Os.
 *
 * A named normalname bends the shading normal by a tangent-space normal
 * map: the texel's channels, each c in [0, 1], decode to 2c - 1 along the
 * tangent, the bitangent and N. The tangent is dPdu and the bitangent dPdv,
 * each made perpendicular to N and the earlier axis. Green points up the
 * image, along -dPdv; a nonzero normalgreendown reads a DirectX map, green
 * along +dPdv. A degenerate dPdu or dPdv leaves N unbent, as an empty
 * normalname, the default, does.
 */
namespace gmanshader {

class paintedplastic : public GMANSurfaceShader {
public:
  explicit paintedplastic(GMANParameterList const& parameters)
      : ka(getFloatParam(parameters, RI_KA, 1.0)), kd(getFloatParam(parameters, RI_KD, 0.5)),
        ks(getFloatParam(parameters, RI_KS, 0.5)), roughness(getFloatParam(parameters, RI_ROUGHNESS, 0.1)),
        specularcolor(getColorParam(parameters, RI_SPECULARCOLOR, GMANColor((RtFloat)1.0, (RtFloat)1.0, (RtFloat)1.0))),
        texturename(getStringParam(parameters, RI_TEXTURENAME, std::string())),
        opacityname(getStringParam(parameters, "opacityname", std::string())),
        normalname(getStringParam(parameters, "normalname", std::string())),
        normalgreendown(getFloatParam(parameters, "normalgreendown", 0.0) != 0.0) {}

  RtVoid illuminance(RtInt i, GMANVector L, GMANColor Cl, GMANColor Ol);

  GMANColor computeCi(GMANSurfaceEnv const& se) const override;
  GMANColor computeOi(GMANSurfaceEnv const& se) const override;
  gman::BSDF bsdf(GMANSurfaceEnv const& se) const override;

private:
  GMANVector shadingNormal(GMANSurfaceEnv const& se) const;

  RtFloat const ka;
  RtFloat const kd;
  RtFloat const ks;
  RtFloat const roughness;
  GMANColor const specularcolor;
  std::string const texturename;
  std::string const opacityname;
  std::string const normalname;
  bool const normalgreendown;
};

namespace {

constexpr RtFloat kFrameTolerance = 1.0e-4;

GMANVector withoutComponent(GMANVector const& v, GMANVector const& unitAxis) { return v - unitAxis * v.dot(unitAxis); }

std::optional<GMANVector> unitVector(GMANVector v, RtFloat originalLength) {
  RtFloat const length = v.magnitude();
  if (!(length > kFrameTolerance * originalLength)) {
    return std::nullopt;
  }
  return v / length;
}

} // namespace

GMANVector paintedplastic::shadingNormal(GMANSurfaceEnv const& se) const {
  if (normalname.empty()) {
    return se.N;
  }

  GMANVector n = se.N;
  n.normalize();
  GMANVector dPdu = se.dPdu;
  GMANVector dPdv = se.dPdv;
  std::optional<GMANVector> const tangent = unitVector(withoutComponent(dPdu, n), dPdu.magnitude());
  if (!tangent) {
    return se.N;
  }
  std::optional<GMANVector> const bitangent =
      unitVector(withoutComponent(withoutComponent(dPdv, n), *tangent), dPdv.magnitude());
  if (!bitangent) {
    return se.N;
  }

  GMANColor const texel = se.texture(normalname, se.s, se.t);
  RtFloat const alongTangent = (RtFloat)2.0 * texel.getRed() - (RtFloat)1.0;
  RtFloat const alongGreen = (RtFloat)2.0 * texel.getGreen() - (RtFloat)1.0;
  RtFloat const alongNormal = (RtFloat)2.0 * texel.getBlue() - (RtFloat)1.0;
  RtFloat const alongBitangent = normalgreendown ? alongGreen : -alongGreen;

  GMANVector bent = *tangent * alongTangent + *bitangent * alongBitangent + n * alongNormal;
  RtFloat const length = bent.magnitude();
  if (!(length > (RtFloat)0.0)) {
    return se.N;
  }
  return bent / length;
}

RtVoid paintedplastic::illuminance(RtInt /*i*/, GMANVector /*L*/, GMANColor /*Cl*/, GMANColor /*Ol*/) {
  // Unused: computeCi sums lights itself via env.ambient()/diffuse()/
  // specular(), the C++-shader equivalent of an SL illuminance() loop.
}

GMANColor paintedplastic::computeCi(GMANSurfaceEnv const& se) const {
  GMANColor tex =
      texturename.empty() ? GMANColor((RtFloat)1.0, (RtFloat)1.0, (RtFloat)1.0) : se.texture(texturename, se.s, se.t);

  GMANVector nf = se.faceforward(shadingNormal(se), se.I, se.Ng);
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

  GMANColor const oi = computeOi(se);
  return GMANColor(oi.getRed() * lit.getRed(), oi.getGreen() * lit.getGreen(), oi.getBlue() * lit.getBlue());
}

GMANColor paintedplastic::computeOi(GMANSurfaceEnv const& se) const {
  if (opacityname.empty()) {
    return se.Os;
  }
  RtFloat const coverage = se.texture(opacityname, se.s, se.t).getRed();
  return GMANColor(se.Os.getRed() * coverage, se.Os.getGreen() * coverage, se.Os.getBlue() * coverage);
}

// plastic's closure (gmanplastic.cpp) with the diffuse base tinted by the
// texture, white when texturename is empty; the headroom the GGX lobe
// takes is fitted to that textured diffuse, texel by texel.
gman::BSDF paintedplastic::bsdf(GMANSurfaceEnv const& se) const {
  GMANColor const tex =
      texturename.empty() ? GMANColor((RtFloat)1.0, (RtFloat)1.0, (RtFloat)1.0) : se.texture(texturename, se.s, se.t);

  gman::BSDF closure(shadingNormal(se));
  GMANColor const d = clampAlbedo(GMANColor(kd * se.Cs.getRed() * tex.getRed(), kd * se.Cs.getGreen() * tex.getGreen(),
                                            kd * se.Cs.getBlue() * tex.getBlue()));
  GMANColor const s =
      clampAlbedo(GMANColor(ks * specularcolor.getRed(), ks * specularcolor.getGreen(), ks * specularcolor.getBlue()));
  addPlasticLobes(closure, d, s, roughness);
  return closure;
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
