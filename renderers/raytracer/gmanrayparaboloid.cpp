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

#include "gmanerror.h"
#include "gmanmath.h"
#include "gmanrayparaboloid.h"
#include "gmanrayquadratic.h"
#include "gmanvector.h"

GMANRayParaboloid::GMANRayParaboloid(RtFloat rmax, RtFloat zmin, RtFloat zmax, RtFloat thetamax, GMANParameterList pl,
                                     GMANTransform const& transform)
    : GMANParaboloid(rmax, zmin, zmax, thetamax, pl), objectToCamera(transform.interpolate(0.0)),
      cameraToObject(objectToCamera) {
  try {
    cameraToObject.invert();
  } catch (GMANError const&) {
    singular = true;
  }
}

bool GMANRayParaboloid::intersect(const GMANRay& ray, GMANHit& hit) const {
  if (singular)
    return false;

  // A null wedge, a null z-band, a zero rmax or a zero zmax leaves no
  // surface to hit, the sphere's own guard against a NaN-filled hit: zmax
  // divides into k below. Its open question about negative thetamax and
  // negative radius stands unchanged here.
  if (thetamax == 0.0 || rmax == 0.0 || zmax == 0.0 || zmin == zmax)
    return false;

  GMANPoint const objOrigin = gman::transformPoint(cameraToObject, ray.getOrigin());
  GMANVector const objDirection = gman::transformDirection(cameraToObject, ray.getDirection());

  // GMANParaboloid::getLocation's radial factor is rmax*sqrt(v/zmax); the
  // settled reading (see R5b's own prompt) substitutes v == (z - zmin) /
  // (zmax - zmin) to give x^2 + y^2 == k*(z - zmin), k == rmax^2 /
  // (zmax*(zmax - zmin)) -- quadratic in the ray parameter t.
  RtFloat const k = rmax * rmax / (zmax * (zmax - zmin));

  RtFloat const a = objDirection.getX() * objDirection.getX() + objDirection.getY() * objDirection.getY();
  RtFloat const b =
      2 * (objDirection.getX() * objOrigin.getX() + objDirection.getY() * objOrigin.getY()) - k * objDirection.getZ();
  RtFloat const c =
      objOrigin.getX() * objOrigin.getX() + objOrigin.getY() * objOrigin.getY() - k * (objOrigin.getZ() - zmin);

  // a vanishes for a ray parallel to the axis (dx == dy == 0): the
  // apex-seeking case GMANQuadraticRoots' own comment does not cover.
  // gman::solveRayQuadratic solves that linear case directly.
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

    // The implicit surface's gradient (2x, 2y, -k) points along
    // GMANParaboloid::getNormal's own (cos theta, sin theta,
    // -fPrime/(zmax-zmin)) direction: substituting fPrime ==
    // rmax/(2*sqrt(zmax*v)) and r == rmax*sqrt(v/zmax) gives
    // -fPrime/(zmax-zmin) == -k/(2*r), the same ratio to (x, y) == r*(cos
    // theta, sin theta) that (2x, 2y, -k) carries -- so no division by r
    // is needed, and the apex (r == 0) is not a special case here.
    GMANVector objNormal(2 * objPoint.getX(), 2 * objPoint.getY(), -k);
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
