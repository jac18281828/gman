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
 * R5c deferral: a torus with minorradius >= majorradius is a spindle or
 * horn torus -- the tube crosses the axis, and a surface point there has
 * two parameterizations, (theta, phi) and (theta + 180, phi'), whose
 * wedge/band membership decides the hit (see the prompt's own account).
 * GMANRayTorus::intersect returns false for one rather than solving that
 * second inverse mapping. This pins the wanted behavior -- WILL_FAIL until
 * a future unit implements it -- at theta == 0, phi == 0, the outer
 * equator, unambiguous even for a spindle torus and the same point
 * raytorus_test.cpp's testAxisAlignedHits already verifies for an ordinary
 * one.
 */

#include "check.h"
#include "gmanray.h"
#include "gmanraytorus.h"

int main() {
  GMANRayTorus torus(1.0, 1.5, 0.0, 360.0, 360.0, GMANParameterList());
  GMANPoint const origin(10.0, 0.0, 0.0);
  GMANRay ray(origin, GMANVector(origin, GMANPoint(0.0, 0.0, 0.0)));
  GMANHit hit;

  bool const hitFound = torus.intersect(ray, hit);
  check(hitFound, "spindle: a ray along x hits the outer equator at majorradius + minorradius");
  check(hitFound && hit.t == 7.5f, "spindle: t == 7.5");

  return checkSummary("GMANRayTorus::intersect handles a spindle torus (minorradius >= majorradius)");
}
