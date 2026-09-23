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

#include <cmath>

#include "gmanerror.h"
#include "gmanmath.h"
#include "gmanraybbox.h"
#include "gmanrayquadratic.h"
#include "gmanraysphere.h"
#include "gmanvector.h"

GMANRaySphere::GMANRaySphere(RtFloat radius, RtFloat zmin, RtFloat zmax, RtFloat tmax, GMANParameterList pl,
                             GMANTransform const& transform)
    : GMANSphere(radius, zmin, zmax, tmax, pl), objectToCamera(transform.interpolate(0.0)),
      cameraToObject(objectToCamera) {
  try {
    cameraToObject.invert();
  } catch (GMANError const&) {
    singular = true;
  }

  if (singular)
    return;

  // The full sphere's revolution bounds x and y in +/-radius regardless of
  // thetamax (conservative when the wedge is narrower); z takes zmin/zmax
  // exactly, the parameters that directly clip the shape.
  bbox = gman::revolutionBBox(objectToCamera, radius, zmin, zmax);
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
  // through cameraToObject.
  GMANPoint const objOrigin = gman::transformPoint(cameraToObject, ray.getOrigin());
  GMANVector const objDirection = gman::transformDirection(cameraToObject, ray.getDirection());

  // Every surface point satisfies x^2+y^2+z^2 <= radius^2 + max(zmin^2,
  // zmax^2), gmanraybbox.h's revolutionBBox's own r, z0, z1 bound
  // (radius, zmin, zmax here).
  double const boundingRadius = std::sqrt((double)radius * radius + GMANMax((double)zmin * zmin, (double)zmax * zmax));

  gman::ShiftedRay shifted;
  if (!gman::shiftIntoBoundingSphere(objOrigin, objDirection, boundingRadius, shifted))
    return false;

  // The shifted origin's own distance from the centre and the ray's
  // direction give the sphere quadratic directly: no separate deltaP is
  // needed once the centre sits at the object-space origin.
  double const a = shifted.dirSq;
  double const b = 2.0 * (shifted.ox * shifted.dx + shifted.oy * shifted.dy + shifted.oz * shifted.dz);
  double const c =
      shifted.ox * shifted.ox + shifted.oy * shifted.oy + shifted.oz * shifted.oz - (double)radius * (double)radius;

  double t0 = 0.0, t1 = 0.0;
  int const numRoots = gman::solveRayQuadratic(a, b, c, t0, t1);
  if (numRoots == 0)
    return false;

  double const roots[2] = {t0, t1};
  RtFloat const phimin = (RtFloat)asin(GMANClamp<double>(zmin / radius, -1.0, 1.0));
  RtFloat const phimax = (RtFloat)asin(GMANClamp<double>(zmax / radius, -1.0, 1.0));
  RtFloat const thetamaxRad = (RtFloat)(thetamax / 360.0 * 2.0 * PI);

  // Test the near root first and fall through to the far one rather than
  // choosing a root and then validating it: a root can fail on the ray's
  // own interval, on zmin/zmax, or on thetamax, and a ray from inside the
  // sphere or one grazing a clipped cap can still hit on its far root.
  for (int i = 0; i < numRoots; ++i) {
    double const tLocal = roots[i];
    gman::ShiftedRootPoint const root = gman::shiftedRootPoint(shifted, tLocal, objectToCamera);
    if (root.t < ray.getTMin() || root.t > ray.getTMax())
      continue;

    RtFloat const z = (RtFloat)root.pz;
    if (z < zmin || z > zmax)
      continue;

    RtFloat const theta = GMANMod(GMANAtan((RtFloat)root.py, (RtFloat)root.px), (RtFloat)(2.0 * PI));
    if (theta > thetamaxRad)
      continue;

    RtFloat const phi = (RtFloat)asin(GMANClamp<double>(root.pz / radius, -1.0, 1.0));

    // The sphere is centred at the object-space origin, so the outward
    // normal is the point itself, normalized: valid for either sign of
    // radius, unlike dividing by radius, which points inward when it is
    // negative.
    GMANVector objNormal((RtFloat)root.px, (RtFloat)root.py, (RtFloat)root.pz);
    objNormal.normalize();

    GMANVector normal = gman::transformNormal(cameraToObject, objNormal);
    normal.normalize();

    hit.t = root.t;
    hit.point = root.point;
    hit.normal = normal;
    hit.u = theta / thetamaxRad;
    hit.v = (phi - phimin) / (phimax - phimin);
    hit.primitive = this;
    return true;
  }

  return false;
}
