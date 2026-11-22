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
#include <numbers>

#include "gmaninvertiblematrix.h"
#include "gmanmath.h"
#include "gmanmatrix4.h"
#include "gmanparameterlist.h"
#include "gmanpoint.h"
#include "gmanprimitives.h"
#include "gmanray.h"
#include "gmanraybbox.h"
#include "gmanraycylinder.h"
#include "gmanrayfinitehit.h"
#include "gmanrayquadratic.h"
#include "gmantransform.h"
#include "gmanvector.h"
#include "ri.h"

namespace {

void cylinderTangents(RtFloat thetamaxRad, RtFloat zSpan, double px, double py, GMANVector& dPdu, GMANVector& dPdv) {
  double const alongU = (double)thetamaxRad;
  dPdu = GMANVector((RtFloat)(-alongU * py), (RtFloat)(alongU * px), 0.0);
  dPdv = GMANVector(0.0, 0.0, zSpan);
}

} // namespace

GMANRayCylinder::GMANRayCylinder(RtFloat radius, RtFloat zmin, RtFloat zmax, RtFloat thetamax, GMANParameterList pl,
                                 GMANTransform const& transform)
    : GMANCylinder(radius, zmin, zmax, thetamax, pl),
      objectToCamera(gman::InvertibleMatrix::of(transform.interpolate(0.0))) {
  if (!objectToCamera)
    return;

  // The wall's radius is constant across z, so x and y bound in
  // +/-radius; z takes zmin/zmax exactly, the parameters that directly
  // clip the shape.
  bbox = gman::revolutionBBox(objectToCamera->forward(), radius, zmin, zmax);
}

bool GMANRayCylinder::intersect(GMANRay const& ray, GMANHit& hit) const {
  if (!objectToCamera)
    return false;
  gman::InvertibleMatrix const& placement = *objectToCamera;

  // A null z-band, a null wedge or a zero radius leaves no surface to hit,
  // the sphere's own guard against a NaN-filled hit. Its open question
  // about negative thetamax and negative radius stands unchanged here.
  if (zmin == zmax || thetamax == 0.0 || radius == 0.0)
    return false;

  GMANPoint const objOrigin = gman::transformPoint(placement.backward(), ray.getOrigin());
  GMANVector const objDirection = gman::transformDirection(placement.backward(), ray.getDirection());

  // Every surface point satisfies x^2+y^2+z^2 <= radius^2 + max(zmin^2,
  // zmax^2), gmanraybbox.h's revolutionBBox's own r, z0, z1 bound
  // (radius, zmin, zmax here).
  double const boundingRadius = std::sqrt((double)radius * radius + GMANMax((double)zmin * zmin, (double)zmax * zmax));

  gman::ShiftedRay shifted;
  if (!gman::shiftIntoBoundingSphere(objOrigin, objDirection, boundingRadius, shifted))
    return false;

  // x^2 + y^2 == radius^2, independent of z. dx and dy square in double,
  // so a vanishes only when the ray's object-space direction has no x or
  // y component at all -- straight down the axis, which either lies
  // outside the wall for its whole length or runs along it, neither a
  // transverse hit.
  double const a = shifted.dx * shifted.dx + shifted.dy * shifted.dy;
  double const b = 2.0 * (shifted.dx * shifted.ox + shifted.dy * shifted.oy);
  double const c = shifted.ox * shifted.ox + shifted.oy * shifted.oy - (double)radius * (double)radius;

  double t0 = 0.0, t1 = 0.0;
  int const numRoots = gman::solveRayQuadratic(a, b, c, t0, t1);
  if (numRoots == 0)
    return false;

  double const roots[2] = {t0, t1};
  RtFloat const thetamaxRad = GMANRadians(thetamax);

  for (int i = 0; i < numRoots; ++i) {
    double const tLocal = roots[i];
    gman::ShiftedRootPoint const root = gman::shiftedRootPoint(shifted, tLocal, placement.forward());
    if (!(ray.getTMin() <= root.t && root.t <= ray.getTMax()))
      continue;

    RtFloat const z = (RtFloat)root.pz;
    if (!(zmin <= z && z <= zmax))
      continue;

    RtFloat const theta =
        GMANMod(GMANAtan((RtFloat)root.py, (RtFloat)root.px), RtFloat(2.0) * std::numbers::pi_v<RtFloat>);
    if (!(theta <= thetamaxRad))
      continue;

    // The wall's implicit gradient (2x, 2y, 0) points radially outward,
    // matching GMANCylinder::getNormal's (cos theta, sin theta, 0)
    // exactly: both are the same outward radial direction.
    GMANVector const objNormal((RtFloat)root.px, (RtFloat)root.py, 0.0);
    GMANVector normal = gman::transformNormal(placement, objNormal);
    normal.normalize();

    GMANVector objDPdu;
    GMANVector objDPdv;
    cylinderTangents(thetamaxRad, zmax - zmin, root.px, root.py, objDPdu, objDPdv);

    GMANHit candidate;
    candidate.t = root.t;
    candidate.point = root.point;
    candidate.normal = normal;
    candidate.u = theta / thetamaxRad;
    candidate.v = (z - zmin) / (zmax - zmin);
    candidate.dPdu = gman::transformDirection(placement, objDPdu);
    candidate.dPdv = gman::transformDirection(placement, objDPdv);
    candidate.primitive = this;
    if (gman::acceptFiniteHit(candidate, hit))
      return true;
  }

  return false;
}
