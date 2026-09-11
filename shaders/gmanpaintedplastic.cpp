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
#include "gmansurfaceshader.h"
#include "gmanshaderparams.h"

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
class GMANPaintedPlastic : public GMANSurfaceShader
{
public:
  RtVoid illuminance (RtInt i, GMANVector L, GMANColor Cl, GMANColor Ol);

  const GMANColor &computeCi(GMANSurfaceEnv &se);
  const GMANColor &computeOi(GMANSurfaceEnv &se);
};

RtVoid GMANPaintedPlastic::illuminance (RtInt /*i*/, GMANVector /*L*/,
					  GMANColor /*Cl*/, GMANColor /*Ol*/)
{
  // Unused: computeCi sums lights itself via env.ambient()/diffuse()/
  // specular(), the C++-shader equivalent of an SL illuminance() loop.
}

const GMANColor &GMANPaintedPlastic::computeCi(GMANSurfaceEnv &se)
{
  static GMANColor ci;

  RtFloat ka = gmanshaders::getFloatParam(pl, RI_KA, 1.0);
  RtFloat kd = gmanshaders::getFloatParam(pl, RI_KD, 0.5);
  RtFloat ks = gmanshaders::getFloatParam(pl, RI_KS, 0.5);
  RtFloat roughness = gmanshaders::getFloatParam(pl, RI_ROUGHNESS, 0.1);
  GMANColor specularcolor = gmanshaders::getColorParam(
      pl, RI_SPECULARCOLOR, GMANColor((RtFloat) 1.0, (RtFloat) 1.0, (RtFloat) 1.0));
  std::string texturename =
      gmanshaders::getStringParam(pl, RI_TEXTURENAME, std::string());

  GMANColor tex = texturename.empty()
      ? GMANColor((RtFloat) 1.0, (RtFloat) 1.0, (RtFloat) 1.0)
      : se.texture(texturename, se.s, se.t);

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

  GMANColor paint(se.Cs.getRed() * tex.getRed(),
		   se.Cs.getGreen() * tex.getGreen(),
		   se.Cs.getBlue() * tex.getBlue());

  GMANColor lit(paint.getRed() * diffuseTerm.getRed() + tintedSpecular.getRed(),
		paint.getGreen() * diffuseTerm.getGreen() + tintedSpecular.getGreen(),
		paint.getBlue() * diffuseTerm.getBlue() + tintedSpecular.getBlue());

  ci = GMANColor(se.Os.getRed() * lit.getRed(),
		 se.Os.getGreen() * lit.getGreen(),
		 se.Os.getBlue() * lit.getBlue());
  return ci;
}

const GMANColor &GMANPaintedPlastic::computeOi(GMANSurfaceEnv &se)
{
  static GMANColor oi;
  oi = se.Os;
  return oi;
}

static GMANLoadableObjectInfo loadableInfo = {
  "Painted plastic surface shader",
  "John Cairns <john@2ad.com>",
  "Copyright (c) 2026 John Cairns, Licensed under the GNU Lesser General Public License v2.1 or later, https://www.gnu.org/licenses/",
  "A GMAN SurfaceShader for plastic surfaces with a texture-mapped diffuse "
  "colour: the RISpec's own paintedplastic.",
};

static GMANPaintedPlastic shader;

extern "C" GMANLoadableObjectInfo *GMANGetLoadableInfo(void) {
  return &loadableInfo;
}

extern "C" GMANShader *GMANLoadShader(void) {
  return &shader;
}
