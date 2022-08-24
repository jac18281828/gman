/* This is part of the GNU GMAN Library, a FREE implementation of the
 * RenderMan Interface Specification.
 *
 * Copyright (c) 2001, 2000, 1999 by John Cairns 
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
  FT COLLINS, CO 80525, USA, or write via E-mail john@2ad.com.
*/
 

#ifndef __GMAN_GMANSLAPI_H
#define __GMAN_GMANSLAPI_H 1


/* Headers */

// STL
#include <list>
#include <map>
#include <stack>
#include <string>

// the renderman interface
#include "ri.h"
// the universal super class declaration
#include "universalsuperclass.h"

#include "gmancolor.h"
#include "gmanpoint.h"
#include "gmanvector.h"

/*
 * RenderMan SL API 
 *
 */

// min takes a list of two or more arguments and return the argument with
// minimun value, on a component-by-components basis
GMANColor  GMANDLL GMANMin(GMANColor  a, GMANColor  b ...);
GMANPoint  GMANDLL GMANMin(GMANPoint  a, GMANPoint  b ...);
GMANVector GMANDLL GMANMin(GMANVector a, GMANVector b ...);

// max takes a list of two or more arguments and return the argument with
// maximun value, on a component-by-components basis
GMANColor  GMANDLL GMANMax(GMANColor  a, GMANColor  b ...);
GMANPoint  GMANDLL GMANMax(GMANPoint  a, GMANPoint  b ...);
GMANVector GMANDLL GMANMax(GMANVector a, GMANVector b ...);

// clamp returns min if a is less than min, max if a is greater than max;
// otherwise it returns a, on a component-by-components basis
GMANDLL GMANColor  GMANClamp(GMANColor  a, GMANColor  min, GMANColor  max);
GMANDLL GMANPoint  GMANClamp(GMANPoint  a, GMANPoint  min, GMANPoint  max);
GMANDLL GMANVector GAMNClamp(GMANVector a, GMANVector min, GMANVector max);

// mix returns a*(1-alpha) + b*alpha, that is, it performs a linear
// blend between values a and b, on a component-by-components basis
GMANDLL GMANColor  GMANMix(GMANColor  a, GMANColor  b, RtFloat alpha);
GMANDLL GMANPoint  GMANMix(GMANPoint  a, GMANPoint  b, RtFloat alpha);
GMANDLL GMANVector GMANMix(GMANVector a, GMANVector b, RtFloat alpha);

// step returns 0 if value is less than min, otherwise if returns 1
inline GMANDLL RtFloat GMANStep(RtFloat min, RtFloat value)
{
	return value < min ? 0.0 : 1.0;
}

// smoothstep returns 0 if value is less than min, 1 if value is greater than
// or equal to max, and performs a smooth Hermite interpolation between 0 and 1
// in the interval min to max
GMANDLL RtFloat GMANSmoothStep(RtFloat min, RtFloat max, RtFloat value);

// filterstep provides an analytically antialiased step function
GMANDLL RtFloat GMANFilterStep(RtFloat edge, RtFloat s1 ...);

// the spline familly fits a spline to the control points given
// LJL - spline functions added - February 2001
//  #define RI_CATMULLROMSTEP              ((RtInt)1)
//  RtBasis RiCatmullRomBasis = { {-1.0/2,  3.0/2, -3.0/2,  1.0/2},
//                                { 2.0/2, -5.0/2,  4.0/2, -1.0/2}, 
//                                {-1.0/2,  0.0/2,  1.0/2,  0.0/2},
//                                { 0.0/2,  2.0/2,  0.0/2,  0.0/2} };
template <class T>
T GMANCatmullSpline(RtFloat value, RtInt nvals, T fvals[])
{
  RtInt nbSeg=nvals-3;
  RtFloat temp=value*nbSeg;
  RtInt ptr=(RtInt) temp;
  value=temp-ptr;
  RtFloat vv=value*value;
  RtFloat vvv=vv*value;

  RtFloat c0,c1,c2,c3;
  c0=-0.5*vvv+vv-0.5*value;
  c1=1.5*vvv-2.5*vv+1;
  c2=-1.5*vvv+2.0*vv+0.5*value;
  c3=0.5*vvv-0.5*vv;

  return (fvals[ptr]*c0 + fvals[ptr+1]*c1 + fvals[ptr+2]*c2 + fvals[ptr+3]*c3);
}

