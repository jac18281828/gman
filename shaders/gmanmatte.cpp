/* SPDX-License-Identifier: LGPL-2.1-or-later */

/* This is part of the GNU GMAN Library, a FREE implementation of the
 * RenderMan Interface Specification.
 *
 * Copyright (c) 2001 Ken Geis
 *
 * Author: Ken Geis <kgeis@alum.calberkeley.org>
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

  To contact the author of GNU GMAN, write to John Cairns, 607 E STUART ST, 
  FT COLLINS, CO, 80525, USA, or write via E-mail john@2ad.com.
*/

#include "gmanloadable.h"
#include "gmansurfaceshader.h"
#include "gmanshaderparams.h"

/*
 * surface matte(float Ka = 1, Kd = 1)
 * {
 *   normal Nf = faceforward(normalize(N), I);
 *   Oi = Os;
 *   Ci = Os * Cs * (Ka*ambient() + Kd*diffuse(Nf));
 * }
 *
 * The RISpec's own default surface shader; also this renderer's fallback
 * when RiSurface was never called (GMANAttributes::setSurface's fallback,
 * step 6).
 */
class GMANMatte : public GMANSurfaceShader
{
public:
  RtVoid illuminance (RtInt i, GMANVector L, GMANColor Cl, GMANColor Ol);

  /*
   * Output of a surface shader
   */

  const GMANColor &computeCi(GMANSurfaceEnv &se);
  const GMANColor &computeOi(GMANSurfaceEnv &se);
};

RtVoid GMANMatte::illuminance (RtInt /*i*/, GMANVector /*L*/,
			       GMANColor /*Cl*/, GMANColor /*Ol*/)
{
  // Unused: computeCi below sums lights itself via env.ambient()/
  // diffuse(), the C++-shader equivalent of an SL illuminance() loop.
}

const GMANColor &GMANMatte::computeCi(GMANSurfaceEnv &se)
{
  static GMANColor ci;

  RtFloat ka = gmanshaders::getFloatParam(pl, RI_KA, 1.0);
  RtFloat kd = gmanshaders::getFloatParam(pl, RI_KD, 1.0);

  GMANVector nf = se.faceforward(se.N, se.I, se.Ng);

  GMANColor lit = se.ambient();
  lit.scale(ka);
  GMANColor diff = se.diffuse(nf);
  diff.scale(kd);
  lit += diff;

  ci = GMANColor(se.Cs.getRed() * se.Os.getRed() * lit.getRed(),
		 se.Cs.getGreen() * se.Os.getGreen() * lit.getGreen(),
		 se.Cs.getBlue() * se.Os.getBlue() * lit.getBlue());
  return ci;
}

const GMANColor &GMANMatte::computeOi(GMANSurfaceEnv &se)
{
  static GMANColor oi;
  oi = se.Os;
  return oi;
}

static GMANLoadableObjectInfo loadableInfo = {
  "Matte surface shader",
  "Ken Geis <kgeis@alum.calberkeley.org>",
  "Copyright (c) 2001 Ken Geis, Licenced under the GNU Lesser Public License, http://www.gnu.org",
  "A GMAN SurfaceShader for matte surfaces.",
};

static GMANMatte shader;


extern "C" GMANLoadableObjectInfo *GMANGetLoadableInfo(void) {
  return &loadableInfo;
}

extern "C" GMANShader *GMANLoadShader(void) {
  return &shader;
}
