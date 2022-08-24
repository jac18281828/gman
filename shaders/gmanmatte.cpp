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

#include <iostream>
#if HAVE_STD_NAMESPACE
using std::cerr;
using std::endl;
#endif

#include "gmanloadable.h"
#include "gmansurfaceshader.h"


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

RtVoid GMANMatte::illuminance (RtInt i, GMANVector L,
			       GMANColor Cl, GMANColor Ol)
{
  // FIXME
  cerr << "GMANMatte::illuminance" << endl;
}

const GMANColor &GMANMatte::computeCi(GMANSurfaceEnv &se)
{
  // FIXME
  cerr << "computeCi" << endl;
  return GMANColor();
}

const GMANColor &GMANMatte::computeOi(GMANSurfaceEnv &se)
{
  // FIXME
  cerr << "computeOi" << endl;
  return GMANColor();
}

static GMANLoadableObjectInfo info = {
  "Matte surface shader",
  "Ken Geis <kgeis@alum.calberkeley.org>"
  "Copyright (c) 2001 Ken Geis, Licenced under the GNU Lesser Public License, http://www.gnu.org",
  "A GMAN SurfaceShader for matte surfaces.",
};

static GMANMatte shader;


extern "C" GMANLoadableObjectInfo *GMANGetLoadableInfo(void) {
  return &info;
}

extern "C" GMANShader *GMANLoadShader(void) {
  return &shader;
}
