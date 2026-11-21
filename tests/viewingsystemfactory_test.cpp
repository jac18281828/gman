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
 * gman::makeViewingSystem returns the viewing system for a
 * gman::ViewingSystemInputs: orthographic when the projection name is
 * "orthographic", perspective for any other name, with the fov, screen window,
 * raster resolution and world-to-camera matrix carried through.
 *
 * Revert check: making makeViewingSystem always build the perspective system
 * turns the orthographic ray assertions red.
 */

#include <cmath>
#include <memory>
#include <numbers>
#include <string>

#include "check.h"
#include "gmanmatrix4.h"
#include "gmanray.h"
#include "gmanviewingsystem.h"
#include "gmanviewingsysteminputs.h"

namespace {

constexpr RtInt kRes = 100;

gman::ViewingSystemInputs inputsFor(std::string const& projectionName, RtFloat fov) {
  gman::ViewingSystemInputs inputs;
  inputs.projectionName = projectionName;
  inputs.fov = fov;
  inputs.fovDefaulted = false;
  inputs.screenWindow.left = -1.0;
  inputs.screenWindow.right = 1.0;
  inputs.screenWindow.bottom = -1.0;
  inputs.screenWindow.top = 1.0;
  inputs.clipping.nearDist = 1.0;
  inputs.clipping.farDist = 100.0;
  inputs.xres = kRes;
  inputs.yres = kRes;
  return inputs;
}

GMANMatrix4 nonIdentityWorldToCamera() {
  GMANMatrix4 m;
  m.rot(0.4, 1.0, 0.3, 0.0);
  m.trans(2.0, -1.5, 4.0);
  return m;
}

bool leavesFromOrigin(GMANRay const& ray) {
  return ray.getOrigin().getX() == 0.0f && ray.getOrigin().getY() == 0.0f && ray.getOrigin().getZ() == 0.0f;
}

bool pointsDownViewAxis(GMANRay const& ray) {
  return ray.getDirection().getX() == 0.0f && ray.getDirection().getY() == 0.0f && ray.getDirection().getZ() == 1.0f;
}

void testProjectionName() {
  GMANMatrix4 const identity;

  auto const orthographic = gman::makeViewingSystem(inputsFor("orthographic", 90.0), identity);
  GMANRay const orthoNear = orthographic->cameraRay(10.0, 20.0);
  GMANRay const orthoFar = orthographic->cameraRay(80.0, 60.0);
  check(pointsDownViewAxis(orthoNear) && pointsDownViewAxis(orthoFar),
        "factory: \"orthographic\" yields rays with direction (0, 0, 1)");
  check(orthoNear.getOrigin().getX() != orthoFar.getOrigin().getX() &&
            orthoNear.getOrigin().getY() != orthoFar.getOrigin().getY(),
        "factory: \"orthographic\" yields an origin that moves with the raster point");

  for (std::string const name : {"perspective", ""}) {
    auto const perspective = gman::makeViewingSystem(inputsFor(name, 90.0), identity);
    GMANRay const offCentre = perspective->cameraRay(10.0, 20.0);
    check(leavesFromOrigin(offCentre) && leavesFromOrigin(perspective->cameraRay(80.0, 60.0)) &&
              !pointsDownViewAxis(offCentre),
          "factory: projection name \"" + name + "\" yields rays leaving the camera-space origin");
  }
}

void testFovReachesProjection() {
  GMANMatrix4 const identity;
  RtFloat const narrow = 40.0;
  RtFloat const wide = 100.0;

  auto const narrowCamera = gman::makeViewingSystem(inputsFor("perspective", narrow), identity);
  auto const wideCamera = gman::makeViewingSystem(inputsFor("perspective", wide), identity);
  GMANVector const narrowDir = narrowCamera->cameraRay(75.0, 25.0).getDirection();
  GMANVector const wideDir = wideCamera->cameraRay(75.0, 25.0).getDirection();

  double const expected = std::tan(narrow * std::numbers::pi / 360.0) / std::tan(wide * std::numbers::pi / 360.0);
  double const xRatio = (narrowDir.getX() / narrowDir.getZ()) / (wideDir.getX() / wideDir.getZ());
  double const yRatio = (narrowDir.getY() / narrowDir.getZ()) / (wideDir.getY() / wideDir.getZ());
  check(std::fabs(xRatio - expected) < 1e-5, "factory: fov reaches the projection's x slope as tan(fov/2)");
  check(std::fabs(yRatio - expected) < 1e-5, "factory: fov reaches the projection's y slope as tan(fov/2)");
}

void testWorldToCameraCarried() {
  GMANMatrix4 const worldToCamera = nonIdentityWorldToCamera();
  for (std::string const name : {"orthographic", "perspective"}) {
    auto const camera = gman::makeViewingSystem(inputsFor(name, 90.0), worldToCamera);
    bool same = true;
    for (int row = 0; row < 4; ++row) {
      for (int col = 0; col < 4; ++col) {
        same = same && camera->getWorldToCamera().get()[row][col] == worldToCamera.get()[row][col];
      }
    }
    check(same, "factory: " + name + " camera carries the world-to-camera matrix it was given");
  }
}

} // namespace

int main() {
  testProjectionName();
  testFovReachesProjection();
  testWorldToCameraCarried();

  return checkSummary("the factory builds the viewing system its inputs name");
}
