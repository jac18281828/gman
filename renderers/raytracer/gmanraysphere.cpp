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

#include "gmanmath.h"
#include "gmanraysphere.h"
#include "gmanvector.h"

bool GMANRaySphere::intersect(const GMANRay& ray, GMANHit& hit) const {
  const GMANVector& direction = ray.getDirection();
  GMANPoint centre(0, 0, 0);
  GMANVector deltaP(centre, ray.getOrigin()); // O - C

  RtFloat a = direction.dot(direction);
  RtFloat b = 2 * direction.dot(deltaP);
  RtFloat c = deltaP.dot(deltaP) - radius * radius;

  RtFloat t0 = 0.0, t1 = 0.0;
  int numRoots = GMANQuadraticRoots(a, b, c, t0, t1);
  if (numRoots == 0)
    return false;

  RtFloat roots[2] = {t0, t1};
  RtFloat phimin = (RtFloat)asin(GMANClamp<double>(zmin / radius, -1.0, 1.0));
  RtFloat phimax = (RtFloat)asin(GMANClamp<double>(zmax / radius, -1.0, 1.0));
  RtFloat thetamaxRad = (RtFloat)(thetamax / 360.0 * 2.0 * PI);

  // Test the near root first and fall through to the far one rather than
  // choosing a root and then validating it: a root can fail on the ray's
  // own interval, on zmin/zmax, or on thetamax, and a ray from inside the
  // sphere or one grazing a clipped cap can still hit on its far root.
  for (int i = 0; i < numRoots; ++i) {
    RtFloat t = roots[i];
    if (t < ray.getTMin() || t > ray.getTMax())
      continue;

    GMANPoint point = ray.pointAt(t);
    RtFloat z = point.getZ();
    if (z < zmin || z > zmax)
      continue;

    RtFloat theta = GMANMod(GMANAtan(point.getY(), point.getX()), (RtFloat)(2.0 * PI));
    if (theta > thetamaxRad)
      continue;

    RtFloat phi = (RtFloat)asin(GMANClamp<double>(z / radius, -1.0, 1.0));

    hit.t = t;
    hit.point = point;
    GMANVector normal(centre, point); // outward radial direction
    normal.normalize();
    hit.normal = normal;
    hit.u = theta / thetamaxRad;
    hit.v = (phi - phimin) / (phimax - phimin);
    hit.primitive = this;
    return true;
  }

  return false;
}