//  #define RI_BEZIERSTEP              ((RtInt)3)
//  RtBasis RiBezierBasis= { {-1,  3, -3,  1},
//                           { 3, -6,  3,  0}, 
//                           {-3,  3,  0,  0},
//                           { 1,  0,  0,  0} };
template <class T>
T GMANBezierSpline(RtFloat value, RtInt nvals, T fvals[])
{
  RtInt nbSeg=1+(nvals-4)/3;
  RtFloat temp=value*nbSeg;
  RtInt ptr=(RtInt) temp;
  value=temp-ptr;
  RtFloat vv=value*value;
  RtFloat vvv=vv*value;

  RtFloat c0,c1,c2,c3;
  c0=-vvv+3.0*vv-3.0*value+1.0;
  c1=3.0*vvv-6.0*vv+3.0*value;
  c2=-3.0*vvv+3.0*vv;
  c3=vvv;

  return (fvals[ptr]*c0 + fvals[ptr+1]*c1 + fvals[ptr+2]*c2 + fvals[ptr+3]*c3);
}

//  #define RI_BSPLINESTEP              ((RtInt)1)
//  RtBasis RiBSplineBasis = { {-1.0/6,  3.0/6, -3.0/6,  1.0/6},
//                             { 3.0/6, -6.0/6,  3.0/6,  0.0/6}, 
//                             {-3.0/6,  0.0/6,  3.0/6,  0.0/6},
//                             { 1.0/6,  4.0/6,  1.0/6,  0.0/6} };
template <class T>
T GMANBsplineSpline(RtFloat value, RtInt nvals, T fvals[])
{
  RtInt nbSeg=nvals-3;
  RtFloat temp=value*nbSeg;
  RtInt ptr=(RtInt) temp;
  value=temp-ptr;
  RtFloat vv=value*value;
  RtFloat vvv=vv*value;

  RtFloat c0,c1,c2,c3;
  c0=-1.0/6*vvv+0.5*vv-0.5*value+1/6;
  c1=0.5*vvv-vv+2/3;
  c2=-0.5*vvv+0.5*vv+0.5*value+1/6;
  c3=1/6*vvv;

  return (fvals[ptr]*c0 + fvals[ptr+1]*c1 + fvals[ptr+2]*c2 + fvals[ptr+3]*c3);
}

//  #define RI_HERMITESTEP              ((RtInt)2)
//  RtBasis RiHermiteBasis = { { 2,  1, -2,  1},
//                             {-3, -2,  3, -1}, 
//                             { 0,  1,  0,  0},
//                             { 1,  0,  0,  0} };
template <class T>
T GMANHermiteSpline(RtFloat value, RtInt nvals, T fvals[])
{
  RtInt nbSeg=1+(nvals-4)/2;
  RtFloat temp=value*nbSeg;
  RtInt ptr=(RtInt) temp;
  value=temp-ptr;
  RtFloat vv=value*value;
  RtFloat vvv=vv*value;

  RtFloat c0,c1,c2,c3;
  c0=2*vvv-3.0*vv+1.0;
  c1=vvv-2.0*vv+value;
  c2=-2.0*vvv+3.0*vv;
  c3=vvv-vv;

  return (fvals[ptr]*c0 + fvals[ptr+1]*c1 + fvals[ptr+2]*c2 + fvals[ptr+3]*c3);
}

template <class T>
T GMANLinearSpline(RtFloat value, RtInt nvals, T fvals[])
{
  RtInt nbSeg=1+(nvals-4);
  RtFloat temp=value*nbSeg;
  RtInt ptr=(RtInt) temp;
  value=temp-ptr;

  return (fvals[ptr+1]*(1-value) + fvals[ptr+2]*value);
}


GMANDLL RtFloat    distance (const GMANPoint &p1, const GMANPoint &p2);
GMANDLL RtFloat    ptlined (const GMANPoint &p0, const GMANPoint &p1, const GMANPoint &q);
GMANDLL GMANPoint  rotate (const GMANPoint &q, RtFloat angle,
					   const GMANPoint &p1, const GMANPoint &p2);
GMANDLL GMANVector faceforward (const GMANVector &n, const GMANVector &i,
			const GMANVector &nr);
GMANDLL GMANVector reflect (const GMANVector &i, const GMANVector &n);
GMANDLL GMANVector refract (const GMANVector &i, const GMANVector &n, RtFloat eta);
GMANDLL RtVoid     fresnel (const GMANVector &i, const GMANVector &n, RtFloat eta,
		    RtFloat &kr, RtFloat &kt);
GMANDLL RtVoid     fresnel (const GMANVector &i, const GMANVector &n, RtFloat eta,
		    RtFloat &kr, RtFloat &kt,
		    GMANVector &r, GMANVector &t);


#endif
