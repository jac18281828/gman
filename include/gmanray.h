/* SPDX-License-Identifier: LGPL-2.1-or-later */

/* This is part of GMAN, a RenderMan-compatible renderer.
 *
 * Copyright (c) 2026 John Cairns <john@2ad.com>
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

#ifndef __GMAN_GMANRAY_H
#define __GMAN_GMANRAY_H 1

#include "gmanpoint.h"
#include "gmanvector.h"
#include "ri.h"

class GMANPrimitive;

/*
 * RenderMan API GMANRay
 *
 * An origin, a direction and a [tmin, tmax] interval along it. The
 * constructor always normalizes the direction, so every GMANRay's t is a
 * distance and there is no unnormalized path to take: the sphere
 * quadratic's leading coefficient is 1, and an RI_EPSILON self-hit
 * rejection means the same thing at every pixel. An object-space ray
 * whose t must match the world-space one needs its own construction path
 * that scales t against the transform used, rather than paying to
 * re-normalize a direction that already lost the scale that produced it.
 */
class GMAN_EXPORT GMANRay {
  GMANPoint origin;
  GMANVector direction;
  RtFloat tmin;
  RtFloat tmax;

public:
  GMANRay(const GMANPoint& o, const GMANVector& d, RtFloat tMin = RI_EPSILON, RtFloat tMax = RI_INFINITY)
      : origin(o), direction(d), tmin(tMin), tmax(tMax) {
    // GMANVector::normalize leaves a vector below RI_EPSILON in
    // magnitude unchanged, so a zero-length direction survives
    // construction silently rather than being rejected.
    direction.normalize();
  }

  const GMANPoint& getOrigin(RtVoid) const { return origin; }
  const GMANVector& getDirection(RtVoid) const { return direction; }
  RtFloat getTMin(RtVoid) const { return tmin; }
  RtFloat getTMax(RtVoid) const { return tmax; }

  // The point t units along the ray from its origin.
  GMANPoint pointAt(RtFloat t) const {
    return GMANPoint(origin.getX() + direction.getX() * t, origin.getY() + direction.getY() * t,
                     origin.getZ() + direction.getZ() * t);
  }
};

/*
 * RenderMan API GMANHit
 *
 * What an intersect fills in on a hit: the distance, the hit point, the
 * surface normal and parameters, and the primitive hit. intersect
 * returns bool and writes this record only on a hit, so a
 * default-constructed GMANHit -- primitive null -- is a miss.
 */
struct GMAN_EXPORT GMANHit {
  RtFloat t = 0.0;
  GMANPoint point;
  GMANVector normal;
  RtFloat u = 0.0;
  RtFloat v = 0.0;
  // Null means no primitive was hit. The primitive outlives any hit
  // against it, so this pointer is non-owning.
  GMANPrimitive const* primitive = nullptr;
};

#endif
