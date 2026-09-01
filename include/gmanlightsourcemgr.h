/* SPDX-License-Identifier: LGPL-2.1-or-later */

/* This is part of the GNU GMAN Library, a FREE implementation of the
 * RenderMan Interface Specification.
 *
 * Copyright (c) 2001, 2000, 1999 John Cairns 
 *
 * Author: John Cairns <john@2ad.com>
 */

// Added Light List LJL 2000/08/08

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
 

#ifndef __GMAN_GMANLIGHTSOURCEMGR_H
#define __GMAN_GMANLIGHTSOURCEMGR_H 1


/* Headers */

// STL
#include <list>
#include <map>
#include <stack>
#include <string>

// the renderman interface
#include "ri.h"
// logging
#include "gmanlog.h"
#include "gmancolor.h"
#include "gmanpoint.h"
#include "gmanvector.h"

/*
 * A built-in light: ambientlight, distantlight or pointlight. Position and
 * direction are captured in camera space at RiLightSourceV time (the CTM
 * then in effect), per AGENTS.md's "Coordinate spaces" -- shading happens
 * entirely in camera space, so a light declared in any other space would
 * make every N.L wrong.
 */
enum GMANLightType {
  GMAN_LIGHT_AMBIENT,
  GMAN_LIGHT_DISTANT,
  GMAN_LIGHT_POINT
};

class GMAN_EXPORT GMANLight {
private:
  GMANLightType type;
  GMANColor     cl;        // color * intensity, RiLightSourceV time
  GMANPoint     position;  // camera space; pointlight only
  GMANVector    direction; // camera space, light -> scene; distantlight only

public:
  GMANLight(GMANLightType t, const GMANColor &c,
	    const GMANPoint &pos, const GMANVector &dir)
    : type(t), cl(c), position(pos), direction(dir) {}

  GMANLightType getType(RtVoid) const { return type; }

  // The direction from a surface point toward this light and this
  // light's contribution there. Ambient has no direction -- illuminance
  // loops skip it, and a shader adds env.ambient() directly instead, per
  // RiSL convention. Point lights get inverse-square falloff baked into
  // Cl here so a shader's own math stays a plain N.L.
  RtVoid sample(const GMANPoint &p, GMANVector &l, GMANColor &lightCl) const;
};

/*
 * RenderMan API GMANLightSourceMgr
 *
 * Owns every light RiLightSourceV has declared, keyed by the handle it
 * returned. GMANLightList (below, attribute-scoped) tracks which handles
 * are currently switched on; this is where a handle resolves to the
 * actual light.
 */

class GMAN_EXPORT  GMANLightSourceMgr {
private:
  std::map<RtLightHandle, GMANLight *> lights;
  // RtLightHandle is RtPointer (void*); a plain counter, cast to
  // RtLightHandle at handout, is simpler than allocating an object per
  // handle just to have a unique address.
  RtInt nextHandle;

public:
  GMANLightSourceMgr(); // default constructor

  ~GMANLightSourceMgr(); // default destructor

  // Takes ownership of light; returns the handle RiLightSourceV hands back
  // to the caller and RiIlluminate later toggles.
  RtLightHandle add(GMANLight *light);

  // NULL if h names no light this manager holds.
  const GMANLight *get(RtLightHandle h) const;
};

// One light manager per render, like the object and world managers this
// mirrors. A free function rather than a GMANRenderManImpl member so
// createParametric (tessellation time, where a primitive's lit vertices
// are shaded) does not need a light manager threaded through every
// GMANObjectManager::getRS* signature to reach it.
GMAN_EXPORT GMANLightSourceMgr &gmanLightSourceMgr(RtVoid);

/*
 * A class for light lists storage
 */

class GMAN_EXPORT GMANLightList
{
private:
  std::list<RtLightHandle> ll;
public:
  RtVoid  on  (RtLightHandle h);
  RtVoid  off (RtLightHandle h);

  const std::list<RtLightHandle> &getHandles(RtVoid) const { return ll; }
};

#endif



