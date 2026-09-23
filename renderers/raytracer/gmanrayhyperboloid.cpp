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
  bbox = gman::revolutionBBox(objectToCamera, r, this->point1.getZ(), this->point2.getZ());
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
  // P(v) == point1 + v*(point2 - point1)). Both branches below read these
  // in double: a merely tiny dxSeg or dySeg would otherwise underflow
  // dSq's float square to 0 while pDot survives, and the flat branch's
  // linear-case fallback would return a spurious root.
  double const dxSeg = (double)point2.getX() - (double)point1.getX();
  double const dySeg = (double)point2.getY() - (double)point1.getY();
  double const dzSeg = (double)point2.getZ() - (double)point1.getZ();

  double const p0sq = (double)point1.getX() * point1.getX() + (double)point1.getY() * point1.getY();
  double const pDot = (double)point1.getX() * dxSeg + (double)point1.getY() * dySeg;
  double const dSq = dxSeg * dxSeg + dySeg * dySeg;

  RtFloat const thetamaxRad = (RtFloat)(thetamax / 360.0 * 2.0 * PI);

  // A segment with point1.z == point2.z sweeps a flat annulus: z is
  // constant across it, so it carries no information about v, unlike the
  // spanning branch below, which divides by dzSeg. v instead comes from
  // the plane hit's own radius, and the wedge is a spiral sector since
  // phi(v) varies with v.
  if (dzSeg == 0.0) {
    double const dzPlane = objDirection.getZ();
    // A plane solve has no discriminant to cancel, so it runs in double
    // from the unshifted origin: no bounding-sphere shift applies here.
    double const tLocal = ((double)point1.getZ() - (double)objOrigin.getZ()) / dzPlane;
    RtFloat const t = (RtFloat)tLocal;
    if (t < ray.getTMin() || t > ray.getTMax())
      return false;

    double const x = (double)objOrigin.getX() + (double)objDirection.getX() * tLocal;
    double const y = (double)objOrigin.getY() + (double)objDirection.getY() * tLocal;

    // r(v)^2 == x*x + y*y at the plane hit: dSq*v^2 + 2*pDot*v +
    // (p0sq - x*x - y*y) == 0, solved in double. point1 == point2 forces
    // dSq == pDot == 0.0, so gman::solveRayQuadratic's own a == 0.0
    // contract already returns zero roots for that degeneracy.
    double vRoot0 = 0.0, vRoot1 = 0.0;
    int const numVRoots = gman::solveRayQuadratic(dSq, 2.0 * pDot, p0sq - x * x - y * y, vRoot0, vRoot1);

    double const vRoots[2] = {vRoot0, vRoot1};
    for (int i = 0; i < numVRoots; ++i) {
      double const v = vRoots[i];
      // Rejects NaN, the only place a NaN from a degenerate ray is caught: NaN fails every comparison.
      if (!(v >= 0.0 && v <= 1.0))
        continue;

      // theta is measured from the segment's own azimuth phi(v) at this v,
      // which moves with v here (a spiral sector), not from the x-axis.
      RtFloat const xv = (RtFloat)(point1.getX() + v * dxSeg);
      RtFloat const yv = (RtFloat)(point1.getY() + v * dySeg);
      RtFloat const phi = GMANAtan(yv, xv);
      RtFloat const pointPhi = GMANAtan((RtFloat)y, (RtFloat)x);
      RtFloat const theta = GMANMod(pointPhi - phi, (RtFloat)(2.0 * PI));
      if (theta > thetamaxRad)
        continue;

      // The fold, pDot + dSq*v == 0, is measure-zero: the formula and getNormal both vanish there in exact arithmetic.
      GMANVector objNormal(0.0, 0.0, (RtFloat)(-(pDot + dSq * v)));
      GMANVector normal = gman::transformNormal(cameraToObject, objNormal);
      normal.normalize();

      GMANPoint const objPoint((RtFloat)x, (RtFloat)y, point1.getZ());

      hit.t = t;
      hit.point = gman::transformPoint(objectToCamera, objPoint);
      hit.normal = normal;
      hit.u = theta / thetamaxRad;
      hit.v = (RtFloat)v;
      hit.primitive = this;
      return true;
    }

    return false;
  }

  // z spans the segment here, so v == (z - point1.z) / dzSeg is affine in
  // z and so in t; x(t)^2 + y(t)^2 - r(v(t))^2 is then an ordinary
  // quadratic in t.

  // Every surface point satisfies x^2+y^2+z^2 <= r^2 + max(point1.z^2,
  // point2.z^2), r the greater of the two control points' own radii --
  // gmanraybbox.h's revolutionBBox's own r, z0, z1 (the constructor's own
  // bbox uses this same r).
  double const r0 = std::sqrt((double)point1.getX() * point1.getX() + (double)point1.getY() * point1.getY());
  double const r1 = std::sqrt((double)point2.getX() * point2.getX() + (double)point2.getY() * point2.getY());
  double const boundingRadius =
      std::sqrt(GMANMax(r0, r1) * GMANMax(r0, r1) +
                GMANMax((double)point1.getZ() * point1.getZ(), (double)point2.getZ() * point2.getZ()));

  gman::ShiftedRay shifted;
  if (!gman::shiftIntoBoundingSphere(objOrigin, objDirection, boundingRadius, shifted))
    return false;

  double const v0 = (shifted.oz - (double)point1.getZ()) / dzSeg;
  double const vSlope = shifted.dz / dzSeg;

  double const r2c = p0sq + 2.0 * pDot * v0 + dSq * v0 * v0;
  double const r2b = 2.0 * vSlope * (pDot + dSq * v0);
  double const r2a = dSq * vSlope * vSlope;

  double const a = shifted.dx * shifted.dx + shifted.dy * shifted.dy - r2a;
  double const b = 2.0 * (shifted.dx * shifted.ox + shifted.dy * shifted.oy) - r2b;
  double const c = shifted.ox * shifted.ox + shifted.oy * shifted.oy - r2c;

  // a vanishes for a ray parallel to a ruling of the hyperboloid.
  // gman::solveRayQuadratic's own comment covers the linear case and why a
  // near-degenerate a never reaches it.
  double t0 = 0.0, t1 = 0.0;
  int const numRoots = gman::solveRayQuadratic(a, b, c, t0, t1);
  if (numRoots == 0)
    return false;

  double const roots[2] = {t0, t1};

  for (int i = 0; i < numRoots; ++i) {
    double const tLocal = roots[i];
    gman::ShiftedRootPoint const root = gman::shiftedRootPoint(shifted, tLocal, objectToCamera);
    if (root.t < ray.getTMin() || root.t > ray.getTMax())
      continue;

    double const v = (root.pz - (double)point1.getZ()) / dzSeg;
    if (v < 0.0 || v > 1.0)
      continue;

    // theta is measured from the segment's own azimuth phi(v) at this v,
    // not from the x-axis: GMANHyperboloid::getLocation adds theta to
    // phi(v), so recovering it any other way would not round trip.
    RtFloat const xv = (RtFloat)(point1.getX() + v * dxSeg);
    RtFloat const yv = (RtFloat)(point1.getY() + v * dySeg);
    RtFloat const phi = GMANAtan(yv, xv);
    RtFloat const pointPhi = GMANAtan((RtFloat)root.py, (RtFloat)root.px);
    RtFloat const theta = GMANMod(pointPhi - phi, (RtFloat)(2.0 * PI));
    if (theta > thetamaxRad)
      continue;

    // GMANHyperboloid::getNormal's cross product dPdu x dPdv reduces to
    // (kt*dzSeg*x, kt*dzSeg*y, -kt*(Pdot + Dsq*v)), kt == thetamaxRad > 0:
    // dropping the common positive factor kt leaves the vector below,
    // which also carries the sign reversal getNormal's own comment
    // documents for a segment descending in z (dzSeg < 0).
    GMANVector objNormal((RtFloat)(dzSeg * root.px), (RtFloat)(dzSeg * root.py), (RtFloat)(-(pDot + dSq * v)));
    GMANVector normal = gman::transformNormal(cameraToObject, objNormal);
    normal.normalize();

    hit.t = root.t;
    hit.point = root.point;
    hit.normal = normal;
    hit.u = theta / thetamaxRad;
    hit.v = (RtFloat)v;
    hit.primitive = this;
    return true;
  }

  return false;
}
