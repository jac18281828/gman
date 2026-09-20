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
#include "gmanrayhyperboloid.h"
#include "gmanrayquadratic.h"
#include "gmanvector.h"

GMANRayHyperboloid::GMANRayHyperboloid(RtPoint point1, RtPoint point2, RtFloat thetamax, GMANParameterList pl,
                                       GMANTransform const& transform)
    : GMANHyperboloid(point1, point2, thetamax, pl), objectToCamera(transform.interpolate(0.0)),
      cameraToObject(objectToCamera) {
  try {
    cameraToObject.invert();
  } catch (GMANError const&) {
    singular = true;
  }
}

bool GMANRayHyperboloid::intersect(const GMANRay& ray, GMANHit& hit) const {
  if (singular)
    return false;

  // A null wedge leaves no surface to hit, the sphere's own guard. A
  // segment with point1.z == point2.z is a flat annulus this unit defers
  // (R5b's own prompt): v is no longer recoverable from z alone, its
  // radial extent is [min r(v), max r(v)] rather than the endpoint radii,
  // and its wedge is a spiral sector since phi(v) varies with v -- a
  // second intersector's worth of work for a shape no fixture in this
  // tree uses.
  if (thetamax == 0.0 || point1.getZ() == point2.getZ())
    return false;

  GMANPoint const objOrigin = gman::transformPoint(cameraToObject, ray.getOrigin());
  GMANVector const objDirection = gman::transformDirection(cameraToObject, ray.getDirection());

  // GMANHyperboloid::getLocation sweeps the segment point1..point2 by v,
  // r(v) == dist(P(v), axis), phi(v) == atan2(P(v).y, P(v).x). z spans the
  // segment, so v == (z - point1.z) / dzSeg is affine in z and so in t;
  // r(v)^2 is quadratic in v (P0sq + 2*Pdot*v + Dsq*v^2, expanding
  // P(v) == point1 + v*(point2 - point1)), so x(t)^2 + y(t)^2 - r(v(t))^2
  // is an ordinary quadratic in t.
  RtFloat const dxSeg = point2.getX() - point1.getX();
  RtFloat const dySeg = point2.getY() - point1.getY();
  RtFloat const dzSeg = point2.getZ() - point1.getZ();

  RtFloat const p0sq = point1.getX() * point1.getX() + point1.getY() * point1.getY();
  RtFloat const pDot = point1.getX() * dxSeg + point1.getY() * dySeg;
  RtFloat const dSq = dxSeg * dxSeg + dySeg * dySeg;

  RtFloat const v0 = (objOrigin.getZ() - point1.getZ()) / dzSeg;
  RtFloat const vSlope = objDirection.getZ() / dzSeg;

  RtFloat const r2c = p0sq + 2 * pDot * v0 + dSq * v0 * v0;
  RtFloat const r2b = 2 * vSlope * (pDot + dSq * v0);
  RtFloat const r2a = dSq * vSlope * vSlope;

  RtFloat const a = objDirection.getX() * objDirection.getX() + objDirection.getY() * objDirection.getY() - r2a;
  RtFloat const b = 2 * (objDirection.getX() * objOrigin.getX() + objDirection.getY() * objOrigin.getY()) - r2b;
  RtFloat const c = objOrigin.getX() * objOrigin.getX() + objOrigin.getY() * objOrigin.getY() - r2c;

  // a vanishes for a ray parallel to a ruling of the hyperboloid.
  // gman::solveRayQuadratic's own comment covers the linear case and why a
  // near-degenerate a never reaches it.
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
    RtFloat const v = (objPoint.getZ() - point1.getZ()) / dzSeg;
    if (v < 0.0 || v > 1.0)
      continue;

    // theta is measured from the segment's own azimuth phi(v) at this v,
    // not from the x-axis: GMANHyperboloid::getLocation adds theta to
    // phi(v), so recovering it any other way would not round trip.
    RtFloat const xv = point1.getX() + v * dxSeg;
    RtFloat const yv = point1.getY() + v * dySeg;
    RtFloat const phi = GMANAtan(yv, xv);
    RtFloat const pointPhi = GMANAtan(objPoint.getY(), objPoint.getX());
    RtFloat const theta = GMANMod(pointPhi - phi, (RtFloat)(2.0 * PI));
    if (theta > thetamaxRad)
      continue;

    // GMANHyperboloid::getNormal's cross product dPdu x dPdv reduces to
    // (kt*dzSeg*x, kt*dzSeg*y, -kt*(Pdot + Dsq*v)), kt == thetamaxRad > 0:
    // dropping the common positive factor kt leaves the vector below,
    // which also carries the sign reversal getNormal's own comment
    // documents for a segment descending in z (dzSeg < 0).
    GMANVector objNormal(dzSeg * objPoint.getX(), dzSeg * objPoint.getY(), -(pDot + dSq * v));
    GMANVector normal = gman::transformNormal(cameraToObject, objNormal);
    normal.normalize();

    hit.t = t;
    hit.point = ray.pointAt(t);
    hit.normal = normal;
    hit.u = theta / thetamaxRad;
    hit.v = v;
    hit.primitive = this;
    return true;
  }

  return false;
}
