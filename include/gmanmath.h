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
 

#ifndef __GMAN_MATH_H
#define __GMAN_MATH_H 1

// Make this file self-contained - asandro
#include <math.h>

#include "ri.h"

// GMan constants
const double PI       = 3.1415926535898;
const double DEGTORAD = PI/180.0;

#define GMANMAX(x,y) (x>y?x:y)
#define GMANMIN(x,y) (x<y?x:y)

#if 0
inline GMANDLL  RtFloat GMANClamp(RtFloat value, RtFloat min, RtFloat max)
{
  if(value < min) return min;
  if(value > max) return max;
  return value;
}
#endif

// Trigonometric functions - asandro
inline GMANDLL  RtFloat GMANRadians(RtFloat degrees)
{
	return (RtFloat)(degrees * DEGTORAD);
}

inline GMANDLL  RtFloat GMANDegrees(RtFloat radians)
{
	return (RtFloat)(radians / DEGTORAD);
}

inline GMANDLL  RtFloat GMANAtan(RtFloat y, RtFloat x)
{
	return (RtFloat)atan2(y, x);
}


// Square root & logarithmic - asandro
inline GMANDLL const RtFloat GMANInversesqrt(RtFloat x)
{
	RtFloat y = (RtFloat)sqrt(x);
	// avoid division by zero
	if(y - RI_EPSILON > 0.0) {
		return (RtFloat)(1.0 / y);
	} else {
		return (const RtFloat)RI_INFINITY;
	}
}

inline GMANDLL RtFloat GMANLogFn(RtFloat x, RtFloat base)
{
	return (RtFloat)(log(x) / log(base));
}

// Module functions - asandro
inline GMANDLL RtFloat GMANMod(RtFloat a, RtFloat b)
{
  if (a<0) return b-(RtFloat)fmod(-a,b);
  else return (RtFloat)fmod(a,b);
}

inline GMANDLL RtFloat GMANSign(RtFloat x)
{
	return (RtFloat)(x < 0.0 ? -1.0 : x > 0.0 ? 1.0 : 0.0);
}

// Comparison functions - asandro
template<class T> inline T GMANMin(T a, T b)
{
	return a < b ? a : b;
}

template<class T> inline T GMANMax(T a, T b)
{
	return a > b ? a : b;
}

template<class T> inline T GMANClamp(T a, T min, T max)
{
	return a < min ? min : a > max ? max : a;
}


// Mixing values - asandro
template<class T> inline T GMANMix(T a, T b, RtFloat alpha)
{
	return a*(1-alpha) + b*alpha;
}

// Rounding - asandro
inline RtFloat GMANRound(RtFloat x)
{
	return (RtFloat)floor(x+.5);
}

#endif
