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
 * Regression against the delta-light integrator this unit's emitter wiring
 * replaced: the furnace, two lights summed and the light-free environment
 * all reproduce their own previously pinned means and sigma bounds,
 * unmoved by choosing and sampling every light -- delta or area -- through
 * the shared gman::Emitter interface. One light at a time is
 * pathtracerarealightregressiononelight_test.cpp's own: three renders at
 * this file's own resolution and sample count cost enough under a
 * sanitizer build to need a second file of their own.
 */

#include <cstdio>
#include <string>
#include <vector>

#include "check.h"
#include "gmanattributes.h"
#include "gmanframebuffer.h"
#include "gmanlightsourcemgr.h"
#include "gmanoptions.h"
#include "gmanparameterlist.h"
#include "gmanpathtracerenderer.h"
#include "gmanpoint.h"
#include "gmanraypolygon.h"
#include "gmanraysphere.h"
#include "gmanvector.h"
#include "gmanvsperspective.h"
#include "pathtracerscene.h"
#include "ri.h"
#include "samplingstats.h"

namespace {

constexpr std::size_t kMinMeasuredPixels = 400;

// ---- C.1: the furnace ----

constexpr RtInt kFurnaceRes = 16;
constexpr RtInt kFurnaceSamples = 16;
constexpr RtFloat kFurnaceSphereRadius = 10.0f;
constexpr RtFloat kFurnaceReflectance = 0.75f;
constexpr RtFloat kFurnaceIntensity = 100.0f;
constexpr double kFurnaceExpected = 3.0; // rho * I / (R^2 * (1 - rho))
constexpr double kFurnaceFloor = 3e-4;

void renderFurnace(std::vector<double> outChannels[3], std::size_t& droppedOut) {
  GMANOptions options;
  options.setFormat(kFurnaceRes, kFurnaceRes, 1.0f);
  options.setPixelSamples(1.0f, 1.0f);
  options.setPixelFilter(RiBoxFilter, 1.0f, 1.0f);
  options.setPathtracerSamples(kFurnaceSamples);

  GMANMatrix4 const identity;
  gman::VSPerspective viewingSys(kFurnaceRes, kFurnaceRes, squareScreenWindow(), identity, 90.0f, 0.5f, 50.0f);

  GMANPathtraceRenderer renderer;
  GMANRaySphere* sphere =
      new GMANRaySphere(kFurnaceSphereRadius, -kFurnaceSphereRadius, kFurnaceSphereRadius, 360.0f, GMANParameterList());
  GMANLight const light(GMAN_LIGHT_POINT, GMANColor(kFurnaceIntensity, kFurnaceIntensity, kFurnaceIntensity),
                        GMANPoint(0.0f, 0.0f, 0.0f), GMANVector());
  gman::Appearance appearance;
  appearance.shader = loadShader("matte", matteParams(1.0f));
  appearance.Cs = GMANColor(kFurnaceReflectance, kFurnaceReflectance, kFurnaceReflectance);
  appearance.Os = GMANColor(1.0f, 1.0f, 1.0f);
  appearance.lights = {&light};
  sphere->setAppearance(appearance);
  renderer.getWorldManager()->add(sphere);

  GMANFrameBuffer frameBuffer(kFurnaceRes, kFurnaceRes, options.getBackground());
  GMANAttributes const attr;
  renderer.render(&frameBuffer, &viewingSys, options, attr);

  for (int y = 0; y < kFurnaceRes; ++y) {
    for (int x = 0; x < kFurnaceRes; ++x) {
      GMANColor const p = frameBuffer.getPixel(x, y);
      outChannels[0].push_back(p.getRed());
      outChannels[1].push_back(p.getGreen());
      outChannels[2].push_back(p.getBlue());
    }
  }
  droppedOut = renderer.droppedPathCount();
}

void testFurnace() {
  std::vector<double> byChannel[3];
  std::size_t dropped = 0;
  renderFurnace(byChannel, dropped);
  check(dropped == 0, "furnace: droppedPathCount() is 0");

  char const* const channelName[3] = {"red", "green", "blue"};
  for (int c = 0; c < 3; ++c) {
    GmanMeanStderr const stat = meanStderr(byChannel[c]);
    std::printf("furnace %s channel: mean %.6f, stderr %.6f\n", channelName[c], stat.mean, stat.stderrOfMean);
    checkNear(stat.mean, kFurnaceExpected, stat.stderrOfMean, kFurnaceFloor,
              std::string("furnace: the ") + channelName[c] + " channel's mean is within 5 sigma of 3");
  }
}

// ---- C.4: two point lights summed ----

void testTwoLights() {
  GMANLight const lightA(GMAN_LIGHT_POINT, GMANColor(4.0f, 4.0f, 4.0f), GMANPoint(-1.5f, -2.0f, 7.0f), GMANVector());
  GMANLight const lightB(GMAN_LIGHT_POINT, GMANColor(2.0f, 2.0f, 2.0f), GMANPoint(2.0f, -3.0f, 10.0f), GMANVector());
  std::vector<GMANLight const*> const lights = {&lightA, &lightB};

  std::vector<double> residuals[3];
  std::vector<double> expected[3];
  std::size_t measured = 0;
  std::size_t dropped = 0;
  renderAndCollectResiduals(lights, residuals, expected, measured, dropped);
  check(dropped == 0, "two lights: droppedPathCount() is 0");
  check(measured >= kMinMeasuredPixels, "two lights: at least 400 pixels measured");
  checkResiduals(residuals, expected, 1e-4, "two lights");
}

// ---- D.2: the environment, no light at all ----

constexpr RtInt kEnvironmentRes = 81;
constexpr RtInt kEnvironmentSamples = 16;
constexpr RtFloat kEnvironmentReflectance = 0.5f;
GMANColor const kEnvironmentBackground(0.2f, 0.4f, 0.6f);
constexpr double kEnvironmentFloor = 1e-5;

void testEnvironment() {
  GMANOptions options;
  options.setFormat(kEnvironmentRes, kEnvironmentRes, 1.0f);
  options.setPixelSamples(1.0f, 1.0f);
  options.setPixelFilter(RiBoxFilter, 1.0f, 1.0f);
  options.setPathtracerSamples(kEnvironmentSamples);
  options.setBackground(kEnvironmentBackground);

  GMANMatrix4 const identity;
  gman::VSPerspective viewingSys(kEnvironmentRes, kEnvironmentRes, squareScreenWindow(), identity, 90.0f, 0.5f, 50.0f);

  GMANPathtraceRenderer renderer;
  GMANRayPolygon* floor = buildFloor();
  gman::Appearance appearance;
  appearance.shader = loadShader("matte", matteParams(1.0f));
  appearance.Cs = GMANColor(kEnvironmentReflectance, kEnvironmentReflectance, kEnvironmentReflectance);
  appearance.Os = GMANColor(1.0f, 1.0f, 1.0f);
  floor->setAppearance(appearance);
  renderer.getWorldManager()->add(floor);

  GMANFrameBuffer frameBuffer(kEnvironmentRes, kEnvironmentRes, options.getBackground());
  GMANAttributes const attr;
  renderer.render(&frameBuffer, &viewingSys, options, attr);
  check(renderer.droppedPathCount() == 0, "environment: droppedPathCount() is 0");

  GMANColor const expected(kEnvironmentReflectance * kEnvironmentBackground.getRed(),
                           kEnvironmentReflectance * kEnvironmentBackground.getGreen(),
                           kEnvironmentReflectance * kEnvironmentBackground.getBlue());

  std::vector<double> residuals[3];
  std::size_t measured = 0;
  for (int py = 0; py < kEnvironmentRes; ++py) {
    for (int px = 0; px < kEnvironmentRes; ++px) {
      if (!pixelMeasured(viewingSys, px, py)) {
        continue;
      }
      ++measured;
      GMANColor const p = frameBuffer.getPixel(px, py);
      for (int c = 0; c < 3; ++c) {
        residuals[c].push_back(channel(p, c) - channel(expected, c));
      }
    }
  }
  check(measured >= kMinMeasuredPixels, "environment: at least 400 pixels measured");

  char const* const channelName[3] = {"red", "green", "blue"};
  for (int c = 0; c < 3; ++c) {
    GmanMeanStderr const stat = meanStderr(residuals[c]);
    checkNear(stat.mean, 0.0, stat.stderrOfMean, kEnvironmentFloor,
              std::string("environment: the ") + channelName[c] + " channel's residual mean is within 5 sigma of 0");
  }
}

} // namespace

int main() {
  testFurnace();
  testTwoLights();
  testEnvironment();

  return checkSummary("the delta-light integrator's own pinned means and sigma bounds hold unmoved: the furnace, "
                      "two lights summed and the light-free environment");
}
