/* SPDX-License-Identifier: LGPL-2.1-or-later */

/* This is part of the GNU GMAN Library, a FREE implementation of the
 * RenderMan Interface Specification.
 *
 * Copyright (c) 2001, 2000, 1999  John Cairns 
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

  To contact the author of GNU GMAN, write to John Cairns, 607 E STUART ST, 
  FT COLLINS, CO, 80525, USA, or write via E-mail john@2ad.com.
*/

#include <cstdint>

/* Local Headers */
#include "ri.h"      /* RenderMan Interface */
#include "gmanlog.h"
#include "gmanlightsourcemgr.h" /* Declaration Header */

/*
 * GMANLight
 *
 */

RtVoid GMANLight::sample(const GMANPoint &p, GMANVector &l,
			  GMANColor &lightCl) const
{
  switch (type) {
  case GMAN_LIGHT_AMBIENT:
    // No direction: illuminance loops (which need an L to dot against N)
    // skip ambient lights entirely rather than calling this.
    l = GMANVector(0.0, 0.0, 0.0);
    lightCl = cl;
    break;

  case GMAN_LIGHT_DISTANT:
    // direction is light -> scene; a surface receives light from the
    // opposite way, per the RiSL solar()/illuminate() convention.
    l = GMANVector(-direction.getX(), -direction.getY(), -direction.getZ());
    lightCl = cl;
    break;

  case GMAN_LIGHT_POINT: {
    l = GMANVector(position, p);
    l = GMANVector(-l.getX(), -l.getY(), -l.getZ());  // p -> position
    RtFloat dist2 = l.getX()*l.getX() + l.getY()*l.getY() + l.getZ()*l.getZ();
    RtFloat falloff = (dist2 > RI_EPSILON) ? (1.0 / dist2) : 1.0;
    lightCl = GMANColor(cl.getRed() * falloff, cl.getGreen() * falloff,
			 cl.getBlue() * falloff);
    break;
  }
  }
}

/*
 * RenderMan API GMANLightSourceMgr
 *
 */

// default constructor
GMANLightSourceMgr::GMANLightSourceMgr() : nextHandle(1) { };


// default destructor
GMANLightSourceMgr::~GMANLightSourceMgr() {
  for (std::map<RtLightHandle, GMANLight *>::iterator it = lights.begin();
       it != lights.end(); ++it) {
    delete it->second;
  }
};

RtLightHandle GMANLightSourceMgr::add(GMANLight *light) {
  // RtLightHandle is RtPointer; go through uintptr_t rather than casting
  // an int straight to a pointer (the same integer<->pointer-width
  // mismatch class SPEC.md records for RiObjectInstance/RiIlluminate).
  RtLightHandle h = (RtLightHandle)(std::uintptr_t) nextHandle;
  lights[h] = light;
  ++nextHandle;
  return h;
}

const GMANLight *GMANLightSourceMgr::get(RtLightHandle h) const {
  std::map<RtLightHandle, GMANLight *>::const_iterator it = lights.find(h);
  if (it == lights.end()) {
    return NULL;
  }
  return it->second;
}

GMANLightSourceMgr &gmanLightSourceMgr(RtVoid) {
  static GMANLightSourceMgr mgr;
  return mgr;
}



/*
 *  Light List
 *
 */

RtVoid GMANLightList::on (RtLightHandle h)
{
  std::list<RtLightHandle>::iterator first=ll.begin();
  std::list<RtLightHandle>::iterator last=ll.end();
  for (;first!=last;first++) {
    if (*first==h) return;
  }
  ll.push_back(h);
}

RtVoid GMANLightList::off (RtLightHandle h)
{
  std::list<RtLightHandle>::iterator first=ll.begin();
  std::list<RtLightHandle>::iterator last=ll.end();
  for (;first!=last;first++) {
    if (*first==h) {
      ll.erase(first);
      return;
    }
  }
}
