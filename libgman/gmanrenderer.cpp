/* This is part of the GNU GMAN Library, a FREE implementation of the
 * RenderMan Interface Specification.
 *
 * Copyright (c) 2001, 2000, 1999  John Cairns 
 *
 * Author: John Cairns <john@2ad.com>
 */

/* LJL - March 2001 - added lightBeams and solarBeams */

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

/* Local Headers */
#include "ri.h"      /* RenderMan Interface */
#include "universalsuperclass.h" /* Super class */
#include "gmanrenderer.h" /* Declaration Header */


/*
 * RenderMan API GMANRenderer
 *
 */

// default constructor
GMANRenderer::GMANRenderer() : UniversalSuperClass() { };


// default destructor 
GMANRenderer::~GMANRenderer() { };


// Test if the apex of the illuminance cone is inside illuminate cone
// and vice-versa
bool GMANRenderer::lightBeams (GMANPoint pos1, GMANVector axis1, RtFloat angle1,
			       GMANPoint pos2, GMANVector axis2, RtFloat angle2)
{
  GMANVector p1p2(pos1,pos2);
  p1p2.normalize();
  GMANVector p2p1(-p1p2);

  axis1.normalize();
  RtFloat alpha;

  alpha=p1p2.dot(axis1);
  if (cos(angle1) > alpha) return false;

  axis2.normalize();
  alpha=p2p1.dot(axis2);
  if (cos(angle2) > alpha) return false;

  return true;
}

// test if an illuminance statement can receive light
// from a solar lightsource.
bool GMANRenderer::solarBeams (GMANVector axis1, RtFloat angle1,
			       GMANVector axis2, RtFloat angle2)
{
  RtFloat angle=angle1+angle2;
  if (angle > PI ) angle=PI;

  axis1.normalize();
  axis2.normalize();
  if (-axis1.dot(axis2) < cos(angle)) return false;
  return true;
}
