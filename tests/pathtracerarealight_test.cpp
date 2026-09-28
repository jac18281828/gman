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
 * The path tracer's own area-light behaviour: a sphere light over the
 * shared floor scene checked against its closed-form irradiance, and a
 * smoke check that droppedPathCount and alpha still hold on that render.
 */

#include <cmath>
#include <memory>
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

constexpr RtFloat kSphereRadius = 1.0f;
constexpr RtFloat kSphereCentreX = 10.0f;
constexpr RtFloat kSphereCentreY = -2.0f;
constexpr RtFloat kSphereCentreZ = 9.0f;
constexpr RtFloat kSphereHeight = kSphereCentreY - kFloorY; // 2: sphere centre's height above the floor
constexpr RtInt kFloorRes = 81;
constexpr RtInt kFloorSamples = 64;
constexpr std::size_t kMinMeasuredPixels = 400;
constexpr double kResidualFloor = 1e-4;

// The mean, over a 16 x 16 midpoint grid of pixel (px, py)'s cell, of the
// equivalent-point-source formula for a Lambertian sphere light: rho * Le *
// radius^2 * h / d^3, d the distance from the grid point to the sphere's
// centre. Mirrors analyticExpected's own grid technique for a delta light.
GMANColor analyticAreaLightExpected(gman::VSPerspective& viewingSys, int px, int py) {
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
      double const dx = x - (double)kSphereCentreX;
      double const dy = (double)kFloorY - (double)kSphereCentreY;
      double const dz = z - (double)kSphereCentreZ;
      double const dist2 = dx * dx + dy * dy + dz * dz;
      double const dist = std::sqrt(dist2);
      sum += (double)kReflectance * (double)kAreaLe * (double)kSphereRadius * (double)kSphereRadius *
             (double)kSphereHeight / (dist2 * dist);
    }
  }
  RtFloat const value = (RtFloat)(sum / (double)(kMidGrid * kMidGrid));
  return GMANColor(value, value, value);
}

// The floor under one emitting sphere, off to the side at a height and
// distance no floor camera ray can pass within the sphere's own radius of
// (a geometric argument on the sphere's own placement): renders once,
// for both the residual check and the smoke check below to share.
void renderAreaLightFloor(std::unique_ptr<GMANFrameBuffer>& frameBufferOut,
                          std::unique_ptr<gman::VSPerspective>& viewingSysOut, std::size_t& droppedOut) {
  GMANOptions options;
  options.setFormat(kFloorRes, kFloorRes, 1.0f);
  options.setPixelSamples(1.0f, 1.0f);
  options.setPixelFilter(RiBoxFilter, 1.0f, 1.0f);
  options.setPathtracerSamples(kFloorSamples);
  options.setBackground(GMANColor(0.0f, 0.0f, 0.0f));

  GMANMatrix4 const identity;
  viewingSysOut.reset(
      new gman::VSPerspective(kFloorRes, kFloorRes, squareScreenWindow(), identity, 90.0f, 0.5f, 50.0f));

  GMANPathtraceRenderer renderer;
  GMANRayPolygon* floor = buildFloor();
  gman::Appearance floorAppearance;
  floorAppearance.shader = loadShader("matte", matteParams(1.0f));
  floorAppearance.Cs = GMANColor(0.5f, 0.5f, 0.5f);
  floorAppearance.Os = GMANColor(1.0f, 1.0f, 1.0f);
  floor->setAppearance(floorAppearance);
  renderer.getWorldManager()->add(floor);

  GMANMatrix4 place;
  place.trans(kSphereCentreX, kSphereCentreY, kSphereCentreZ);
  GMANTransform const transform = makeTransform(place);
  GMANRaySphere* sphere =
      new GMANRaySphere(kSphereRadius, -kSphereRadius, kSphereRadius, 360.0f, GMANParameterList(), transform);
  GMANLight const areaLight(GMAN_LIGHT_AREA, GMANColor(kAreaLe, kAreaLe, kAreaLe), GMANPoint(), GMANVector());
  gman::Appearance sphereAppearance;
  sphereAppearance.areaLight = &areaLight;
  // Os 1, the RIB default: opaque, so a shadow ray that walked all the way
  // to the sampled point would graze the emitter's own surface there.
  sphereAppearance.Os = GMANColor(1.0f, 1.0f, 1.0f);
  sphere->setAppearance(sphereAppearance);
  renderer.getWorldManager()->add(sphere);

  frameBufferOut.reset(new GMANFrameBuffer(kFloorRes, kFloorRes, options.getBackground()));
  GMANAttributes const attr;
  renderer.render(frameBufferOut.get(), viewingSysOut.get(), options, attr);
  droppedOut = renderer.droppedPathCount();
}

void testSphereLightOverFloor() {
  std::unique_ptr<GMANFrameBuffer> frameBuffer;
  std::unique_ptr<gman::VSPerspective> viewingSys;
  std::size_t dropped = 0;
  renderAreaLightFloor(frameBuffer, viewingSys, dropped);
  check(dropped == 0, "sphere light over floor: droppedPathCount() is 0");

  std::vector<double> residuals[3];
  std::vector<double> expectedByChannel[3];
  std::size_t measuredCount = 0;
  std::vector<double> alphaValues[3];
  for (int py = 0; py < kFloorRes; ++py) {
    for (int px = 0; px < kFloorRes; ++px) {
      if (!pixelMeasured(*viewingSys, px, py)) {
        continue;
      }
      ++measuredCount;
      GMANColor const expected = analyticAreaLightExpected(*viewingSys, px, py);
      GMANColor const actual = frameBuffer->getPixel(px, py);
      GMANAlpha const alpha = frameBuffer->getAlpha(px, py);
      for (int c = 0; c < 3; ++c) {
        residuals[c].push_back(channel(actual, c) - channel(expected, c));
        expectedByChannel[c].push_back(channel(expected, c));
        alphaValues[c].push_back(channel(alpha, c));
      }
    }
  }
  check(measuredCount >= kMinMeasuredPixels, "sphere light over floor: at least 400 measured pixels");
  checkResiduals(residuals, expectedByChannel, kResidualFloor, "sphere light over floor");

  // The floor is opaque everywhere a camera ray measures it, so every
  // measured pixel's alpha stays 1, unaffected by the area-light wiring.
  for (int c = 0; c < 3; ++c) {
    GmanMeanStderr const alphaStat = meanStderr(alphaValues[c]);
    check(std::fabs(alphaStat.mean - 1.0) <= 1e-4, "sphere light over floor: alpha stays 1");
  }
}

} // namespace

int main() {
  testSphereLightOverFloor();

  return checkSummary(
      "the path tracer's own sphere light over the floor matches its closed form, and droppedPathCount and alpha "
      "stay unaffected by the area-light wiring");
}
