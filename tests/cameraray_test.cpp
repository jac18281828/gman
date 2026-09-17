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
 * R1 proof: GMANViewingSystem answers a camera-space ray (cameraRay) and
 * a world-space one (ray), and GMANRay carries its [tmin, tmax] defaults.
 *
 * Every check below shares one viewing system built over a non-identity
 * world-to-camera transform -- a translation plus a rotation off the
 * view axis. Checks 1 to 3 read cameraRay, which never touches
 * cameraToWorld; building on identity instead would let a cameraRay that
 * mistakenly returns world space pass every one of them. The rotation
 * must be off the view axis: a pure z rotation fixes (0,0,1) in place,
 * so check 1's centre ray would not catch that mistake either.
 */

#include <cmath>

#include "check.h"
#include "gmanmatrix4.h"
#include "gmanoptions.h"
#include "gmanpoint.h"
#include "gmanray.h"
#include "gmanvector.h"
#include "gmanvsorthographic.h"
#include "gmanvsperspective.h"
#include "ri.h"

namespace {

constexpr RtInt kXRes = 200;
constexpr RtInt kYRes = 100;
constexpr RtFloat kTolerance = 1e-4f;

bool near(RtFloat a, RtFloat b, RtFloat tol = kTolerance) { return std::fabs(a - b) <= tol; }

GMANOptions::ScreenWindowStruct squareWindow() {
  GMANOptions::ScreenWindowStruct sw;
  sw.left = -1.0;
  sw.right = 1.0;
  sw.bottom = -1.0;
  sw.top = 1.0;
  return sw;
}

// Translation plus a rotation off the view axis -- see the file
// comment for why identity or a z-only rotation would not do.
GMANMatrix4 nonIdentityWorldToCamera() {
  GMANMatrix4 m;
  m.rot(0.4, 1.0, 0.3, 0.0);
  m.trans(2.0, -1.5, 4.0);
  return m;
}

// ---- check 1: the centre pixel looks down the view axis ----
void testPerspectiveCentreLooksDownAxis() {
  GMANVSPerspective vs(kXRes, kYRes, squareWindow(), nonIdentityWorldToCamera(), 90.0, 1.0, 100.0);

  GMANRay centre = vs.cameraRay(kXRes / 2.0, kYRes / 2.0);
  check(near(centre.getDirection().getX(), 0.0) && near(centre.getDirection().getY(), 0.0) &&
            near(centre.getDirection().getZ(), 1.0),
        "perspective: the centre pixel's camera-space direction is (0,0,1)");
}

// ---- check 2: the corners match the screen window ----
void testPerspectiveCornersMatchScreenWindow() {
  GMANVSPerspective vs(kXRes, kYRes, squareWindow(), nonIdentityWorldToCamera(), 90.0, 1.0, 100.0);

  RtFloat const corners[4][2] = {
      {0.0, 0.0}, {(RtFloat)kXRes, 0.0}, {0.0, (RtFloat)kYRes}, {(RtFloat)kXRes, (RtFloat)kYRes}};
  for (auto const& corner : corners) {
    RtFloat sx = corner[0];
    RtFloat sy = corner[1];
    vs.rasterToScreen(sx, sy);
    GMANVector expected(sx, sy, 1.0);
    expected.normalize();

    GMANRay ray = vs.cameraRay(corner[0], corner[1]);
    check(near(ray.getDirection().getX(), expected.getX()) && near(ray.getDirection().getY(), expected.getY()) &&
              near(ray.getDirection().getZ(), expected.getZ()),
          "perspective: a raster corner's camera-space direction is the normalized screen-space "
          "point rasterToScreen maps it to");
  }
}

// ---- check 3: orthographic rays are parallel ----
void testOrthographicRaysAreParallel() {
  GMANVSOrthographic vs(kXRes, kYRes, squareWindow(), nonIdentityWorldToCamera(), 1.0, 100.0);

  RtFloat const points[5][2] = {{(RtFloat)kXRes / 2.0f, (RtFloat)kYRes / 2.0f},
                                {0.0, 0.0},
                                {(RtFloat)kXRes, 0.0},
                                {0.0, (RtFloat)kYRes},
                                {(RtFloat)kXRes, (RtFloat)kYRes}};
  for (auto const& point : points) {
    RtFloat sx = point[0];
    RtFloat sy = point[1];
    vs.rasterToScreen(sx, sy);

    GMANRay ray = vs.cameraRay(point[0], point[1]);
    check(near(ray.getDirection().getX(), 0.0) && near(ray.getDirection().getY(), 0.0) &&
              near(ray.getDirection().getZ(), 1.0),
          "orthographic: every camera-space direction is (0,0,1)");
    check(near(ray.getOrigin().getX(), sx) && near(ray.getOrigin().getY(), sy) && near(ray.getOrigin().getZ(), 0.0),
          "orthographic: the camera-space origin spans the screen window");
  }
}

// ---- check 4: the world-space ray is the camera-space ray transformed ----
void testWorldRayIsCameraRayTransformed() {
  GMANVSPerspective vs(kXRes, kYRes, squareWindow(), nonIdentityWorldToCamera(), 90.0, 1.0, 100.0);

  RtFloat const x = 137.0, y = 63.0;
  GMANRay cam = vs.cameraRay(x, y);
  GMANRay world = vs.ray(x, y);

  // Independently derive the expected world-space ray from cameraRay's
  // own output: getCameraToWorld() applied to the origin as a point, and
  // to origin+direction as a point, taking the difference for a
  // translation-free direction. This is the transform ray() is
  // responsible for, not cameraRay's own arithmetic (checks 1-3).
  GMANPoint const origin = cam.getOrigin();
  GMANPoint const through = cam.pointAt(1.0);
  RtFloat srcOrigin[] = {origin.getX(), origin.getY(), origin.getZ()};
  RtFloat srcThrough[] = {through.getX(), through.getY(), through.getZ()};
  RtFloat dstOrigin[3], dstThrough[3];
  GMANMatrix4 c2w = vs.getCameraToWorld();
  c2w.p3m(1, srcOrigin, dstOrigin);
  c2w.p3m(1, srcThrough, dstThrough);
  GMANPoint const expectedOrigin(dstOrigin[0], dstOrigin[1], dstOrigin[2]);
  GMANVector expectedDirection(expectedOrigin, GMANPoint(dstThrough[0], dstThrough[1], dstThrough[2]));
  expectedDirection.normalize();

  check(near(world.getOrigin().getX(), expectedOrigin.getX()) &&
            near(world.getOrigin().getY(), expectedOrigin.getY()) &&
            near(world.getOrigin().getZ(), expectedOrigin.getZ()),
        "world ray: origin is the camera-space origin carried through getCameraToWorld() as a point");
  check(near(world.getDirection().getX(), expectedDirection.getX()) &&
            near(world.getDirection().getY(), expectedDirection.getY()) &&
            near(world.getDirection().getZ(), expectedDirection.getZ()),
        "world ray: direction is the camera-space direction carried through getCameraToWorld() "
        "as a vector -- translation-free");
}

// ---- check 5: the interval defaults ----
void testIntervalDefaults() {
  GMANVSPerspective persp(kXRes, kYRes, squareWindow(), nonIdentityWorldToCamera(), 90.0, 1.0, 100.0);
  GMANVSOrthographic ortho(kXRes, kYRes, squareWindow(), nonIdentityWorldToCamera(), 1.0, 100.0);

  GMANRay perspCam = persp.cameraRay(10.0, 10.0);
  GMANRay perspWorld = persp.ray(10.0, 10.0);
  GMANRay orthoCam = ortho.cameraRay(10.0, 10.0);
  GMANRay orthoWorld = ortho.ray(10.0, 10.0);

  check(perspCam.getTMin() == RI_EPSILON && perspCam.getTMax() == RI_INFINITY,
        "interval defaults: a perspective camera-space ray carries [RI_EPSILON, RI_INFINITY]");
  check(perspWorld.getTMin() == RI_EPSILON && perspWorld.getTMax() == RI_INFINITY,
        "interval defaults: a perspective world-space ray carries [RI_EPSILON, RI_INFINITY]");
  check(orthoCam.getTMin() == RI_EPSILON && orthoCam.getTMax() == RI_INFINITY,
        "interval defaults: an orthographic camera-space ray carries [RI_EPSILON, RI_INFINITY]");
  check(orthoWorld.getTMin() == RI_EPSILON && orthoWorld.getTMax() == RI_INFINITY,
        "interval defaults: an orthographic world-space ray carries [RI_EPSILON, RI_INFINITY]");
}

} // namespace

int main() {
  testPerspectiveCentreLooksDownAxis();
  testPerspectiveCornersMatchScreenWindow();
  testOrthographicRaysAreParallel();
  testWorldRayIsCameraRayTransformed();
  testIntervalDefaults();

  return checkSummary("the camera-space and world-space rays agree");
}
