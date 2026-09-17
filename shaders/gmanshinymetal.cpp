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
class GMANShinyMetal : public GMANSurfaceShader {
public:
  RtVoid illuminance(RtInt i, GMANVector L, GMANColor Cl, GMANColor Ol);

  const GMANColor& computeCi(GMANSurfaceEnv& se);
  const GMANColor& computeOi(GMANSurfaceEnv& se);
};

RtVoid GMANShinyMetal::illuminance(RtInt /*i*/, GMANVector /*L*/, GMANColor /*Cl*/, GMANColor /*Ol*/) {
  // Unused: computeCi sums lights itself via env.ambient()/specular(),
  // the C++-shader equivalent of an SL illuminance() loop.
}

const GMANColor& GMANShinyMetal::computeCi(GMANSurfaceEnv& se) {
  static GMANColor ci;

  RtFloat ka = gmanshaders::getFloatParam(pl, RI_KA, 1.0);
  RtFloat ks = gmanshaders::getFloatParam(pl, RI_KS, 1.0);
  RtFloat kr = gmanshaders::getFloatParam(pl, RI_KR, 1.0);
  RtFloat roughness = gmanshaders::getFloatParam(pl, RI_ROUGHNESS, 0.1);
  std::string texturename = gmanshaders::getStringParam(pl, RI_TEXTURENAME, std::string());

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

  ci = GMANColor(se.Os.getRed() * se.Cs.getRed() * lit.getRed(), se.Os.getGreen() * se.Cs.getGreen() * lit.getGreen(),
                 se.Os.getBlue() * se.Cs.getBlue() * lit.getBlue());
  return ci;
}

const GMANColor& GMANShinyMetal::computeOi(GMANSurfaceEnv& se) {
  static GMANColor oi;
  oi = se.Os;
  return oi;
}

static GMANLoadableObjectInfo loadableInfo = {
    "Shiny metal surface shader",
    "John Cairns <john@2ad.com>",
    "A GMAN SurfaceShader for shiny metal surfaces: Cs-tinted specular "
    "response plus a world-space environment reflection.",
};

static GMANShinyMetal shader;

extern "C" GMANLoadableObjectInfo* GMANGetLoadableInfo(void) { return &loadableInfo; }

extern "C" GMANShader* GMANLoadShader(void) { return &shader; }
