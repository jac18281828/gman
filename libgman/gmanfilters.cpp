/* SPDX-License-Identifier: LGPL-2.1-or-later */

/* This is part of GMAN, a RenderMan-compatible renderer.
 *
 * Copyright (c) 2001, 2000, 1999 John Cairns 
 *
 * Author: John Cairns <john@2ad.com>
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

#include <math.h>
#include "ri.h"
#include "gmanmath.h"



/*
 * RenderMan C API Filter functions
 *
 * Standard pixel-reconstruction filters, per the RISpec. Tested now
 * (tests/filters_test.cpp), but still uncalled by any resolve: the one
 * real reference is a dead one, GMANFrameBuffer's constructors defaulting
 * `filter` to RiBoxFilter and getSuperSampledPixel calling it through
 * that member -- and getSuperSampledPixel itself has no caller. Two more
 * places name these functions without calling them: gmandefaults.cpp's
 * DefaultFilterFunc stores RiGaussianFilter as a pointer, never
 * dereferenced, and gmanascii.cpp compares filter function pointers by
 * address in roughly two dozen places to pick a RIB keyword to echo --
 * neither is a call. A filter that silently returns 1.0 for every (x,y)
 * is a landmine for whoever wires up sampling next.
 *
 * RiTriangleFilter returns zero outside its support; RiGaussianFilter's
 * argument is rescaled by 2/width before squaring, matching the RISpec
 * reference.
 */


extern "C" RtFloat   RiGaussianFilter(RtFloat x, RtFloat y,
				      RtFloat xwidth, RtFloat ywidth)
{
  // RISpec reference scales the argument by 2/width before squaring.
  RtFloat dx = 2.0 * x / xwidth;
  RtFloat dy = 2.0 * y / ywidth;
  return exp(-2.0 * (dx*dx + dy*dy));
};


extern "C" RtFloat   RiBoxFilter(RtFloat /*x*/, RtFloat /*y*/,
				 RtFloat /*xwidth*/, RtFloat /*ywidth*/)
{
  return 1.0;
};


extern "C" RtFloat   RiTriangleFilter(RtFloat x, RtFloat y,
				      RtFloat xwidth, RtFloat ywidth)
{
  if (fabs(x) > xwidth / 2.0 || fabs(y) > ywidth / 2.0) {
    return 0.0;
  }
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

