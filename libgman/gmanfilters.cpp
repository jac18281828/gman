/* SPDX-License-Identifier: LGPL-2.1-or-later */

/* This is part of the GNU GMAN Library, a FREE implementation of the
 * RenderMan Interface Specification.
 *
 * Copyright (c) 2001, 2000, 1999 John Cairns 
 *
 * Author: John Cairns <john@2ad.com>
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

#include <math.h>
#include "ri.h"
#include "gmanmath.h"



/*
 * RenderMan C API Filter functions
 *
 * Standard pixel-reconstruction filters, per the RISpec. None has a caller
 * yet -- the renderer stays at 1 sample/pixel this phase -- but a filter
 * that silently returns 1.0 for every (x,y) is a landmine for whoever wires
 * up sampling next.
 */


extern "C" RtFloat   RiGaussianFilter(RtFloat x, RtFloat y,
				      RtFloat xwidth, RtFloat ywidth)
{
  return exp(-2.0 * (x*x/(xwidth*xwidth) + y*y/(ywidth*ywidth)));
};


extern "C" RtFloat   RiBoxFilter(RtFloat /*x*/, RtFloat /*y*/,
				 RtFloat /*xwidth*/, RtFloat /*ywidth*/)
{
  return 1.0;
};


extern "C" RtFloat   RiTriangleFilter(RtFloat x, RtFloat y,
				      RtFloat xwidth, RtFloat ywidth)
{
  return (1.0 - fabs(x) / (xwidth / 2.0)) * (1.0 - fabs(y) / (ywidth / 2.0));
};


static RtFloat catmullRom1D(RtFloat x)
{
  // Catmull-Rom cubic convolution kernel, a = -0.5 (the RISpec's choice).
  RtFloat ax = fabs(x);
  if (ax < 1.0) {
    return (3.0*ax*ax*ax - 5.0*ax*ax + 2.0) / 2.0;
  }
  if (ax < 2.0) {
    return (-ax*ax*ax + 5.0*ax*ax - 8.0*ax + 4.0) / 2.0;
  }
  return 0.0;
}

extern "C" RtFloat   RiCatmullRomFilter(RtFloat x,RtFloat y,
					RtFloat /*xwidth*/, RtFloat /*ywidth*/)
{
  return catmullRom1D(x) * catmullRom1D(y);
};


static RtFloat sinc1D(RtFloat x)
{
  if (fabs(x) < RI_EPSILON) {
    return 1.0;
  }
  RtFloat px = (RtFloat)PI * x;
  return sin(px) / px;
}

extern "C" RtFloat  RiSincFilter(RtFloat x, RtFloat y,
				 RtFloat /*xwidth*/, RtFloat /*ywidth*/)
{
  return sinc1D(x) * sinc1D(y);
};

