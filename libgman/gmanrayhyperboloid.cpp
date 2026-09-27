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

namespace {

// GMANHyperboloid::getLocation sweeps the segment point1..point2 by v,
// r(v) == dist(P(v), axis), phi(v) == atan2(P(v).y, P(v).x); r(v)^2 is
// quadratic in v (P0sq + 2*Pdot*v + Dsq*v^2, expanding
// P(v) == point1 + v*(point2 - point1)). annulusHit and spanningHit read
// these in double: a merely tiny dxSeg or dySeg would otherwise underflow
// dSq's float square to 0 while pDot survives, and annulusHit's
// linear-case fallback would return a spurious root.
struct Sweep {
  double p1x, p1y, p1z;
  double p2x, p2y, p2z;
  double dxSeg, dySeg, dzSeg;
  double p0sq, pDot, dSq;
  RtFloat thetamaxRad;
};

Sweep sweepOf(GMANPoint const& point1, GMANPoint const& point2, RtFloat thetamax) {
  Sweep sweep;
  sweep.p1x = (double)point1.getX();
  sweep.p1y = (double)point1.getY();
  sweep.p1z = (double)point1.getZ();
  sweep.p2x = (double)point2.getX();
  sweep.p2y = (double)point2.getY();
  sweep.p2z = (double)point2.getZ();

  sweep.dxSeg = sweep.p2x - sweep.p1x;
  sweep.dySeg = sweep.p2y - sweep.p1y;
  sweep.dzSeg = sweep.p2z - sweep.p1z;

  sweep.p0sq = sweep.p1x * sweep.p1x + sweep.p1y * sweep.p1y;
  sweep.pDot = sweep.p1x * sweep.dxSeg + sweep.p1y * sweep.dySeg;
  sweep.dSq = sweep.dxSeg * sweep.dxSeg + sweep.dySeg * sweep.dySeg;

  sweep.thetamaxRad = (RtFloat)(thetamax / 360.0 * 2.0 * PI);
  return sweep;
}

// theta is measured from the segment's own azimuth phi(v) at this v, not
// from the x-axis: GMANHyperboloid::getLocation adds theta to phi(v), so
// recovering it any other way would not round trip. In annulusHit, phi(v)
// itself moves with v, since a flat sweep traces a spiral sector.
RtFloat wedgeTheta(Sweep const& sweep, double v, double px, double py) {
  RtFloat const xv = (RtFloat)(sweep.p1x + v * sweep.dxSeg);
  RtFloat const yv = (RtFloat)(sweep.p1y + v * sweep.dySeg);
  RtFloat const phi = GMANAtan(yv, xv);
  RtFloat const pointPhi = GMANAtan((RtFloat)py, (RtFloat)px);
  return GMANMod(pointPhi - phi, (RtFloat)(2.0 * PI));
}

// Every surface point satisfies x^2+y^2+z^2 <= r^2 + max(point1.z^2,
// point2.z^2), r the greater of the two control points' own radii --
// gmanraybbox.h's revolutionBBox's own r, z0, z1 (the constructor's own
// bbox uses this same r).
double boundingRadius(Sweep const& sweep) {
  double const r0 = std::sqrt(sweep.p1x * sweep.p1x + sweep.p1y * sweep.p1y);
  double const r1 = std::sqrt(sweep.p2x * sweep.p2x + sweep.p2y * sweep.p2y);
  return std::sqrt(GMANMax(r0, r1) * GMANMax(r0, r1) + GMANMax(sweep.p1z * sweep.p1z, sweep.p2z * sweep.p2z));
}

void spanningCoefficients(Sweep const& sweep, gman::ShiftedRay const& shifted, double& a, double& b, double& c) {
  double const v0 = (shifted.oz - sweep.p1z) / sweep.dzSeg;
  double const vSlope = shifted.dz / sweep.dzSeg;

  double const r2c = sweep.p0sq + 2.0 * sweep.pDot * v0 + sweep.dSq * v0 * v0;
  double const r2b = 2.0 * vSlope * (sweep.pDot + sweep.dSq * v0);
  double const r2a = sweep.dSq * vSlope * vSlope;

  a = shifted.dx * shifted.dx + shifted.dy * shifted.dy - r2a;
  b = 2.0 * (shifted.dx * shifted.ox + shifted.dy * shifted.oy) - r2b;
  c = shifted.ox * shifted.ox + shifted.oy * shifted.oy - r2c;
}

struct SweepHit {
  RtFloat t;
  GMANPoint point;
  GMANVector objNormal;
  RtFloat theta;
  double v;
};

// A segment with point1.z == point2.z sweeps a flat annulus: z is
// constant across it, so it carries no information about v, unlike
// spanningHit below, which divides by dzSeg. v instead comes from the
// plane hit's own radius, and the wedge is a spiral sector since phi(v)
// varies with v.
bool annulusHit(Sweep const& sweep, GMANPoint const& objOrigin, GMANVector const& objDirection, GMANRay const& ray,
                GMANMatrix4 const& objectToCamera, SweepHit& found) {
  double const dzPlane = objDirection.getZ();
  // A plane solve has no discriminant to cancel, so it runs in double
  // from the unshifted origin: no bounding-sphere shift applies here.
  double const tLocal = (sweep.p1z - (double)objOrigin.getZ()) / dzPlane;
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
  int const numVRoots =
      gman::solveRayQuadratic(sweep.dSq, 2.0 * sweep.pDot, sweep.p0sq - x * x - y * y, vRoot0, vRoot1);

  double const vRoots[2] = {vRoot0, vRoot1};
  for (int i = 0; i < numVRoots; ++i) {
    double const v = vRoots[i];
    // Rejects NaN, the only place a NaN from a degenerate ray is caught: NaN fails every comparison.
    if (!(v >= 0.0 && v <= 1.0))
      continue;

    RtFloat const theta = wedgeTheta(sweep, v, x, y);
    if (theta > sweep.thetamaxRad)
      continue;

    // The fold, pDot + dSq*v == 0, is measure-zero: the formula and getNormal both vanish there in exact arithmetic.
    GMANVector const objNormal(0.0, 0.0, (RtFloat)(-(sweep.pDot + sweep.dSq * v)));

    GMANPoint const objPoint((RtFloat)x, (RtFloat)y, (RtFloat)sweep.p1z);

    found.t = t;
    found.point = gman::transformPoint(objectToCamera, objPoint);
    found.objNormal = objNormal;
    found.theta = theta;
    found.v = v;
    return true;
  }

  return false;
}

// z spans the segment here, so v == (z - point1.z) / dzSeg is affine in
// z and so in t; x(t)^2 + y(t)^2 - r(v(t))^2 is then an ordinary
// quadratic in t.
bool spanningHit(Sweep const& sweep, GMANPoint const& objOrigin, GMANVector const& objDirection, GMANRay const& ray,
                 GMANMatrix4 const& objectToCamera, SweepHit& found) {
  gman::ShiftedRay shifted;
  if (!gman::shiftIntoBoundingSphere(objOrigin, objDirection, boundingRadius(sweep), shifted))
    return false;

  double a = 0.0, b = 0.0, c = 0.0;
  spanningCoefficients(sweep, shifted, a, b, c);

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

    double const v = (root.pz - sweep.p1z) / sweep.dzSeg;
    if (v < 0.0 || v > 1.0)
      continue;

    RtFloat const theta = wedgeTheta(sweep, v, root.px, root.py);
    if (theta > sweep.thetamaxRad)
      continue;

    // GMANHyperboloid::getNormal's cross product dPdu x dPdv reduces to
    // (kt*dzSeg*x, kt*dzSeg*y, -kt*(Pdot + Dsq*v)), kt == thetamaxRad > 0:
    // dropping the common positive factor kt leaves the vector below,
    // which also carries the sign reversal getNormal's own comment
    // documents for a segment descending in z (dzSeg < 0).
    GMANVector const objNormal((RtFloat)(sweep.dzSeg * root.px), (RtFloat)(sweep.dzSeg * root.py),
                               (RtFloat)(-(sweep.pDot + sweep.dSq * v)));

    found.t = root.t;
    found.point = root.point;
    found.objNormal = objNormal;
    found.theta = theta;
    found.v = v;
    return true;
  }

