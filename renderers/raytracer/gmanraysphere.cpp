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

#include "gmanraysphere.h"
#include "gmanvector.h"

bool GMANRaySphere::intersect(const GMANRay& ray, GMANHit& /*hit*/) const {
  const GMANVector& direction = ray.getDirection();
  GMANPoint centre(0, 0, 0);
  RtFloat coef[3];
  int numRoots;
  RtFloat roots[2];

  GMANVector deltaP(ray.getOrigin(), centre);

  coef[0] = direction.dot(direction);
  coef[1] = 2 * direction.dot(deltaP);
  coef[2] = deltaP.dot(deltaP) - radius * radius;

  // TODO(phase-1): the root solver is not written, so this always misses.
  numRoots = 0;
  //   QuadraticRoots(coef, numRoots, roots);
  if (numRoots == 0)
    return false;

  return false;
}
