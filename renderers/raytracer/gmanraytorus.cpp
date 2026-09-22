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
#include "gmanrayquartic.h"
#include "gmanraytorus.h"
#include "gmanvector.h"

GMANRayTorus::GMANRayTorus(RtFloat majorradius, RtFloat minorradius, RtFloat phimin, RtFloat phimax, RtFloat thetamax,
                           GMANParameterList pl, GMANTransform const& transform)
    : GMANTorus(majorradius, minorradius, phimin, phimax, thetamax, pl), objectToCamera(transform.interpolate(0.0)),
      cameraToObject(objectToCamera) {
  try {
    cameraToObject.invert();
  } catch (GMANError const&) {
    singular = true;
  }
}

bool GMANRayTorus::intersect(const GMANRay& ray, GMANHit& hit) const {
  if (singular)
    return false;

  // Degenerate parameters leave no surface to hit; without this guard, the
  // band search's division by (phimax - phimin) below would fill the hit
  // with NaN instead of reporting a miss.
  if (minorradius == 0.0 || thetamax == 0.0 || phimin == phimax)
    return false;

  // The quartic below sees only majorradius^2 and minorradius^2, but
  // GMANTorus::getLocation's phi shift for a negative minorradius (and the
  // spindle guard a negative majorradius falls into regardless) would
  // leave the (u, v) inverse and the normal's sign below wrong.
  if (majorradius < 0.0 || minorradius < 0.0)
    return false;

  // The spindle and horn torus (minorradius >= majorradius): the tube
  // crosses the axis, and a surface point then has two parameterizations
  // -- (theta, phi) and (theta + 180, phi') -- whose wedge/band membership
  // decides the hit. A second inverse mapping this unit defers; see the
  // prompt's own account.
  if (minorradius >= majorradius)
    return false;

  // Open question, matching gmanraysphere.cpp's own: a negative thetamax
  // hits nothing here, while GMANTorus::getLocation sweeps a real
  // clockwise wedge for one. Which behavior is right is undecided.

  GMANPoint const objOrigin = gman::transformPoint(cameraToObject, ray.getOrigin());
  GMANVector const objDirection = gman::transformDirection(cameraToObject, ray.getDirection());

  double const dx = objDirection.getX(), dy = objDirection.getY(), dz = objDirection.getZ();
  double const dirSq = dx * dx + dy * dy + dz * dz;
  // A zero-length direction survives GMANRay's own construction (see
  // gmanray.h); the shift below divides by dirSq, so this check must come
  // first.
  if (dirSq == 0.0)
    return false;

  double const ox = objOrigin.getX(), oy = objOrigin.getY(), oz = objOrigin.getZ();

  // Shift the object-space origin to the ray's closest approach to the
  // torus's centre when the ray starts outside the bounding sphere
  // (radius majorradius + minorradius, centred on the origin): every
  // coefficient below is then built from a point near the torus regardless
  // of camera distance, so the roots the quartic solver returns stay near
  // zero and its Newton polish has an accurate root to converge to. A ray
  // whose closest approach still clears the bounding sphere misses the
  // torus outright.
  double const boundingRadius = majorradius + minorradius;
  double const boundingRadiusSq = boundingRadius * boundingRadius;
  double shift = 0.0;
  if (ox * ox + oy * oy + oz * oz > boundingRadiusSq) {
    shift = -(ox * dx + oy * dy + oz * dz) / dirSq;
    double const cx = ox + shift * dx, cy = oy + shift * dy, cz = oz + shift * dz;
    if (cx * cx + cy * cy + cz * cz > boundingRadiusSq)
      return false;
  }

  double const sox = ox + shift * dx, soy = oy + shift * dy, soz = oz + shift * dz;

  // The implicit torus (x^2+y^2+z^2 + R^2 - r^2)^2 == 4*R^2*(x^2+y^2),
  // R == majorradius, r == minorradius, matches GMANTorus::getLocation's
  // parametrization exactly (expand x, y, z there and both sides agree).
  // Substituting x(t), y(t), z(t) along the shifted ray and collecting
  // powers of t gives a quartic: U(t) == S(t) + R^2 - r^2 is quadratic in
  // t (S(t) == x^2+y^2+z^2), and (U(t))^2 - 4*R^2*Q(t) == 0, Q(t) ==
  // x(t)^2+y(t)^2, expands to the coefficients below.
  double const R = majorradius, r = minorradius;
  double const K = R * R - r * r;

  double const S0 = sox * sox + soy * soy + soz * soz;
  double const S1 = sox * dx + soy * dy + soz * dz;
  double const Q0 = sox * sox + soy * soy;
  double const Q1 = sox * dx + soy * dy;
  double const Q2 = dx * dx + dy * dy;

  double const U0 = S0 + K;
  double const U1 = 2.0 * S1;
  double const U2 = dirSq;

  double const a = U2 * U2;
  double const b = 2.0 * U1 * U2;
  double const c = U1 * U1 + 2.0 * U0 * U2 - 4.0 * R * R * Q2;
  double const d = 2.0 * U0 * U1 - 8.0 * R * R * Q1;
  double const e = U0 * U0 - 4.0 * R * R * Q0;

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
    RtFloat const t = (RtFloat)(tLocal + shift);
    if (t < ray.getTMin() || t > ray.getTMax())
      continue;

    double const px = sox + tLocal * dx;
    double const py = soy + tLocal * dy;
    double const pz = soz + tLocal * dz;

    double rawTheta = std::atan2(py, px);
    if (rawTheta < 0.0)
      rawTheta += 2.0 * PI;
    if (rawTheta > thetamaxRad)
      continue;

    // phi is only defined up to a whole turn; search a handful of turns
    // either side of the raw value for a representative that falls in
    // [phimin, phimax] (or [phimax, phimin] for a descending band).
    double const radial = std::sqrt(px * px + py * py); // > 0: minorradius < majorradius, guarded above.
    double const phiRad = std::atan2(pz, radial - R);
    double const phiDeg0 = phiRad / DEGTORAD;

    bool foundBand = false;
    double v = 0.0;
    for (int k = -3; k <= 3 && !foundBand; ++k) {
      double const candidate = phiDeg0 + 360.0 * k;
      if (candidate < loBound || candidate > hiBound)
        continue;
      v = (candidate - phimin) / (phimax - phimin);
      foundBand = true;
    }
    if (!foundBand)
      continue;

    // The outward normal: the unit vector from the tube's centre circle
    // (radius R around z) to the hit point. cos(theta) == px/radial,
    // sin(theta) == py/radial, cos(phi) == (radial-R)/r, sin(phi) == pz/r
    // -- GMANTorus::getNormal's own formula before its normalize().
    double const cosPhi = (radial - R) / r;
    GMANVector objNormal((RtFloat)(cosPhi * px / radial), (RtFloat)(cosPhi * py / radial), (RtFloat)(pz / r));
    GMANVector normal = gman::transformNormal(cameraToObject, objNormal);
    normal.normalize();

    GMANPoint const objPoint((RtFloat)px, (RtFloat)py, (RtFloat)pz);

    hit.t = t;
    hit.point = gman::transformPoint(objectToCamera, objPoint);
    hit.normal = normal;
    hit.u = (RtFloat)(rawTheta / thetamaxRad);
    hit.v = (RtFloat)v;
    hit.primitive = this;
    return true;
  }

  return false;
}