  return false;
}

} // namespace

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

  // r(v)^2 (sweepOf's own p0sq + 2*pDot*v + dSq*v^2) is convex in v
  // (dSq >= 0), so its max over the swept segment [0, 1] falls at an
  // endpoint: the greater of the two control points' own radii bounds x
  // and y for the full revolution. z takes the segment's own z extent
  // exactly, the parameter that directly clips the shape.
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
  // covers both annulusHit and spanningHit.
  if (thetamax == 0.0)
    return false;

  GMANPoint const objOrigin = gman::transformPoint(cameraToObject, ray.getOrigin());
  GMANVector const objDirection = gman::transformDirection(cameraToObject, ray.getDirection());

  Sweep const sweep = sweepOf(point1, point2, thetamax);

  SweepHit found;
  bool const isHit = sweep.dzSeg == 0.0 ? annulusHit(sweep, objOrigin, objDirection, ray, objectToCamera, found)
                                        : spanningHit(sweep, objOrigin, objDirection, ray, objectToCamera, found);
  if (!isHit)
    return false;

  GMANVector normal = gman::transformNormal(cameraToObject, found.objNormal);
  normal.normalize();

  hit.t = found.t;
  hit.point = found.point;
  hit.normal = normal;
  hit.u = found.theta / sweep.thetamaxRad;
  hit.v = (RtFloat)found.v;
  hit.primitive = this;
  return true;
}
