/* SPDX-License-Identifier: LGPL-2.1-or-later */

/* This is part of the GNU GMAN Library, a FREE implementation of the
 * RenderMan Interface Specification.
 *
 * Copyright (c) 2001, 2000, 1999 John Cairns 
 *
 * Author: John Cairns <john@2ad.com>
 */

/* LJL - March 2001 - Moved Environment from shaders here */

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

#ifndef _GMANSHADERENVIRONMENT_H
#define _GMANSHADERENVIRONMENT_H 1

#include <cmath>
#include <vector>

#include "ri.h"
#include "gmancolor.h"
#include "gmanpoint.h"
#include "gmanvector.h"
#include "gmannormal.h"
#include "gmanlightsourcemgr.h"
#include "gmanslapi.h"

// Forward-declared, not included: gmannoise.h #defines N (and MASK) at
// file scope with no #undef, which collides with GMANSurfaceEnv's own N
// (the shading normal) the moment both are visible in one translation
// unit. The noise family below is declared here and defined in
// gmanshaderenvironment.cpp, the one file that can safely include
// gmannoise.h.
class GMANNoise;

/*
 * The interface a surface shader is written against -- and the interface
 * a future shading-language VM would target, since a C++ shader and an
 * SL-compiled one both need the same inputs and the same builtins. Every
 * field is camera space (see AGENTS.md's "Coordinate spaces"); every
 * shadeop below forwards to the SL runtime in gmannoise.cpp/gmanslapi.cpp,
 * which existed, worked and had no caller before this phase.
 */
struct GMAN_EXPORT GMANSurfaceEnv
{
  GMANColor		Cs;	// surface color
  GMANColor		Os;	// surface opacity
  GMANPoint		P;	// surface position
  GMANVector		dPdu;	// derivative of surface position along u.
  GMANVector		dPdv;	// derivative of surface position along v.
  GMANNormal		N;      // surface shading normal
  GMANNormal		Ng;	// surface geometric normal

  RtFloat		u;	// surface parameters
  RtFloat		v;	// surface parameters

  RtFloat		du;     // change in surface parameters
  RtFloat		dv;     // change in surface parameters

  RtFloat		s;      // change in surface texture coordinates
  RtFloat		t;      // change in surface texture coordinates

  GMANPoint		E;      // position of the eye
  GMANVector		I;      // incident ray direction

  RtFloat		ncomps; // Number of color components
  RtFloat		time;   // current shutter time
  RtFloat               dtime;  // amount of time covered by this shading sample
  GMANVector            dPdtime;

  GMANColor		Ci;     // incident ray color
  GMANColor		Oi;     // incident ray opacity

  // Lights active in the attribute scope this surface was declared in
  // (RiIlluminate), resolved from handle to light at shading time. Not
  // owned: these point into gmanLightSourceMgr()'s storage.
  std::vector<const GMANLight *> lights;

  // ---- noise family (gmannoise.cpp), defined in
  // gmanshaderenvironment.cpp ----
  // A single generator shared by every GMANSurfaceEnv, matching real SL's
  // noise() being a pure function of its argument, not of shader
  // instance. gmannoise.cpp's periodic() is RSL's pnoise().
  RtFloat noise (RtFloat v) const;
  RtFloat noise (RtFloat u_, RtFloat v_) const;
  RtFloat noise (const GMANPoint &p) const;
  RtFloat noise (const GMANPoint &p, RtFloat t_) const;

  RtFloat pnoise (RtFloat v, RtFloat pv) const;
  RtFloat pnoise (const GMANPoint &p, const GMANPoint &pp) const;

  RtFloat cellnoise (RtFloat v) const;
  RtFloat cellnoise (const GMANPoint &p) const;

  // ---- gmanslapi.cpp: already free functions, forwarded here so a
  // shader reaches every builtin the same way, through env. Named
  // GMANReflect/GMANRefract/GMANFresnel/GMANFaceForward in gmanslapi.cpp;
  // gmanslapi.h previously declared unprefixed lowercase forms with no
  // definition anywhere -- fixed alongside this, see gmanslapi.h. ----
  GMANVector reflect (const GMANVector &i, const GMANVector &n) const {
    return GMANReflect(i, n);
  }
  GMANVector refract (const GMANVector &i, const GMANVector &n,
		       RtFloat eta) const {
    return GMANRefract(i, n, eta);
  }
  RtVoid fresnel (const GMANVector &i, const GMANVector &n, RtFloat eta,
		  RtFloat &kr, RtFloat &kt) const {
    GMANFresnel(i, n, eta, kr, kt);
  }
  GMANVector faceforward (const GMANVector &n, const GMANVector &i,
			   const GMANVector &nr) const {
    return GMANFaceForward(n, i, nr);
  }
  RtFloat smoothstep (RtFloat min, RtFloat max, RtFloat value) const {
    return GMANSmoothStep(min, max, value);
  }

  template <class T>
  T spline (const std::string &basis, RtFloat value, RtInt nvals,
	    T fvals[]) const {
    if (basis == "bezier") return GMANBezierSpline<T>(value, nvals, fvals);
    if (basis == "bspline") return GMANBsplineSpline<T>(value, nvals, fvals);
    if (basis == "hermite") return GMANHermiteSpline<T>(value, nvals, fvals);
    if (basis == "linear") return GMANLinearSpline<T>(value, nvals, fvals);
    return GMANCatmullSpline<T>(value, nvals, fvals);  // "catmull-rom", default
  }

