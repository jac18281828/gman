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

#include <cmath>

#include "gmanerror.h"
#include "gmanmath.h"
#include "gmanraybbox.h"
#include "gmanraycylinder.h"
#include "gmanrayquadratic.h"
#include "gmanvector.h"

GMANRayCylinder::GMANRayCylinder(RtFloat radius, RtFloat zmin, RtFloat zmax, RtFloat thetamax, GMANParameterList pl,
                                 GMANTransform const& transform)
    : GMANCylinder(radius, zmin, zmax, thetamax, pl), objectToCamera(transform.interpolate(0.0)),
      cameraToObject(objectToCamera) {
  try {
    cameraToObject.invert();
  } catch (GMANError const&) {
    singular = true;
  }

  if (singular)
    return;

  // The wall's radius is constant across z, so x and y bound in
  // +/-radius; z takes zmin/zmax exactly, the parameters that directly
  // clip the shape.
  bbox = gman::revolutionBBox(objectToCamera, radius, zmin, zmax);
}

bool GMANRayCylinder::intersect(const GMANRay& ray, GMANHit& hit) const {
  if (singular)
    return false;

  // A null z-band, a null wedge or a zero radius leaves no surface to hit,
  // the sphere's own guard against a NaN-filled hit. Its open question
  // about negative thetamax and negative radius stands unchanged here.
  if (zmin == zmax || thetamax == 0.0 || radius == 0.0)
    return false;

  GMANPoint const objOrigin = gman::transformPoint(cameraToObject, ray.getOrigin());
  GMANVector const objDirection = gman::transformDirection(cameraToObject, ray.getDirection());

  // Every surface point satisfies x^2+y^2+z^2 <= radius^2 + max(zmin^2,
  // zmax^2), gmanraybbox.h's revolutionBBox's own r, z0, z1 bound
  // (radius, zmin, zmax here).
  double const boundingRadius = std::sqrt((double)radius * radius + GMANMax((double)zmin * zmin, (double)zmax * zmax));

  gman::ShiftedRay shifted;
  if (!gman::shiftIntoBoundingSphere(objOrigin, objDirection, boundingRadius, shifted))
    return false;

  // x^2 + y^2 == radius^2, independent of z. a vanishes only when the
  // ray's object-space direction has no x or y component at all --
  // straight down the axis -- since dx and dy are promoted to double
  // before squaring: a merely tiny dx or dy no longer underflows a to
  // zero the way a float square once could. A ray parallel to the axis
  // either lies outside the wall for its whole length or runs along it,
  // neither a transverse hit.
  double const a = shifted.dx * shifted.dx + shifted.dy * shifted.dy;
  double const b = 2.0 * (shifted.dx * shifted.ox + shifted.dy * shifted.oy);
  double const c = shifted.ox * shifted.ox + shifted.oy * shifted.oy - (double)radius * (double)radius;

  double t0 = 0.0, t1 = 0.0;
  int const numRoots = gman::solveRayQuadratic(a, b, c, t0, t1);
  if (numRoots == 0)
    return false;

  double const roots[2] = {t0, t1};
  RtFloat const thetamaxRad = (RtFloat)(thetamax / 360.0 * 2.0 * PI);

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

    // The wall's implicit gradient (2x, 2y, 0) points radially outward,
    // matching GMANCylinder::getNormal's (cos theta, sin theta, 0)
    // exactly: both are the same outward radial direction.
    GMANVector const objNormal((RtFloat)root.px, (RtFloat)root.py, 0.0);
    GMANVector normal = gman::transformNormal(cameraToObject, objNormal);
    normal.normalize();

    hit.t = root.t;
    hit.point = root.point;
    hit.normal = normal;
    hit.u = theta / thetamaxRad;
    hit.v = (z - zmin) / (zmax - zmin);
    hit.primitive = this;
    return true;
  }

  return false;
}
