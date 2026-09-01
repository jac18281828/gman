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
class GMANMetal : public GMANSurfaceShader
{
public:
  RtVoid illuminance (RtInt i, GMANVector L, GMANColor Cl, GMANColor Ol);

  const GMANColor &computeCi(GMANSurfaceEnv &se);
  const GMANColor &computeOi(GMANSurfaceEnv &se);
};

RtVoid GMANMetal::illuminance (RtInt /*i*/, GMANVector /*L*/,
				GMANColor /*Cl*/, GMANColor /*Ol*/)
{
  // Unused: computeCi sums lights itself via env.ambient()/specular(),
  // the C++-shader equivalent of an SL illuminance() loop.
}

const GMANColor &GMANMetal::computeCi(GMANSurfaceEnv &se)
{
  static GMANColor ci;

  RtFloat ka = gmanshaders::getFloatParam(pl, RI_KA, 1.0);
  RtFloat ks = gmanshaders::getFloatParam(pl, RI_KS, 1.0);
  RtFloat roughness = gmanshaders::getFloatParam(pl, RI_ROUGHNESS, 0.1);
  GMANColor specularcolor = gmanshaders::getColorParam(
      pl, RI_SPECULARCOLOR, GMANColor((RtFloat) 1.0, (RtFloat) 1.0, (RtFloat) 1.0));

  GMANVector nf = se.faceforward(se.N, se.I, se.Ng);
  GMANVector vf(-se.I.getX(), -se.I.getY(), -se.I.getZ());
  vf.normalize();

  GMANColor lit = se.ambient();
  lit.scale(ka);

  GMANColor specularTerm = se.specular(nf, vf, roughness);
  specularTerm.scale(ks);
  lit += GMANColor(specularcolor.getRed() * specularTerm.getRed(),
		    specularcolor.getGreen() * specularTerm.getGreen(),
		    specularcolor.getBlue() * specularTerm.getBlue());

  ci = GMANColor(se.Os.getRed() * se.Cs.getRed() * lit.getRed(),
		 se.Os.getGreen() * se.Cs.getGreen() * lit.getGreen(),
		 se.Os.getBlue() * se.Cs.getBlue() * lit.getBlue());
  return ci;
}

const GMANColor &GMANMetal::computeOi(GMANSurfaceEnv &se)
{
  static GMANColor oi;
  oi = se.Os;
  return oi;
}

static GMANLoadableObjectInfo loadableInfo = {
  "Metal surface shader",
  "John Cairns <john@2ad.com>",
  "Copyright (c) 2026 John Cairns, Licenced under the GNU Lesser Public License, http://www.gnu.org",
  "A GMAN SurfaceShader for metal surfaces: Cs-tinted specular response, "
  "no diffuse term.",
};

static GMANMetal shader;

extern "C" GMANLoadableObjectInfo *GMANGetLoadableInfo(void) {
  return &loadableInfo;
}

extern "C" GMANShader *GMANLoadShader(void) {
  return &shader;
}