  // ---- illuminance loop, RiSL's own shape: ambient()/diffuse()/
  // specular() sum every currently-active light's contribution at P, so a
  // shader's own body stays the couple of lines matte/plastic/metal are
  // in the RISpec. Blinn-Phong for specular(), the common approximation
  // to the RISpec's own (more expensive) integral. ----
  GMANColor ambient (RtVoid) const {
    GMANColor sum;
    for (std::size_t i = 0; i < lights.size(); ++i) {
      if (lights[i]->getType() != GMAN_LIGHT_AMBIENT) {
	continue;
      }
      GMANVector l;
      GMANColor cl;
      lights[i]->sample(P, l, cl);
      sum += cl;
    }
    return sum;
  }

  GMANColor diffuse (const GMANVector &n) const {
    GMANVector nn(n);
    nn.normalize();
    GMANColor sum;
    for (std::size_t i = 0; i < lights.size(); ++i) {
      if (lights[i]->getType() == GMAN_LIGHT_AMBIENT) {
	continue;
      }
      GMANVector l;
      GMANColor cl;
      lights[i]->sample(P, l, cl);
      l.normalize();
      RtFloat nDotL = nn.dot(l);
      if (nDotL > 0.0) {
	cl.scale(nDotL);
	sum += cl;
      }
    }
    return sum;
  }

  GMANColor specular (const GMANVector &n, const GMANVector &v,
		       RtFloat roughness) const {
    GMANVector nn(n);
    nn.normalize();
    GMANVector vv(v);
    vv.normalize();
    GMANColor sum;
    for (std::size_t i = 0; i < lights.size(); ++i) {
      if (lights[i]->getType() == GMAN_LIGHT_AMBIENT) {
	continue;
      }
      GMANVector l;
      GMANColor cl;
      lights[i]->sample(P, l, cl);
      l.normalize();
      GMANVector h(l.getX() + vv.getX(), l.getY() + vv.getY(),
		   l.getZ() + vv.getZ());
      h.normalize();
      RtFloat nDotH = nn.dot(h);
      if (nDotH > 0.0) {
	RtFloat exponent = (roughness > RI_EPSILON) ? (1.0 / roughness)
						     : (1.0 / RI_EPSILON);
	cl.scale((RtFloat) pow(nDotH, exponent));
	sum += cl;
      }
    }
    return sum;
  }
};

struct GMAN_EXPORT GMANLightEnv
{
  GMANPoint		P;	// surface position
  GMANVector		dPdu;	// derivative of surface position along u.
  GMANVector		dPdv;	// derivative of surface position along v.
  GMANNormal		N;      // surface shading normal
  GMANNormal		Ng;	// surface geometric normal

  RtFloat		u;	// surface parameters
  RtFloat		v;	// surface parameters

  RtFloat		du;     // change in surface parameters
  RtFloat		dv;     // change in surface parameters

  RtFloat		s;      // change in surface texture coordinates
  RtFloat		t;      // change in surface texture coordinates

  GMANPoint		Ps;     // Position being illuminated
  GMANPoint		E;      // position of the eye

  RtFloat		ncomps; // Number of color components
  RtFloat		time;   // current shutter time
  RtFloat               dtime;  // amount of time covered by this shading sample

  GMANColor             Cl;     // Outgoing light ray color
  GMANColor             Ol;     // Outgoing light ray opacity
};

struct GMAN_EXPORT GMANVolumeEnv
{
  GMANPoint		P;	// surface position

  GMANVector            I;      // incident ray direction
  GMANPoint		E;      // position of the eye

  GMANColor		Ci;     // incident ray color
  GMANColor		Oi;     // incident ray opacity

  RtFloat		ncomps; // Number of color components
  RtFloat		time;   // current shutter time
  RtFloat               dtime;  // amount of time covered by this shading sample
};

struct GMAN_EXPORT GMANDisplacementEnv
{
  GMANPoint		P;	// surface position
  GMANVector		dPdu;	// derivative of surface position along u.
  GMANVector		dPdv;	// derivative of surface position along v.
  GMANNormal		N;      // surface shading normal
  GMANNormal		Ng;	// surface geometric normal

  GMANPoint		E;      // position of the eye

  RtFloat		u;	// surface parameters
  RtFloat		v;	// surface parameters

  RtFloat		du;     // change in surface parameters
  RtFloat		dv;     // change in surface parameters

  RtFloat		s;      // change in surface texture coordinates
  RtFloat		t;      // change in surface texture coordinates

  RtFloat		ncomps; // Number of color components
  RtFloat		time;   // current shutter time
  RtFloat               dtime;  // amount of time covered by this shading sample
  GMANVector            dPdtime;
};

struct GMAN_EXPORT GMANImagerEnv
{
  GMANPoint		P;	// Pixel raster position
  
  GMANColor		Ci;     // Pixel color
  GMANColor		Oi;     // Pixel opacity
  RtFloat		alpha;  // fractional pixel coverage

  RtFloat		ncomps; // Number of color components
  RtFloat		time;   // current shutter time
  RtFloat               dtime;  // amount of time covered by this shading sample
};

#endif
