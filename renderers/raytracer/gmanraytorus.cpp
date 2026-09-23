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
#include "gmanrayquartic.h"
#include "gmanraytorus.h"
#include "gmanvector.h"

namespace {

// The implicit torus (x^2+y^2+z^2+R^2-r^2)^2 == 4*R^2*(x^2+y^2), R ==
// majorradius, r == minorradius, matches GMANTorus::getLocation's own
// parametrization exactly (expand x, y, z there and both sides agree).
// Substituting the shifted ray's x(t), y(t), z(t) (origin (sox, soy,
// soz), direction (dx, dy, dz)) and collecting powers of t gives this
// quartic's coefficients: U(t) == S(t)+R^2-r^2 is quadratic in t (S(t) ==
// x^2+y^2+z^2), and (U(t))^2 - 4*R^2*Q(t) == 0, Q(t) == x(t)^2+y(t)^2.
void torusQuarticCoefficients(double sox, double soy, double soz, double dx, double dy, double dz, double dirSq,
                              double R, double r, double& a, double& b, double& c, double& d, double& e) {
  double const K = R * R - r * r;
  double const S0 = sox * sox + soy * soy + soz * soz;
  double const S1 = sox * dx + soy * dy + soz * dz;
  double const Q0 = sox * sox + soy * soy;
  double const Q1 = sox * dx + soy * dy;
  double const Q2 = dx * dx + dy * dy;

  double const U0 = S0 + K;
  double const U1 = 2.0 * S1;
  double const U2 = dirSq;

  a = U2 * U2;
  b = 2.0 * U1 * U2;
  c = U1 * U1 + 2.0 * U0 * U2 - 4.0 * R * R * Q2;
  d = 2.0 * U0 * U1 - 8.0 * R * R * Q1;
  e = U0 * U0 - 4.0 * R * R * Q0;
}

// phi is only defined up to a whole turn; its representative in
// [loBound, loBound + 360) is what modular arithmetic gives directly, for
// a band offset by any number of turns. A descending band (phimax <
// phimin) takes the same computation, since loBound and hiBound are
// already sorted by the caller. The inner fmod's result carries the sign
// of phiDeg - loBound; adding 360 before the outer fmod folds a negative
// result back into [0, 360). Returns false when that representative sits
// past hiBound; otherwise writes v, computed from it through
// GMANTorus::getLocation's own inverse (phimin and phimax unsorted).
bool torusBandParameter(double phiDeg, double loBound, double hiBound, double phimin, double phimax, double& v) {
  double const candidate = loBound + std::fmod(std::fmod(phiDeg - loBound, 360.0) + 360.0, 360.0);
  if (candidate > hiBound)
    return false;
  v = (candidate - phimin) / (phimax - phimin);
  return true;
}

} // namespace

GMANRayTorus::GMANRayTorus(RtFloat majorradius, RtFloat minorradius, RtFloat phimin, RtFloat phimax, RtFloat thetamax,
                           GMANParameterList pl, GMANTransform const& transform)
    : GMANTorus(majorradius, minorradius, phimin, phimax, thetamax, pl), objectToCamera(transform.interpolate(0.0)),
      cameraToObject(objectToCamera) {
  try {
    cameraToObject.invert();
  } catch (GMANError const&) {
    singular = true;
  }

  if (singular)
    return;

  // majorradius/minorradius are the shape's own named extrema: x and y
  // bound in +/-(majorradius + minorradius), z in +/-minorradius, for the
  // full revolution. Absolute values keep the box finite and well-ordered
  // even for a majorradius/minorradius pair intersect() itself rejects
  // (the spindle/horn torus, a negative radius).
  RtFloat const majorAbs = (RtFloat)std::fabs(majorradius);
  RtFloat const minorAbs = (RtFloat)std::fabs(minorradius);
  bbox = gman::revolutionBBox(objectToCamera, majorAbs + minorAbs, -minorAbs, minorAbs);
}

