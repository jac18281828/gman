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
 * surface shinymetal(float Ka=1, Ks=1, Kr=1, roughness=.1;
 *                     string texturename="")
 * {
 *   normal Nf = faceforward(normalize(N), I);
 *   vector V = -normalize(I);
 *   vector D = vtransform("current", "world", reflect(I, Nf));
 *   Ci = Os * Cs * (Ka*ambient() + Ks*specular(Nf, V, roughness)
 *                   + Kr*environment(texturename, D));
 *   Oi = Os;
 * }
 *
 * RISpec A.2.4. environment() is indexed in world space (D), so a
 * reflection stays put when the camera moves -- see toWorld's own
 * comment. An empty texturename skips the lookup and adds black, so
 * shinymetal degrades to metal (gmanmetal.cpp), as the RISpec says an
 * implementation without environment mapping behaves.
 */
namespace gmanshader {

class shinymetal : public GMANSurfaceShader {
public:
  explicit shinymetal(GMANParameterList const& parameters)
      : ka(getFloatParam(parameters, RI_KA, 1.0)), ks(getFloatParam(parameters, RI_KS, 1.0)),
        kr(getFloatParam(parameters, RI_KR, 1.0)), roughness(getFloatParam(parameters, RI_ROUGHNESS, 0.1)),
        texturename(getStringParam(parameters, RI_TEXTURENAME, std::string())) {}

  RtVoid illuminance(RtInt i, GMANVector L, GMANColor Cl, GMANColor Ol);

  GMANColor computeCi(GMANSurfaceEnv const& se) const;
  GMANColor computeOi(GMANSurfaceEnv const& se) const;

private:
  RtFloat const ka;
  RtFloat const ks;
  RtFloat const kr;
  RtFloat const roughness;
  std::string const texturename;
};

RtVoid shinymetal::illuminance(RtInt /*i*/, GMANVector /*L*/, GMANColor /*Cl*/, GMANColor /*Ol*/) {
  // Unused: computeCi sums lights itself via env.ambient()/specular(),
  // the C++-shader equivalent of an SL illuminance() loop.
}

GMANColor shinymetal::computeCi(GMANSurfaceEnv const& se) const {
  GMANVector nf = se.faceforward(se.N, se.I, se.Ng);
  GMANVector vf(-se.I.getX(), -se.I.getY(), -se.I.getZ());
  vf.normalize();

  GMANColor lit = se.ambient();
  lit.scale(ka);

  GMANColor specularTerm = se.specular(nf, vf, roughness);
  specularTerm.scale(ks);
  lit += specularTerm;

  if (!texturename.empty()) {
    GMANVector reflected = se.reflect(se.I, nf);
    GMANColor env = se.environment(texturename, se.toWorld(reflected));
    env.scale(kr);
    lit += env;
  }

  return GMANColor(se.Os.getRed() * se.Cs.getRed() * lit.getRed(), se.Os.getGreen() * se.Cs.getGreen() * lit.getGreen(),
                   se.Os.getBlue() * se.Cs.getBlue() * lit.getBlue());
}

GMANColor shinymetal::computeOi(GMANSurfaceEnv const& se) const { return se.Os; }

} // namespace gmanshader

static GMANLoadableObjectInfo loadableInfo = {
    "Shiny metal surface shader",
    "John Cairns <john@2ad.com>",
    "A GMAN SurfaceShader for shiny metal surfaces: Cs-tinted specular "
    "response plus a world-space environment reflection.",
};

extern "C" GMAN_EXPORT GMANLoadableObjectInfo* GMANGetLoadableInfo(void) { return &loadableInfo; }

extern "C" GMAN_EXPORT GMANShader* GMANLoadShader(GMANParameterList const& parameters) {
  return new gmanshader::shinymetal(parameters);
}

extern "C" GMAN_EXPORT void GMANDestroyShader(GMANShader* shader) { delete shader; }
