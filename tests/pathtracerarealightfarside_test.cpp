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
 * The path tracer's own area-light behaviour on the far side of the
 * sphere's own fixed preview point: an emitting sphere placed so a
 * light-choice weight tied to one fixed object-space point would zero out
 * over most of the floor still matches the closed-form irradiance there.
 */

#include <cmath>
#include <vector>

#include "check.h"
#include "gmanattributes.h"
#include "gmanframebuffer.h"
#include "gmanlightsourcemgr.h"
#include "gmanmatrix4.h"
#include "gmanoptions.h"
#include "gmanparameterlist.h"
#include "gmanpathtracerenderer.h"
#include "gmanpoint.h"
#include "gmanray.h"
#include "gmanraypolygon.h"
#include "gmanraysphere.h"
#include "gmantransform.h"
#include "gmanvector.h"
#include "gmanvsperspective.h"
#include "pathtracerarealightscene.h"
#include "pathtracerscene.h"
#include "ri.h"
#include "samplingstats.h"

namespace {

constexpr RtInt kFloorRes = 81;
constexpr RtInt kFloorSamples = 64;
constexpr double kResidualFloor = 1e-4;

// Placed behind the camera's own near plane, at the same height above the
// floor as the sphere light over the floor: chooseLight's old, removed
// weight read a fixed preview point at this sphere's own object-space
// (-radius, 0, 0), whose outward normal, unrotated by this pure
// translation, stays (-1, 0, 0) in camera space -- facing away from every
// floor point whose x exceeds centreX - radius, the greater part of the
// floor's own width.
constexpr RtFloat kFarSideLe = 10.0f;
constexpr RtFloat kFarSideRadius = 1.0f;
constexpr RtFloat kFarSideCentreX = 0.0f;
constexpr RtFloat kFarSideCentreY = -2.0f;
constexpr RtFloat kFarSideCentreZ = -1.0f;
constexpr RtFloat kFarSideHeight = kFarSideCentreY - kFloorY;
constexpr RtFloat kFarSideMeasuredXMin = 2.0f;
constexpr std::size_t kMinFarSideMeasuredPixels = 100;

GMANColor analyticFarSideExpected(gman::VSPerspective& viewingSys, int px, int py) {
  constexpr int kMidGrid = 16;
  constexpr RtFloat kReflectance = 0.5f;
  double sum = 0.0;
  for (int sy = 0; sy < kMidGrid; ++sy) {
    for (int sx = 0; sx < kMidGrid; ++sx) {
      RtFloat const rx = (RtFloat)px + ((RtFloat)sx + 0.5f) / (RtFloat)kMidGrid;
      RtFloat const ry = (RtFloat)py + ((RtFloat)sy + 0.5f) / (RtFloat)kMidGrid;
      GMANRay const ray = viewingSys.cameraRay(rx, ry);
      double x, z;
      if (!intersectFloorPlane(ray, x, z)) {
        continue;
      }
      double const dx = x - (double)kFarSideCentreX;
      double const dy = (double)kFloorY - (double)kFarSideCentreY;
      double const dz = z - (double)kFarSideCentreZ;
      double const dist2 = dx * dx + dy * dy + dz * dz;
      double const dist = std::sqrt(dist2);
      sum += (double)kReflectance * (double)kFarSideLe * (double)kFarSideRadius * (double)kFarSideRadius *
             (double)kFarSideHeight / (dist2 * dist);
    }
  }
  RtFloat const value = (RtFloat)(sum / (double)(kMidGrid * kMidGrid));
  return GMANColor(value, value, value);
}

void testFarSideOfPreviewPoint() {
  GMANOptions options;
  options.setFormat(kFloorRes, kFloorRes, 1.0f);
  options.setPixelSamples(1.0f, 1.0f);
  options.setPixelFilter(RiBoxFilter, 1.0f, 1.0f);
  options.setPathtracerSamples(kFloorSamples);
  options.setBackground(GMANColor(0.0f, 0.0f, 0.0f));

  GMANMatrix4 const identity;
  gman::VSPerspective viewingSys(kFloorRes, kFloorRes, squareScreenWindow(), identity, 90.0f, 0.5f, 50.0f);

  GMANPathtraceRenderer renderer;
  GMANRayPolygon* floor = buildFloor();
  gman::Appearance floorAppearance;
  floorAppearance.shader = loadShader("matte", matteParams(1.0f));
  floorAppearance.Cs = GMANColor(0.5f, 0.5f, 0.5f);
  floorAppearance.Os = GMANColor(1.0f, 1.0f, 1.0f);
  floor->setAppearance(floorAppearance);
  renderer.getWorldManager()->add(floor);

  GMANMatrix4 place;
  place.trans(kFarSideCentreX, kFarSideCentreY, kFarSideCentreZ);
  GMANTransform const transform = makeTransform(place);
  GMANRaySphere* sphere =
      new GMANRaySphere(kFarSideRadius, -kFarSideRadius, kFarSideRadius, 360.0f, GMANParameterList(), transform);
  GMANLight const areaLight(GMAN_LIGHT_AREA, GMANColor(kFarSideLe, kFarSideLe, kFarSideLe), GMANPoint(), GMANVector());
  gman::Appearance sphereAppearance;
  sphereAppearance.areaLight = &areaLight;
  // Os 0: this check targets the light-choice weight, already proved
  // opaque by the sphere light over the floor above; transparent keeps a
  // shadow ray's own intersection precision at this closer range from
  // ever entering the residual this check reads.
  sphereAppearance.Os = GMANColor(0.0f, 0.0f, 0.0f);
  sphere->setAppearance(sphereAppearance);
  renderer.getWorldManager()->add(sphere);

  GMANFrameBuffer frameBuffer(kFloorRes, kFloorRes, options.getBackground());
  GMANAttributes const attr;
  renderer.render(&frameBuffer, &viewingSys, options, attr);
  check(renderer.droppedPathCount() == 0, "far side of the preview point: droppedPathCount() is 0");

  std::vector<double> residuals[3];
  std::vector<double> expectedByChannel[3];
  std::size_t measuredCount = 0;
  for (int py = 0; py < kFloorRes; ++py) {
    for (int px = 0; px < kFloorRes; ++px) {
      if (!pixelMeasured(viewingSys, px, py)) {
        continue;
      }
      double x, z;
      GMANRay const centreRay = viewingSys.cameraRay((RtFloat)px + 0.5f, (RtFloat)py + 0.5f);
      if (!intersectFloorPlane(centreRay, x, z) || x < (double)kFarSideMeasuredXMin) {
        continue;
      }
      ++measuredCount;
      GMANColor const expected = analyticFarSideExpected(viewingSys, px, py);
      GMANColor const actual = frameBuffer.getPixel(px, py);
      for (int c = 0; c < 3; ++c) {
        residuals[c].push_back(channel(actual, c) - channel(expected, c));
        expectedByChannel[c].push_back(channel(expected, c));
      }
    }
  }
  check(measuredCount >= kMinFarSideMeasuredPixels, "far side of the preview point: enough pixels measured");
  checkResiduals(residuals, expectedByChannel, kResidualFloor, "far side of the preview point");
}

} // namespace

int main() {
  testFarSideOfPreviewPoint();

  return checkSummary("the path tracer's own sphere light over the floor matches its closed form on the far side of "
                      "its own fixed preview point");
}
