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

#include "gmanerror.h"
#include "gmanmath.h"
#include "gmanraysphere.h"
#include "gmanvector.h"

namespace {

// The same row-vector product GMANMatrix4::p3m computes, through the
// const operator[] a library caller can use, since p3m itself is not
// const-qualified.
GMANPoint transformPoint(GMANMatrix4 const& m, GMANPoint const& p) {
  RtFloat const x = p.getX(), y = p.getY(), z = p.getZ();
  RtFloat const rx = x * m[0][0] + y * m[1][0] + z * m[2][0] + m[3][0];
  RtFloat const ry = x * m[0][1] + y * m[1][1] + z * m[2][1] + m[3][1];
  RtFloat const rz = x * m[0][2] + y * m[1][2] + z * m[2][2] + m[3][2];
  RtFloat const rw = x * m[0][3] + y * m[1][3] + z * m[2][3] + m[3][3];
  if (rw != 1.0 && rw != 0.0)
    return GMANPoint(rx / rw, ry / rw, rz / rw);
  return GMANPoint(rx, ry, rz);
}

// A direction's homogeneous w is 0, so the translation row (row 3) drops
// out and no perspective divide applies.
GMANVector transformDirection(GMANMatrix4 const& m, GMANVector const& v) {
  RtFloat const x = v.getX(), y = v.getY(), z = v.getZ();
  return GMANVector(x * m[0][0] + y * m[1][0] + z * m[2][0], x * m[0][1] + y * m[1][1] + z * m[2][1],
                    x * m[0][2] + y * m[1][2] + z * m[2][2]);
}

// A camera-space normal is objectToCamera's inverse transpose applied to
// the object-space one (AGENTS.md's RIB-authoring rule), not the plain
// product transformPoint and transformDirection compute. Those read m as
// v * M -- v against m's columns; a normal instead needs M * v -- m's own
// rows dotted against v. The two productions agree whenever m is
// symmetric, as a translation's or a scale's linear part is, and diverge
// under a rotation.
GMANVector transformNormal(GMANMatrix4 const& m, GMANVector const& v) {
  RtFloat const x = v.getX(), y = v.getY(), z = v.getZ();
  return GMANVector(m[0][0] * x + m[0][1] * y + m[0][2] * z, m[1][0] * x + m[1][1] * y + m[1][2] * z,
                    m[2][0] * x + m[2][1] * y + m[2][2] * z);
}

} // namespace

GMANRaySphere::GMANRaySphere(RtFloat radius, RtFloat zmin, RtFloat zmax, RtFloat tmax, GMANParameterList pl,
                             GMANTransform const& transform)
    : GMANSphere(radius, zmin, zmax, tmax, pl), objectToCamera(transform.interpolate(0.0)),
      cameraToObject(objectToCamera) {
  try {
    cameraToObject.invert();
  } catch (GMANError const&) {
    singular = true;
  }
}

bool GMANRaySphere::intersect(const GMANRay& ray, GMANHit& hit) const {
  if (singular)
    return false;

  // A null z-band, a null wedge or a zero radius leaves no surface to hit;
  // without this guard, phi's z/radius division and u's theta/thetamaxRad
  // division below fill the hit with NaN instead of reporting a miss.
  //
  // Open question: a negative thetamax hits nothing here, while
  // GMANSphere::getLocation sweeps a real clockwise wedge for one. Which
  // behavior is right is undecided.
  if (zmin == zmax || thetamax == 0.0 || radius == 0.0)
    return false;

  // Move the ray into the sphere's object space, centred at the origin,
  // through cameraToObject. The direction keeps the length the transform
  // gives it rather than the unit length a freshly built GMANRay would
  // normalize it to, so a root found against it is already a camera-space
  // distance and the ray's own [tmin, tmax] interval applies unchanged.
  GMANPoint const objOrigin = transformPoint(cameraToObject, ray.getOrigin());
  GMANVector const objDirection = transformDirection(cameraToObject, ray.getDirection());

  GMANPoint centre(0, 0, 0);
  GMANVector deltaP(centre, objOrigin); // O - C, in object space

  RtFloat a = objDirection.dot(objDirection);
  RtFloat b = 2 * objDirection.dot(deltaP);
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

    GMANPoint objPoint(objOrigin.getX() + objDirection.getX() * t, objOrigin.getY() + objDirection.getY() * t,
                       objOrigin.getZ() + objDirection.getZ() * t);
    RtFloat z = objPoint.getZ();
    if (z < zmin || z > zmax)
      continue;

    RtFloat theta = GMANMod(GMANAtan(objPoint.getY(), objPoint.getX()), (RtFloat)(2.0 * PI));
    if (theta > thetamaxRad)
      continue;

    RtFloat phi = (RtFloat)asin(GMANClamp<double>(z / radius, -1.0, 1.0));

    GMANVector objNormal(centre, objPoint); // outward radial direction, object space
    objNormal.normalize();

    GMANVector normal = transformNormal(cameraToObject, objNormal);
    normal.normalize();

    hit.t = t;
    hit.point = ray.pointAt(t);
    hit.normal = normal;
    hit.u = theta / thetamaxRad;
    hit.v = (phi - phimin) / (phimax - phimin);
    hit.primitive = this;
    return true;
  }

  return false;
}
