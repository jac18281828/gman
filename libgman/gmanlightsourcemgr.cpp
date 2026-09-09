/* SPDX-License-Identifier: LGPL-2.1-or-later */

/* This is part of GMAN, a RenderMan-compatible renderer.
 *
 * Copyright (c) 2001, 2000, 1999  John Cairns 
 *
 * Author: John Cairns <john@2ad.com>
 */

// Added Light List LJL 2000/08/08

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

#include <cmath>
#include <cstdint>

/* Local Headers */
#include "ri.h"      /* RenderMan Interface */
#include "gmanlog.h"
#include "gmanlightsourcemgr.h" /* Declaration Header */
#include "gmanslapi.h"

namespace {

// Direction from p toward a light at position (p -> position, in l) and
// the squared distance between them; shared by point and spotlight, whose
// falloff both start from inverse-square on that distance.
RtFloat pointToLight(const GMANPoint &position, const GMANPoint &p,
		      GMANVector &l) {
  l = GMANVector(position, p);
  l = GMANVector(-l.getX(), -l.getY(), -l.getZ());  // p -> position
  return l.getX()*l.getX() + l.getY()*l.getY() + l.getZ()*l.getZ();
}

// cl scaled by a scalar falloff, applied uniformly across channels.
GMANColor scaledColor(const GMANColor &cl, RtFloat falloff) {
  return GMANColor(cl.getRed() * falloff, cl.getGreen() * falloff,
		    cl.getBlue() * falloff);
}

} // namespace

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
    RtFloat dist2 = pointToLight(position, p, l);
    RtFloat falloff = (dist2 > RI_EPSILON) ? (1.0 / dist2) : 1.0;
    lightCl = scaledColor(cl, falloff);
    break;
  }

  case GMAN_LIGHT_SPOT: {
    RtFloat dist2 = pointToLight(position, p, l);
    RtFloat dist = std::sqrt(dist2);

    // Cosine of the angle between the cone's axis and the light -> p
    // direction; pow() below needs a nonnegative base to stay defined
    // past 90 degrees for a non-integer beamDistribution.
    RtFloat cosAngle = (dist > RI_EPSILON)
      ? -(l.getX()*direction.getX() + l.getY()*direction.getY() +
	  l.getZ()*direction.getZ()) / dist
      : (RtFloat) 1.0;
    RtFloat axisCos = (cosAngle > 0.0) ? cosAngle : (RtFloat) 0.0;

    RtFloat atten = std::pow(axisCos, beamDistribution);
    atten *= GMANSmoothStep(std::cos(coneAngle),
			     std::cos(coneAngle - coneDeltaAngle), cosAngle);

    RtFloat falloff = (dist2 > RI_EPSILON) ? (atten / dist2) : atten;
    lightCl = scaledColor(cl, falloff);
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
