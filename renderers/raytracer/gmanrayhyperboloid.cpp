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

  if (singular)
    return;

  // r(v)^2 (GMANRayHyperboloid::intersect's own p0sq + 2*pDot*v + dSq*v^2)
  // is convex in v (dSq >= 0), so its max over the swept segment [0, 1]
  // falls at an endpoint: the greater of the two control points' own
  // radii bounds x and y for the full revolution. z takes the segment's
  // own z extent exactly, the parameter that directly clips the shape.
  RtFloat const r0 = (RtFloat)std::sqrt(
      (double)(this->point1.getX() * this->point1.getX() + this->point1.getY() * this->point1.getY()));
  RtFloat const r1 = (RtFloat)std::sqrt(
      (double)(this->point2.getX() * this->point2.getX() + this->point2.getY() * this->point2.getY()));
  RtFloat const r = GMANMax(r0, r1);
  GMANPoint const objMin(-r, -r, GMANMin(this->point1.getZ(), this->point2.getZ()));
  GMANPoint const objMax(r, r, GMANMax(this->point1.getZ(), this->point2.getZ()));
  bbox = gman::cameraSpaceBBox(objectToCamera, objMin, objMax);
}

bool GMANRayHyperboloid::intersect(const GMANRay& ray, GMANHit& hit) const {
  if (singular)
    return false;

  // A null wedge leaves no surface to hit, the sphere's own guard; it
  // covers both branches below.
  if (thetamax == 0.0)
    return false;

  GMANPoint const objOrigin = gman::transformPoint(cameraToObject, ray.getOrigin());
  GMANVector const objDirection = gman::transformDirection(cameraToObject, ray.getDirection());

  // GMANHyperboloid::getLocation sweeps the segment point1..point2 by v,
  // r(v) == dist(P(v), axis), phi(v) == atan2(P(v).y, P(v).x); r(v)^2 is
  // quadratic in v (P0sq + 2*Pdot*v + Dsq*v^2, expanding
  // P(v) == point1 + v*(point2 - point1)).
  RtFloat const dxSeg = point2.getX() - point1.getX();
  RtFloat const dySeg = point2.getY() - point1.getY();
  RtFloat const dzSeg = point2.getZ() - point1.getZ();

  RtFloat const p0sq = point1.getX() * point1.getX() + point1.getY() * point1.getY();
  RtFloat const pDot = point1.getX() * dxSeg + point1.getY() * dySeg;
  RtFloat const dSq = dxSeg * dxSeg + dySeg * dySeg;

  RtFloat const thetamaxRad = (RtFloat)(thetamax / 360.0 * 2.0 * PI);

  // A segment with point1.z == point2.z sweeps a flat annulus: z is
  // constant across it, so it carries no information about v, unlike the
  // spanning branch below, which divides by dzSeg. v instead comes from
  // the plane hit's own radius, and the wedge is a spiral sector since
  // phi(v) varies with v.
  if (dzSeg == 0.0) {
    RtFloat const t = (point1.getZ() - objOrigin.getZ()) / objDirection.getZ();
    if (t < ray.getTMin() || t > ray.getTMax())
      return false;

    RtFloat const x = objOrigin.getX() + objDirection.getX() * t;
    RtFloat const y = objOrigin.getY() + objDirection.getY() * t;

    // r(v)^2 == x*x + y*y at the plane hit: dSq*v^2 + 2*pDot*v +
    // (p0sq - x*x - y*y) == 0. point1 == point2 forces dSq == pDot == 0.0,
    // so GMANQuadraticRoots' own a == 0.0 contract already returns zero
    // roots for that degeneracy.
    RtFloat vRoot0 = 0.0, vRoot1 = 0.0;
    int const numVRoots = GMANQuadraticRoots(dSq, 2 * pDot, p0sq - x * x - y * y, vRoot0, vRoot1);

    RtFloat const vRoots[2] = {vRoot0, vRoot1};
    for (int i = 0; i < numVRoots; ++i) {
      RtFloat const v = vRoots[i];
      // Rejects NaN, the only place a NaN from a degenerate ray is caught: NaN fails every comparison.
      if (!(v >= 0.0 && v <= 1.0))
        continue;

      // theta is measured from the segment's own azimuth phi(v) at this v,
      // which moves with v here (a spiral sector), not from the x-axis.
      RtFloat const xv = point1.getX() + v * dxSeg;
      RtFloat const yv = point1.getY() + v * dySeg;
      RtFloat const phi = GMANAtan(yv, xv);
      RtFloat const pointPhi = GMANAtan(y, x);
      RtFloat const theta = GMANMod(pointPhi - phi, (RtFloat)(2.0 * PI));
      if (theta > thetamaxRad)
        continue;

      // The fold, pDot + dSq*v == 0, is measure-zero: the formula and getNormal both vanish there in exact arithmetic.
      GMANVector objNormal(0.0, 0.0, -(pDot + dSq * v));
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

  // z spans the segment here, so v == (z - point1.z) / dzSeg is affine in
  // z and so in t; x(t)^2 + y(t)^2 - r(v(t))^2 is then an ordinary
  // quadratic in t.
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
