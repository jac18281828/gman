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
#include "gmanraybbox.h"
#include "gmanraydisk.h"
#include "gmanvector.h"

GMANRayDisk::GMANRayDisk(RtFloat height, RtFloat radius, RtFloat thetamax, GMANParameterList pl,
                         GMANTransform const& transform)
    : GMANDisk(height, radius, thetamax, pl), objectToCamera(transform.interpolate(0.0)),
      cameraToObject(objectToCamera) {
  try {
    cameraToObject.invert();
  } catch (GMANError const&) {
    singular = true;
  }

  if (singular)
    return;

  // The flat plate's z is height exactly, the parameter that directly
  // clips the shape; x and y bound in +/-radius, the full revolution.
  bbox = gman::revolutionBBox(objectToCamera, radius, height, height);
}

bool GMANRayDisk::intersect(const GMANRay& ray, GMANHit& hit) const {
  if (singular)
    return false;

  // A null wedge or a zero radius leaves no surface to hit; without this
  // guard, u's theta/thetamaxRad division below fills the hit with NaN
  // instead of reporting a miss.
  //
  // Open question, the same one GMANRaySphere's own intersect leaves open:
  // a negative thetamax hits nothing here (thetamaxRad goes negative, and
  // theta -- always in [0, 2*PI) -- can never be less than it), and a
  // negative radius likewise hits nothing (r is never negative, so
  // r > radius is always true), while GMANDisk::getLocation sweeps or
  // mirrors a real shape for either. Which behavior is right is undecided.
  if (thetamax == 0.0 || radius == 0.0)
    return false;

  GMANPoint const objOrigin = gman::transformPoint(cameraToObject, ray.getOrigin());
  GMANVector const objDirection = gman::transformDirection(cameraToObject, ray.getDirection());

  // The plane z == height, in object space. A ray exactly parallel to it
  // never reaches height, whatever its origin.
  if (objDirection.getZ() == (RtFloat)0.0)
    return false;

  RtFloat const t = (height - objOrigin.getZ()) / objDirection.getZ();
  if (t < ray.getTMin() || t > ray.getTMax())
    return false;

  RtFloat const x = objOrigin.getX() + objDirection.getX() * t;
  RtFloat const y = objOrigin.getY() + objDirection.getY() * t;
  RtFloat const r = (RtFloat)sqrt(x * x + y * y);
  if (r > radius)
    return false;

  RtFloat const thetamaxRad = (RtFloat)(thetamax / 360.0 * 2.0 * PI);
  RtFloat const theta = GMANMod(GMANAtan(y, x), (RtFloat)(2.0 * PI));
  if (theta > thetamaxRad)
    return false;

  // (0, 0, -1), GMANDisk::getNormal's own value: this flat plate's normal
  // does not depend on u or v, so there is no per-hit case to compute.
  GMANVector const objNormal(0.0, 0.0, -1.0);
  GMANVector normal = gman::transformNormal(cameraToObject, objNormal);
  normal.normalize();

  hit.t = t;
  hit.point = ray.pointAt(t);
  hit.normal = normal;
  hit.u = theta / thetamaxRad;
  hit.v = r / radius;
  hit.primitive = this;
  return true;
}
