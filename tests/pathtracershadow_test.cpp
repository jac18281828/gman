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
 * The path tracer's own shadow walk under a distant light: a two-face
 * glass pane passes (1 - F)^2, a half-opaque matte blocker passes
 * Os * (1 - Os is thin), and an opaque blocker passes nothing. The
 * composite-layer cap is tests/pathtracershadowcap_test.cpp's own.
 */

#include <algorithm>
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

constexpr RtFloat kShadowedXMin = -1.8f;
constexpr RtFloat kShadowedXMax = 1.8f;
constexpr RtFloat kShadowedZMin = 5.2f;
constexpr RtFloat kShadowedZMax = 8.8f;
constexpr std::size_t kMinShadowedPixels = 100;

constexpr double kFresnelNormal = 0.04; // exact Fresnel at normal incidence, index 1 -> 1.5

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

// Every measured pixel on the far side (x >= 3 on every corner) or near
// side (x <= -3 on every corner): unshadowed, reading the lit value.
bool pixelClearSide(gman::VSPerspective& viewingSys, int px, int py) {
  if (!pixelMeasured(viewingSys, px, py)) {
    return false;
  }
  bool allFar = true;
  bool allNear = true;
  for (int dy = 0; dy <= 1; ++dy) {
    for (int dx = 0; dx <= 1; ++dx) {
      GMANRay const corner = viewingSys.cameraRay((RtFloat)(px + dx), (RtFloat)(py + dy));
      double x, z;
      if (!intersectFloorPlane(corner, x, z)) {
        return false;
      }
      if (!(x >= 3.0)) {
        allFar = false;
      }
      if (!(x <= -3.0)) {
        allNear = false;
      }
    }
  }
  return allFar || allNear;
}

// Renders the floor under the distant light, with blockerAppearances added
// above it, once; the caller selects whichever pixel sets it needs from
// the result.
void renderScene(std::vector<gman::Appearance> const& blockerAppearances,
                 std::vector<std::vector<GMANPoint>> const& blockerVerts,
                 std::unique_ptr<GMANFrameBuffer>& frameBufferOut, std::unique_ptr<gman::VSPerspective>& viewingSysOut,
                 std::size_t& droppedOut) {
  GMANOptions options;
  options.setFormat(kRes, kRes, 1.0f);
  options.setPixelSamples(1.0f, 1.0f);
  options.setPixelFilter(RiBoxFilter, 1.0f, 1.0f);
  options.setPathtracerSamples(kSamples);

  GMANMatrix4 const identity;
  viewingSysOut.reset(new gman::VSPerspective(kRes, kRes, squareScreenWindow(), identity, 90.0f, 0.5f, 50.0f));

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

  for (std::size_t i = 0; i < blockerVerts.size(); ++i) {
    GMANRayPolygon* blocker = new GMANRayPolygon(blockerVerts[i], GMANParameterList());
    gman::Appearance appearance = blockerAppearances[i];
    appearance.lights = {&light};
    blocker->setAppearance(appearance);
    renderer.getWorldManager()->add(blocker);
  }

  frameBufferOut.reset(new GMANFrameBuffer(kRes, kRes, options.getBackground()));
  GMANAttributes const attr;
  renderer.render(frameBufferOut.get(), viewingSysOut.get(), options, attr);
  droppedOut = renderer.droppedPathCount();
}

// Selects pixels from a rendered scene, appending the finite value at each
// into valuesByChannel and counting how many were selected.
void selectPixels(gman::VSPerspective& viewingSys, GMANFrameBuffer& frameBuffer,
                  bool (*select)(gman::VSPerspective&, int, int), std::vector<double> valuesByChannel[3],
                  std::size_t& selectedCount) {
  selectedCount = 0;
  for (int py = 0; py < kRes; ++py) {
    for (int px = 0; px < kRes; ++px) {
      if (!select(viewingSys, px, py)) {
        continue;
      }
      ++selectedCount;
      GMANColor const p = frameBuffer.getPixel(px, py);
      valuesByChannel[0].push_back(p.getRed());
      valuesByChannel[1].push_back(p.getGreen());
      valuesByChannel[2].push_back(p.getBlue());
    }
  }
}

void checkAgainstConstant(std::vector<double> values[3], double expected, double relativeFloor,
                          std::string const& label) {
  char const* const channelName[3] = {"red", "green", "blue"};
  for (int c = 0; c < 3; ++c) {
    GmanMeanStderr const stat = meanStderr(values[c]);
    std::printf("%s %s: mean %.6f, expected %.6f\n", label.c_str(), channelName[c], stat.mean, expected);
    checkNear(stat.mean, expected, stat.stderrOfMean, relativeFloor * expected,
              label + ": the " + channelName[c] + " channel's mean is within 5 sigma of " + std::to_string(expected));
  }
}

std::vector<GMANPoint> rectAt(RtFloat y, bool reversedWinding) {
  std::vector<GMANPoint> verts = {GMANPoint(kBlockerXMin, y, kBlockerZMin), GMANPoint(kBlockerXMin, y, kBlockerZMax),
                                  GMANPoint(kBlockerXMax, y, kBlockerZMax), GMANPoint(kBlockerXMax, y, kBlockerZMin)};
  if (reversedWinding) {
    std::reverse(verts.begin(), verts.end());
  }
  return verts;
}

