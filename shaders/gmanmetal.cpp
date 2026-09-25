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
  RtVoid illuminance(RtInt i, GMANVector L, GMANColor Cl, GMANColor Ol);

  GMANColor computeCi(GMANSurfaceEnv const& se) const;
  GMANColor computeOi(GMANSurfaceEnv const& se) const;
};

RtVoid metal::illuminance(RtInt /*i*/, GMANVector /*L*/, GMANColor /*Cl*/, GMANColor /*Ol*/) {
  // Unused: computeCi sums lights itself via env.ambient()/specular(),
  // the C++-shader equivalent of an SL illuminance() loop.
}

GMANColor metal::computeCi(GMANSurfaceEnv const& se) const {
  RtFloat ka = getFloatParam(pl, RI_KA, 1.0);
  RtFloat ks = getFloatParam(pl, RI_KS, 1.0);
  RtFloat roughness = getFloatParam(pl, RI_ROUGHNESS, 0.1);
  GMANColor specularcolor = getColorParam(pl, RI_SPECULARCOLOR, GMANColor((RtFloat)1.0, (RtFloat)1.0, (RtFloat)1.0));

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

} // namespace gmanshader

static GMANLoadableObjectInfo loadableInfo = {
    "Metal surface shader",
    "John Cairns <john@2ad.com>",
    "A GMAN SurfaceShader for metal surfaces: Cs-tinted specular response, "
    "no diffuse term.",
};

static gmanshader::metal shader;

extern "C" GMAN_EXPORT GMANLoadableObjectInfo* GMANGetLoadableInfo(void) { return &loadableInfo; }

extern "C" GMAN_EXPORT GMANShader* GMANLoadShader(void) { return &shader; }
