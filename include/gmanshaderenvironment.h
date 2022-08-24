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

#include "ri.h"
#include "gmancolor.h"
#include "gmanpoint.h"
#include "gmanvector.h"
#include "gmannormal.h"

struct GMANDLL GMANSurfaceEnv
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
};

struct GMANDLL GMANLightEnv
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

struct GMANDLL GMANVolumeEnv
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

struct GMANDLL GMANDisplacementEnv
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

struct GMANDLL GMANImagerEnv
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
