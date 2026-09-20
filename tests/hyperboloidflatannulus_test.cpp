/* SPDX-License-Identifier: LGPL-2.1-or-later
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

/*
 * R5b deferral: a hyperboloid segment with point1.z == point2.z sweeps a
 * flat annulus, not a surface spanning z -- GMANRayHyperboloid::intersect
 * returns false for it rather than solving that case (see the prompt's
 * own scope: v is not recoverable from z alone there, the radial extent
 * is [min r(v), max r(v)] rather than the endpoint radii, and the wedge
 * is a spiral sector since phi(v) varies with v). This pins the wanted
 * behavior -- WILL_FAIL until a future unit implements it.
 */

#include "check.h"
#include "gmanray.h"
#include "gmanrayhyperboloid.h"

int main() {
  // A flat annulus at z == 0, inner radius 1 (point1), outer radius 2
  // (point2): a ray straight down through r == 1.5 should hit it.
  RtPoint p1 = {1.0, 0.0, 0.0};
  RtPoint p2 = {2.0, 0.0, 0.0};
  GMANRayHyperboloid hyperboloid(p1, p2, 360.0, GMANParameterList());
  GMANRay ray(GMANPoint(1.5, 0.0, -5.0), GMANVector(0.0, 0.0, 1.0));
  GMANHit hit;

  bool const hitFound = hyperboloid.intersect(ray, hit);
  check(hitFound, "flat annulus: a ray through r == 1.5 hits the annulus at z == 0");
  check(hitFound && hit.t == 5.0, "flat annulus: t == 5");

  return checkSummary("GMANRayHyperboloid::intersect handles a flat annulus (point1.z == point2.z)");
}
