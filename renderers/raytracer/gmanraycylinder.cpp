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
#include "gmanraybboxbuilder.h"
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
  RtFloat const r = (RtFloat)std::fabs(radius);
  GMANPoint const objMin(-r, -r, GMANMin(zmin, zmax));
  GMANPoint const objMax(r, r, GMANMax(zmin, zmax));
  bbox = gman::cameraSpaceBBox(objectToCamera, objMin, objMax);
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

  // x^2 + y^2 == radius^2, independent of z. a vanishes only when the ray's
  // object-space direction has no x or y component at all -- straight down
  // the axis -- and in exact arithmetic that forces b to vanish too, both
  // being built from the same dx, dy: a ray parallel to the axis either
  // lies outside the wall for its whole length or runs along it, neither a
  // transverse hit. A dx or dy whose square underflows leaves a exactly 0
  // while b, scaled by the ray's origin rather than squared, survives as a
  // tiny nonzero value; gman::solveRayQuadratic's linear root is then
  // rejected downstream, by tmin when the origin sits on the wall (a small
  // or negative root) or by the z-band when it does not (a large root) --
  // never by tmax.
  RtFloat const a = objDirection.getX() * objDirection.getX() + objDirection.getY() * objDirection.getY();
  RtFloat const b = 2 * (objDirection.getX() * objOrigin.getX() + objDirection.getY() * objOrigin.getY());
  RtFloat const c = objOrigin.getX() * objOrigin.getX() + objOrigin.getY() * objOrigin.getY() - radius * radius;

  RtFloat t0 = 0.0, t1 = 0.0;
  int const numRoots = gman::solveRayQuadratic(a, b, c, t0, t1);
  if (numRoots == 0)
    return false;

  RtFloat const roots[2] = {t0, t1};
  RtFloat const thetamaxRad = (RtFloat)(thetamax / 360.0 * 2.0 * PI);

  for (int i = 0; i < numRoots; ++i) {
    RtFloat const t = roots[i];
    if (t < ray.getTMin() || t > ray.getTMax())
      continue;

    GMANPoint const objPoint(objOrigin.getX() + objDirection.getX() * t, objOrigin.getY() + objDirection.getY() * t,
                             objOrigin.getZ() + objDirection.getZ() * t);
    RtFloat const z = objPoint.getZ();
    if (z < zmin || z > zmax)
      continue;

    RtFloat const theta = GMANMod(GMANAtan(objPoint.getY(), objPoint.getX()), (RtFloat)(2.0 * PI));
    if (theta > thetamaxRad)
      continue;

    // The wall's implicit gradient (2x, 2y, 0) points radially outward,
    // matching GMANCylinder::getNormal's (cos theta, sin theta, 0)
    // exactly: both are the same outward radial direction.
    GMANVector const objNormal(objPoint.getX(), objPoint.getY(), 0.0);
    GMANVector normal = gman::transformNormal(cameraToObject, objNormal);
    normal.normalize();

    hit.t = t;
    hit.point = ray.pointAt(t);
    hit.normal = normal;
    hit.u = theta / thetamaxRad;
    hit.v = (z - zmin) / (zmax - zmin);
    hit.primitive = this;
    return true;
  }

  return false;
}
