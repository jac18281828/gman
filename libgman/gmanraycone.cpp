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
#include "gmanraycone.h"
#include "gmanrayquadratic.h"
#include "gmanvector.h"

GMANRayCone::GMANRayCone(RtFloat height, RtFloat radius, RtFloat thetamax, GMANParameterList pl,
                         GMANTransform const& transform)
    : GMANCone(height, radius, thetamax, pl), objectToCamera(transform.interpolate(0.0)),
      cameraToObject(objectToCamera) {
  try {
    cameraToObject.invert();
  } catch (GMANError const&) {
    singular = true;
  }

  if (singular)
    return;

  // r(z) == radius*(1 - z/height) (GMANCone::intersect's own radial
  // factor) is linear from radius at z == 0 to 0 at z == height, so its
  // magnitude over [0, height] never exceeds |radius|; z spans 0..height
  // exactly, the parameter that directly clips the shape.
  bbox = gman::revolutionBBox(objectToCamera, radius, (RtFloat)0.0, height);
}

bool GMANRayCone::intersect(const GMANRay& ray, GMANHit& hit) const {
  if (singular)
    return false;

  // A null wedge, a zero radius or a zero height leaves no surface to hit,
  // the sphere's own guard against a NaN-filled hit: height also divides
  // into A and B below. Its open question about negative thetamax and
  // negative radius stands unchanged here, and a negative height belongs
  // to the same question.
  if (thetamax == 0.0 || radius == 0.0 || height == 0.0)
    return false;

  GMANPoint const objOrigin = gman::transformPoint(cameraToObject, ray.getOrigin());
  GMANVector const objDirection = gman::transformDirection(cameraToObject, ray.getDirection());

  // Every surface point satisfies x^2+y^2+z^2 <= radius^2 + height^2,
  // gmanraybbox.h's revolutionBBox's own r, z0, z1 bound (radius, 0,
  // height here).
  double const boundingRadius = std::sqrt((double)radius * radius + (double)height * height);

  gman::ShiftedRay shifted;
  if (!gman::shiftIntoBoundingSphere(objOrigin, objDirection, boundingRadius, shifted))
    return false;

  // x^2 + y^2 == g(z)^2, g(z) = radius*(1 - z/height) (GMANCone::getLocation's
  // own radial factor). g(z(u)) is affine in the ray parameter t, g(z(t)) ==
  // A + B*t, so substituting collects into an ordinary a*t^2+b*t+c == 0.
  double const A = (double)radius * (1.0 - shifted.oz / (double)height);
  double const B = -(double)radius * shifted.dz / (double)height;

  double const a = shifted.dx * shifted.dx + shifted.dy * shifted.dy - B * B;
  double const b = 2.0 * (shifted.dx * shifted.ox + shifted.dy * shifted.oy - A * B);
  double const c = shifted.ox * shifted.ox + shifted.oy * shifted.oy - A * A;

  // a vanishes for a ray parallel to a generatrix: an ordinary ray with no
  // quadratic term left to solve, not a degenerate direction.
  // gman::solveRayQuadratic's own comment covers the linear case and why a
  // near-degenerate a never reaches it.
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
    if (z < 0.0 || z > height)
      continue;

    RtFloat const theta = GMANMod(GMANAtan((RtFloat)root.py, (RtFloat)root.px), (RtFloat)(2.0 * PI));
    if (theta > thetamaxRad)
      continue;

    // GMANCone::getNormal's own closed form: independent of the radial
    // coordinate, so well-defined at the apex too.
    GMANVector objNormal((RtFloat)cos(theta), (RtFloat)sin(theta), radius / height);
    GMANVector normal = gman::transformNormal(cameraToObject, objNormal);
    normal.normalize();

    hit.t = root.t;
    hit.point = root.point;
    hit.normal = normal;
    hit.u = theta / thetamaxRad;
    hit.v = z / height;
    hit.primitive = this;
    return true;
  }

  return false;
}