// The pane: two opposite-wound glass polygons, Os 1, rendered once; the
// shadowed and clear-side pixel sets both come from that one render.
void testGlassPane() {
  gman::Appearance glassAppearance;
  glassAppearance.shader = loadShader("glass", GMANParameterList());
  glassAppearance.Os = GMANColor(1.0f, 1.0f, 1.0f);

  std::vector<gman::Appearance> const appearances = {glassAppearance, glassAppearance};
  std::vector<std::vector<GMANPoint>> const verts = {rectAt(kBlockerY, false), rectAt(kBlockerY + 0.02f, true)};

  std::unique_ptr<GMANFrameBuffer> frameBuffer;
  std::unique_ptr<gman::VSPerspective> viewingSys;
  std::size_t dropped = 0;
  renderScene(appearances, verts, frameBuffer, viewingSys, dropped);
  check(dropped == 0, "glass pane: droppedPathCount() is 0");

  std::vector<double> shadowed[3];
  std::size_t shadowedCount = 0;
  selectPixels(*viewingSys, *frameBuffer, pixelShadowed, shadowed, shadowedCount);
  check(shadowedCount >= kMinShadowedPixels, "glass pane: at least 100 shadowed pixels");

  double const expected = kExpectedLit * (1.0 - kFresnelNormal) * (1.0 - kFresnelNormal);
  checkAgainstConstant(shadowed, expected, 2e-3, "glass pane, shadowed");

  std::vector<double> clear[3];
  std::size_t clearCount = 0;
  selectPixels(*viewingSys, *frameBuffer, pixelClearSide, clear, clearCount);
  check(clearCount >= 1, "glass pane: at least one clear-side pixel measured");
  checkAgainstConstant(clear, kExpectedLit, 2e-3, "glass pane, clear side");
}

// A half-opaque blocker: matte, Cs 0, Os 0.25.
void testHalfOpaqueBlocker() {
  gman::Appearance appearance;
  appearance.shader = loadShader("matte", matteParams(1.0f));
  appearance.Cs = GMANColor(0.0f, 0.0f, 0.0f);
  appearance.Os = GMANColor(0.25f, 0.25f, 0.25f);

  std::vector<gman::Appearance> const appearances = {appearance};
  std::vector<std::vector<GMANPoint>> const verts = {rectAt(kBlockerY, false)};

  std::unique_ptr<GMANFrameBuffer> frameBuffer;
  std::unique_ptr<gman::VSPerspective> viewingSys;
  std::size_t dropped = 0;
  renderScene(appearances, verts, frameBuffer, viewingSys, dropped);
  check(dropped == 0, "half-opaque blocker: droppedPathCount() is 0");

  std::vector<double> shadowed[3];
  std::size_t shadowedCount = 0;
  selectPixels(*viewingSys, *frameBuffer, pixelShadowed, shadowed, shadowedCount);
  check(shadowedCount >= kMinShadowedPixels, "half-opaque blocker: at least 100 shadowed pixels");

  double const expected = kExpectedLit * 0.75;
  checkAgainstConstant(shadowed, expected, 1e-4, "half-opaque blocker");
}

// An opaque blocker: the same at Os 1.
void testOpaqueBlocker() {
  gman::Appearance appearance;
  appearance.shader = loadShader("matte", matteParams(1.0f));
  appearance.Cs = GMANColor(0.0f, 0.0f, 0.0f);
  appearance.Os = GMANColor(1.0f, 1.0f, 1.0f);

  std::vector<gman::Appearance> const appearances = {appearance};
  std::vector<std::vector<GMANPoint>> const verts = {rectAt(kBlockerY, false)};

  std::unique_ptr<GMANFrameBuffer> frameBuffer;
  std::unique_ptr<gman::VSPerspective> viewingSys;
  std::size_t dropped = 0;
  renderScene(appearances, verts, frameBuffer, viewingSys, dropped);
  check(dropped == 0, "opaque blocker: droppedPathCount() is 0");

  std::vector<double> shadowed[3];
  std::size_t shadowedCount = 0;
  selectPixels(*viewingSys, *frameBuffer, pixelShadowed, shadowed, shadowedCount);
  check(shadowedCount >= kMinShadowedPixels, "opaque blocker: at least 100 shadowed pixels");

  for (int c = 0; c < 3; ++c) {
    GmanMeanStderr const stat = meanStderr(shadowed[c]);
    std::printf("opaque blocker channel %d: mean %.8f\n", c, stat.mean);
    check(std::fabs(stat.mean) <= 1e-6, "opaque blocker: shadowed pixels read 0 within 1e-6");
  }
}

} // namespace

int main() {
  testGlassPane();
  testHalfOpaqueBlocker();
  testOpaqueBlocker();

  return checkSummary("the path tracer's own shadow walk: a glass pane passes (1 - F)^2, a half-opaque blocker "
                      "passes Os, and an opaque blocker passes nothing");
}
