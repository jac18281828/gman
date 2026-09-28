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
 * The shadow walk's own composite-layer cap: PATHTRACER_SHADOW_CAP_COUNT
 * black, zero-opacity blockers stacked 0.1 apart still pass light at
 * gman::kMaxCompositeLayers of them, and read opaque one blocker past it.
 * Built twice, at 16 and at 17, into the pathtracershadowcap16 and
 * pathtracershadowcap17 executables.
 */

#ifndef PATHTRACER_SHADOW_CAP_COUNT
#error "PATHTRACER_SHADOW_CAP_COUNT must name this build's own blocker count"
#endif

#include <cmath>
#include <cstdio>
#include <memory>
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
#include "gmanrayoccluder.h"
#include "gmanraypolygon.h"
#include "gmanvector.h"
#include "gmanvsperspective.h"
#include "pathtracerscene.h"
#include "ri.h"
#include "samplingstats.h"

namespace {

constexpr RtInt kRes = 81;
constexpr RtFloat kReflectance = 0.5f; // Kd = 1, Cs = 0.5
constexpr RtInt kSamples = 64;

constexpr RtFloat kLightIntensity = 0.8f;
constexpr double kExpectedLit = 0.4; // rho * I, cos(theta) = 1

constexpr RtFloat kBlockerY = 12.0f;
constexpr RtFloat kBlockerXMin = -2.0f;
constexpr RtFloat kBlockerXMax = 2.0f;
constexpr RtFloat kBlockerZMin = 5.0f;
constexpr RtFloat kBlockerZMax = 9.0f;
constexpr RtFloat kBlockerSpacing = 0.1f;

constexpr RtFloat kShadowedXMin = -1.8f;
constexpr RtFloat kShadowedXMax = 1.8f;
constexpr RtFloat kShadowedZMin = 5.2f;
constexpr RtFloat kShadowedZMax = 8.8f;
constexpr std::size_t kMinShadowedPixels = 100;

bool withinRect(double x, double z, double xmin, double xmax, double zmin, double zmax, double margin) {
  return x >= xmin + margin && x <= xmax - margin && z >= zmin + margin && z <= zmax - margin;
}

// A shadowed pixel: measured, and its four corner hits lie within the
// shadowed rectangle, clear of its own edge.
bool pixelShadowed(gman::VSPerspective& viewingSys, int px, int py) {
  if (!pixelMeasured(viewingSys, px, py)) {
    return false;
  }
  for (int dy = 0; dy <= 1; ++dy) {
    for (int dx = 0; dx <= 1; ++dx) {
      GMANRay const corner = viewingSys.cameraRay((RtFloat)(px + dx), (RtFloat)(py + dy));
      double x, z;
      if (!intersectFloorPlane(corner, x, z) ||
          !withinRect(x, z, kShadowedXMin, kShadowedXMax, kShadowedZMin, kShadowedZMax, 0.05)) {
        return false;
      }
    }
  }
  return true;
}

std::vector<GMANPoint> rectAt(RtFloat y) {
  return {GMANPoint(kBlockerXMin, y, kBlockerZMin), GMANPoint(kBlockerXMin, y, kBlockerZMax),
          GMANPoint(kBlockerXMax, y, kBlockerZMax), GMANPoint(kBlockerXMax, y, kBlockerZMin)};
}

// Stacks PATHTRACER_SHADOW_CAP_COUNT black, zero-opacity blockers above the
// floor, renders once under the distant light, and reports the shadowed
// pixels' mean plus the dropped-path count.
void renderCap(std::vector<double> shadowed[3], std::size_t& shadowedCount, std::size_t& droppedOut) {
  GMANOptions options;
  options.setFormat(kRes, kRes, 1.0f);
  options.setPixelSamples(1.0f, 1.0f);
  options.setPixelFilter(RiBoxFilter, 1.0f, 1.0f);
  options.setPathtracerSamples(kSamples);

  GMANMatrix4 const identity;
  gman::VSPerspective viewingSys(kRes, kRes, squareScreenWindow(), identity, 90.0f, 0.5f, 50.0f);

  GMANLight const light(GMAN_LIGHT_DISTANT, GMANColor(kLightIntensity, kLightIntensity, kLightIntensity), GMANPoint(),
                        GMANVector(0.0f, -1.0f, 0.0f));

  GMANPathtraceRenderer renderer;
  GMANRayPolygon* floor = buildFloor();
  gman::Appearance floorAppearance;
  floorAppearance.shader = loadShader("matte", matteParams(1.0f));
  floorAppearance.Cs = GMANColor(kReflectance, kReflectance, kReflectance);
  floorAppearance.Os = GMANColor(1.0f, 1.0f, 1.0f);
  floorAppearance.lights = {&light};
  floor->setAppearance(floorAppearance);
  renderer.getWorldManager()->add(floor);

  std::shared_ptr<GMANSurfaceShader const> const blockerShader = loadShader("matte", matteParams(1.0f));
  for (int i = 0; i < PATHTRACER_SHADOW_CAP_COUNT; ++i) {
    GMANRayPolygon* blocker = new GMANRayPolygon(rectAt(kBlockerY + (RtFloat)i * kBlockerSpacing), GMANParameterList());
    gman::Appearance appearance;
    appearance.shader = blockerShader;
    appearance.Cs = GMANColor(0.0f, 0.0f, 0.0f);
    appearance.Os = GMANColor(0.0f, 0.0f, 0.0f);
    appearance.lights = {&light};
    blocker->setAppearance(appearance);
    renderer.getWorldManager()->add(blocker);
  }

  GMANFrameBuffer frameBuffer(kRes, kRes, options.getBackground());
  GMANAttributes const attr;
  renderer.render(&frameBuffer, &viewingSys, options, attr);
  droppedOut = renderer.droppedPathCount();

  shadowedCount = 0;
  for (int py = 0; py < kRes; ++py) {
    for (int px = 0; px < kRes; ++px) {
      if (!pixelShadowed(viewingSys, px, py)) {
        continue;
      }
      ++shadowedCount;
      GMANColor const p = frameBuffer.getPixel(px, py);
      shadowed[0].push_back(p.getRed());
      shadowed[1].push_back(p.getGreen());
      shadowed[2].push_back(p.getBlue());
    }
  }
}

void testCap() {
  std::vector<double> shadowed[3];
  std::size_t shadowedCount = 0;
  std::size_t dropped = 0;
  renderCap(shadowed, shadowedCount, dropped);
  check(dropped == 0, "cap: droppedPathCount() is 0");
  check(shadowedCount >= kMinShadowedPixels, "cap: at least 100 shadowed pixels");

  if (PATHTRACER_SHADOW_CAP_COUNT <= gman::kMaxCompositeLayers) {
    char const* const channelName[3] = {"red", "green", "blue"};
    for (int c = 0; c < 3; ++c) {
      GmanMeanStderr const stat = meanStderr(shadowed[c]);
      std::printf("cap %s: mean %.6f, expected %.6f\n", channelName[c], stat.mean, kExpectedLit);
      checkNear(stat.mean, kExpectedLit, stat.stderrOfMean, 1e-4 * kExpectedLit,
                "cap: the " + std::string(channelName[c]) + " channel's mean is within 5 sigma of " +
                    std::to_string(kExpectedLit));
    }
  } else {
    for (int c = 0; c < 3; ++c) {
      GmanMeanStderr const stat = meanStderr(shadowed[c]);
      std::printf("cap channel %d: mean %.8f\n", c, stat.mean);
      check(std::fabs(stat.mean) <= 1e-6, "cap: shadowed pixels read 0 within 1e-6");
    }
  }
}

} // namespace

int main() {
  testCap();

  return checkSummary("the shadow walk's composite-layer cap: PATHTRACER_SHADOW_CAP_COUNT blockers still pass "
                      "light at the cap and read opaque one blocker past it");
}
