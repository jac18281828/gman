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
 * GMANViewingSystem::facesCamera answers in camera-space core types alone.
 * gman::VSPerspective measures a normal against the vector from the eye at
 * the origin to the position, so one normal can face the camera at one
 * position and not at another; gman::VSOrthographic measures against +z and
 * ignores the position. Both tests are strict: a perpendicular normal does
 * not face the camera.
 *
 * Revert check: making VSPerspective::facesCamera compare the normal's z
 * component, as VSOrthographic does, turns the two-position assertion red.
 */

#include "check.h"
#include "gmanmatrix4.h"
#include "gmanoptions.h"
#include "gmanpoint.h"
#include "gmanvector.h"
#include "gmanvsorthographic.h"
#include "gmanvsperspective.h"

namespace {

GMANOptions::ScreenWindowStruct squareWindow() {
  GMANOptions::ScreenWindowStruct sw;
  sw.left = -1.0;
  sw.right = 1.0;
  sw.bottom = -1.0;
  sw.top = 1.0;
  return sw;
}

void testPerspective() {
  GMANMatrix4 identity;
  gman::VSPerspective const vs(100, 100, squareWindow(), identity, 90.0, 1.0, 100.0);

  GMANVector const slanted(1.0, 0.0, -0.001);
  check(vs.facesCamera(GMANPoint(5.0, 0.0, 0.01), slanted),
        "perspective: a normal with a hairline-negative z faces the camera at +x, where the view vector is almost +x");
  check(!vs.facesCamera(GMANPoint(-5.0, 0.0, 0.01), slanted),
        "perspective: the same normal does not face the camera at -x, where the view vector is almost -x");

  check(!vs.facesCamera(GMANPoint(1.0, 0.0, 0.0), GMANVector(0.0, 1.0, 0.0)),
        "perspective: a normal perpendicular to the view vector does not face the camera");
}

void testOrthographic() {
  GMANMatrix4 identity;
  gman::VSOrthographic const vs(100, 100, squareWindow(), identity, 1.0, 100.0);

  GMANVector const slanted(1.0, 0.0, -0.001);
  check(!vs.facesCamera(GMANPoint(5.0, 0.0, 0.01), slanted),
        "orthographic: a normal with a negative z does not face the camera at +x");
  check(!vs.facesCamera(GMANPoint(-5.0, 0.0, 0.01), slanted),
        "orthographic: the same normal does not face the camera at -x either");

  GMANVector const towardCamera(0.0, 0.0, 1.0);
  check(vs.facesCamera(GMANPoint(5.0, 0.0, 0.01), towardCamera), "orthographic: a +z normal faces the camera at +x");
  check(vs.facesCamera(GMANPoint(-5.0, 0.0, 0.01), towardCamera), "orthographic: a +z normal faces the camera at -x");

  check(!vs.facesCamera(GMANPoint(1.0, 0.0, 0.0), GMANVector(1.0, 0.0, 0.0)),
        "orthographic: a normal perpendicular to +z does not face the camera");
}

} // namespace

int main() {
  testPerspective();
  testOrthographic();

  return checkSummary("facing is answered in camera space");
}