bool GMANRayTorus::intersect(const GMANRay& ray, GMANHit& hit) const {
  if (singular)
    return false;

  // Degenerate parameters leave no surface to hit; without this guard,
  // dividing by them below fills the hit with NaN instead of reporting a
  // miss: (phimax - phimin) in the band's v formula, r in the normal's
  // cosPhi and pz/r, and thetamaxRad in u's theta/thetamaxRad.
  if (minorradius == 0.0 || thetamax == 0.0 || phimin == phimax)
    return false;

  // The quartic below sees only majorradius^2 and minorradius^2, but
  // GMANTorus::getLocation's phi shift for a negative minorradius (and the
  // spindle guard a negative majorradius falls into regardless) would
  // leave the (u, v) inverse and the normal's sign below wrong.
  if (majorradius < 0.0 || minorradius < 0.0)
    return false;

  // The spindle and horn torus (minorradius >= majorradius) returns
  // false: the tube crosses the axis there, and a surface point gets a
  // second parameterization -- (theta, phi) and (theta + 180, phi') --
  // whose wedge/band membership would decide the hit; that second inverse
  // mapping is unimplemented.
  if (minorradius >= majorradius)
    return false;

  // Open question, matching gmanraysphere.cpp's own: a negative thetamax
  // hits nothing here, while GMANTorus::getLocation sweeps a real
  // clockwise wedge for one. Which behavior is right is undecided.

  GMANPoint const objOrigin = gman::transformPoint(cameraToObject, ray.getOrigin());
  GMANVector const objDirection = gman::transformDirection(cameraToObject, ray.getDirection());

  gman::ShiftedRay shifted;
  if (!gman::shiftIntoBoundingSphere(objOrigin, objDirection, majorradius + minorradius, shifted))
    return false;

  double const R = majorradius, r = minorradius;
  double a = 0.0, b = 0.0, c = 0.0, d = 0.0, e = 0.0;
  torusQuarticCoefficients(shifted.ox, shifted.oy, shifted.oz, shifted.dx, shifted.dy, shifted.dz, shifted.dirSq, R, r,
                           a, b, c, d, e);

  double localRoots[4];
  int const numRoots = gman::solveQuartic(a, b, c, d, e, localRoots);
  if (numRoots == 0)
    return false;

  double const thetamaxRad = thetamax / 360.0 * 2.0 * PI;
  double const loBound = GMANMin<double>(phimin, phimax);
  double const hiBound = GMANMax<double>(phimin, phimax);

  // Roots nearest first, falling through on a wedge, band or interval
  // miss: a root can fail any of the three while a later one is still the
  // hit (GMANRaySphere's own pattern).
  for (int i = 0; i < numRoots; ++i) {
    double const tLocal = localRoots[i];
    gman::ShiftedRootPoint const root = gman::shiftedRootPoint(shifted, tLocal, objectToCamera);
    if (root.t < ray.getTMin() || root.t > ray.getTMax())
      continue;

    double rawTheta = std::atan2(root.py, root.px);
    if (rawTheta < 0.0)
      rawTheta += 2.0 * PI;
    if (rawTheta > thetamaxRad)
      continue;

    double const radial =
        std::sqrt(root.px * root.px + root.py * root.py); // > 0: minorradius < majorradius, guarded above.
    double const phiRad = std::atan2(root.pz, radial - R);
    double const phiDeg0 = phiRad / DEGTORAD;

    double v = 0.0;
    if (!torusBandParameter(phiDeg0, loBound, hiBound, phimin, phimax, v))
      continue;

    // The outward normal: the unit vector from the tube's centre circle
    // (radius R around z) to the hit point. cos(theta) == px/radial,
    // sin(theta) == py/radial, cos(phi) == (radial-R)/r, sin(phi) == pz/r
    // -- GMANTorus::getNormal's own formula before its normalize().
    double const cosPhi = (radial - R) / r;
    GMANVector objNormal((RtFloat)(cosPhi * root.px / radial), (RtFloat)(cosPhi * root.py / radial),
                         (RtFloat)(root.pz / r));
    GMANVector normal = gman::transformNormal(cameraToObject, objNormal);
    normal.normalize();

    hit.t = root.t;
    hit.point = root.point;
    hit.normal = normal;
    hit.u = (RtFloat)(rawTheta / thetamaxRad);
    hit.v = (RtFloat)v;
    hit.primitive = this;
    return true;
  }

  return false;
}
