/* SPDX-License-Identifier: LGPL-2.1-or-later */

/* This is part of the GNU GMAN Library, a FREE implementation of the
 * RenderMan Interface Specification.
 *
 * Copyright (c) 2026 John Cairns <john@2ad.com>
 */

/*
  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Library General Public
  License as published by the Free Software Foundation; either
  version 2 of the License, or (at your option) any later version.


  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Library General Public License for more details.

  You should have received a copy of the GNU Library General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
*/

#include "gmanloadable.h"
#include "gmansurfaceshader.h"
#include "gmanshaderparams.h"

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
class GMANPlastic : public GMANSurfaceShader
{
public:
  RtVoid illuminance (RtInt i, GMANVector L, GMANColor Cl, GMANColor Ol);

  const GMANColor &computeCi(GMANSurfaceEnv &se);
  const GMANColor &computeOi(GMANSurfaceEnv &se);
};

RtVoid GMANPlastic::illuminance (RtInt /*i*/, GMANVector /*L*/,
				  GMANColor /*Cl*/, GMANColor /*Ol*/)
{
  // Unused: computeCi sums lights itself via env.ambient()/diffuse()/
  // specular(), the C++-shader equivalent of an SL illuminance() loop.
}

const GMANColor &GMANPlastic::computeCi(GMANSurfaceEnv &se)
{
  static GMANColor ci;

  RtFloat ka = gmanshaders::getFloatParam(pl, RI_KA, 1.0);
  RtFloat kd = gmanshaders::getFloatParam(pl, RI_KD, 0.5);
  RtFloat ks = gmanshaders::getFloatParam(pl, RI_KS, 0.5);
  RtFloat roughness = gmanshaders::getFloatParam(pl, RI_ROUGHNESS, 0.1);
  GMANColor specularcolor = gmanshaders::getColorParam(
      pl, RI_SPECULARCOLOR, GMANColor((RtFloat) 1.0, (RtFloat) 1.0, (RtFloat) 1.0));

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

  ci = GMANColor(se.Os.getRed() * lit.getRed(),
		 se.Os.getGreen() * lit.getGreen(),
		 se.Os.getBlue() * lit.getBlue());
  return ci;
}

const GMANColor &GMANPlastic::computeOi(GMANSurfaceEnv &se)
{
  static GMANColor oi;
  oi = se.Os;
  return oi;
}

static GMANLoadableObjectInfo loadableInfo = {
  "Plastic surface shader",
  "John Cairns <john@2ad.com>",
  "Copyright (c) 2026 John Cairns, Licenced under the GNU Lesser Public License, http://www.gnu.org",
  "A GMAN SurfaceShader for plastic surfaces: diffuse base plus a "
  "specularcolor-tinted specular highlight.",
};

static GMANPlastic shader;

extern "C" GMANLoadableObjectInfo *GMANGetLoadableInfo(void) {
  return &loadableInfo;
}

extern "C" GMANShader *GMANLoadShader(void) {
  return &shader;
}
