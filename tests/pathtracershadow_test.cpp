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
 * Os * (1 - Os is thin), an opaque blocker passes nothing, and a stack of
 * 16 zero-opacity blockers still passes light while a 17th -- past
 * gman::kMaxCompositeLayers -- reads opaque.
 */

#include <algorithm>
#include <cmath>
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
#include "gmanshaderenvironment.h"
#include "gmansurfaceshader.h"
#include "gmanvector.h"
#include "gmanvsperspective.h"
#include "ri.h"
#include "samplingstats.h"

namespace {

constexpr RtInt kRes = 81;
constexpr RtFloat kReflectance = 0.5f; // Kd = 1, Cs = 0.5
constexpr RtInt kSamples = 64;

constexpr RtFloat kFloorXMin = -8.0f;
constexpr RtFloat kFloorXMax = 8.0f;
constexpr RtFloat kFloorY = -4.0f;
constexpr RtFloat kFloorZMin = 1.0f;
constexpr RtFloat kFloorZMax = 17.0f;

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

constexpr RtFloat kGlassIor = 1.5f;
constexpr double kFresnelNormal = 0.04; // exact Fresnel at normal incidence, index 1 -> 1.5

class LambertShader : public GMANSurfaceShader {
public:
  explicit LambertShader(GMANColor const& reflectance) : reflectance(reflectance) {}
  GMANColor computeCi(GMANSurfaceEnv const&) const override { return GMANColor(0.0f, 0.0f, 0.0f); }
  GMANColor computeOi(GMANSurfaceEnv const& se) const override { return se.Os; }
  gman::BSDF bsdf(GMANSurfaceEnv const& se) const override {
    gman::BSDF closure(se.N);
    closure.addLambert(reflectance);
    return closure;
  }

private:
  GMANColor reflectance;
};

// A smooth dielectric interface of eta kGlassIor, weight white: the same
// closure shaders/gmanglass.cpp builds, reproduced here so this file needs
// no shader plugin.
class GlassShader : public GMANSurfaceShader {
public:
  GMANColor computeCi(GMANSurfaceEnv const&) const override { return GMANColor(0.0f, 0.0f, 0.0f); }
  GMANColor computeOi(GMANSurfaceEnv const& se) const override { return se.Os; }
  gman::BSDF bsdf(GMANSurfaceEnv const& se) const override {
    gman::BSDF closure(se.N);
    closure.addDielectric(GMANColor(1.0f, 1.0f, 1.0f), kGlassIor);
    return closure;
  }
};

GMANOptions::ScreenWindowStruct squareScreenWindow() {
  GMANOptions::ScreenWindowStruct sw;
  sw.left = -1.0f;
  sw.right = 1.0f;
  sw.bottom = -1.0f;
  sw.top = 1.0f;
  return sw;
}

std::shared_ptr<GMANSurfaceShader const> asAppearanceShader(GMANSurfaceShader const& shader) {
  return std::shared_ptr<GMANSurfaceShader const>(&shader, [](GMANSurfaceShader const*) {});
}

GMANRayPolygon* buildFloor() {
  std::vector<GMANPoint> const verts = {
      GMANPoint(kFloorXMin, kFloorY, kFloorZMin), GMANPoint(kFloorXMin, kFloorY, kFloorZMax),
      GMANPoint(kFloorXMax, kFloorY, kFloorZMax), GMANPoint(kFloorXMax, kFloorY, kFloorZMin)};
  return new GMANRayPolygon(verts, GMANParameterList());
}

bool intersectFloorPlane(GMANRay const& ray, double& outX, double& outZ) {
  double const oy = (double)ray.getOrigin().getY();
  double const dy = (double)ray.getDirection().getY();
  if (dy == 0.0) {
    return false;
  }
  double const t = ((double)kFloorY - oy) / dy;
  if (!(t > 0.0)) {
    return false;
  }
  outX = (double)ray.getOrigin().getX() + t * (double)ray.getDirection().getX();
  outZ = (double)ray.getOrigin().getZ() + t * (double)ray.getDirection().getZ();
  return true;
}

bool withinRect(double x, double z, double xmin, double xmax, double zmin, double zmax, double margin) {
  return x >= xmin + margin && x <= xmax - margin && z >= zmin + margin && z <= zmax - margin;
}

constexpr double kEdgeMargin = 1.0;

// A measured pixel: its cell's four corners all hit the floor, clear of
// its own edge.
bool pixelMeasured(gman::VSPerspective& viewingSys, int px, int py) {
  for (int dy = 0; dy <= 1; ++dy) {
    for (int dx = 0; dx <= 1; ++dx) {
      GMANRay const corner = viewingSys.cameraRay((RtFloat)(px + dx), (RtFloat)(py + dy));
      double x, z;
      if (!intersectFloorPlane(corner, x, z) ||
          !withinRect(x, z, kFloorXMin, kFloorXMax, kFloorZMin, kFloorZMax, kEdgeMargin)) {
        return false;
      }
      // Camera rays that reach the floor stay below y = 0, so none
      // crosses the blocker (at kBlockerY, well above the eye).
      if (!(corner.getOrigin().getY() < 0.0f || corner.getDirection().getY() < 0.0f)) {
        return false;
      }
    }
  }
  return true;
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
      intersectFloorPlane(corner, x, z);
      if (!withinRect(x, z, kShadowedXMin, kShadowedXMax, kShadowedZMin, kShadowedZMax, 0.05)) {
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
      intersectFloorPlane(corner, x, z);
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

// Renders the floor under the distant light, with blockerAppearances
// added above it, and collects the finite value at pixels selected by
// select, plus the dropped-path count.
void renderAndSelect(std::vector<gman::Appearance> const& blockerAppearances,
                     std::vector<std::vector<GMANPoint>> const& blockerVerts,
                     bool (*select)(gman::VSPerspective&, int, int), std::vector<double> valuesByChannel[3],
                     std::size_t& selectedCount, std::size_t& droppedOut) {
  GMANOptions options;
  options.setFormat(kRes, kRes, 1.0f);
  options.setPixelSamples(1.0f, 1.0f);
  options.setPixelFilter(RiBoxFilter, 1.0f, 1.0f);
  options.setPathtracerSamples(kSamples);

  GMANMatrix4 const identity;
  gman::VSPerspective viewingSys(kRes, kRes, squareScreenWindow(), identity, 90.0f, 0.5f, 50.0f);

  GMANLight const light(GMAN_LIGHT_DISTANT, GMANColor(kLightIntensity, kLightIntensity, kLightIntensity), GMANPoint(),
                        GMANVector(0.0f, -1.0f, 0.0f));
  LambertShader const floorShader(GMANColor(kReflectance, kReflectance, kReflectance));

  GMANPathtraceRenderer renderer;
  GMANRayPolygon* floor = buildFloor();
  gman::Appearance floorAppearance;
  floorAppearance.shader = asAppearanceShader(floorShader);
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

  GMANFrameBuffer frameBuffer(kRes, kRes, options.getBackground());
  GMANAttributes const attr;
  renderer.render(&frameBuffer, &viewingSys, options, attr);
  droppedOut = renderer.droppedPathCount();

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

// ---- The pane: two opposite-wound glass polygons, Os 1 ----
void testGlassPane() {
  GlassShader const glass;
  gman::Appearance glassAppearance;
  glassAppearance.shader = asAppearanceShader(glass);
  glassAppearance.Os = GMANColor(1.0f, 1.0f, 1.0f);

  std::vector<gman::Appearance> const appearances = {glassAppearance, glassAppearance};
  std::vector<std::vector<GMANPoint>> const verts = {rectAt(kBlockerY, false), rectAt(kBlockerY + 0.02f, true)};

  std::vector<double> shadowed[3];
  std::size_t shadowedCount = 0;
  std::size_t dropped = 0;
  renderAndSelect(appearances, verts, pixelShadowed, shadowed, shadowedCount, dropped);
  check(dropped == 0, "glass pane: droppedPathCount() is 0");
  check(shadowedCount >= kMinShadowedPixels, "glass pane: at least 100 shadowed pixels");

  double const expected = kExpectedLit * (1.0 - kFresnelNormal) * (1.0 - kFresnelNormal);
  checkAgainstConstant(shadowed, expected, 2e-3, "glass pane, shadowed");

  std::vector<double> clear[3];
  std::size_t clearCount = 0;
  renderAndSelect(appearances, verts, pixelClearSide, clear, clearCount, dropped);
  check(clearCount >= 1, "glass pane: at least one clear-side pixel measured");
  checkAgainstConstant(clear, kExpectedLit, 2e-3, "glass pane, clear side");
}

// ---- A half-opaque blocker: matte, Cs 0, Os 0.25 ----
void testHalfOpaqueBlocker() {
  LambertShader const blockerShader(GMANColor(0.0f, 0.0f, 0.0f));
  gman::Appearance appearance;
  appearance.shader = asAppearanceShader(blockerShader);
  appearance.Os = GMANColor(0.25f, 0.25f, 0.25f);

  std::vector<gman::Appearance> const appearances = {appearance};
  std::vector<std::vector<GMANPoint>> const verts = {rectAt(kBlockerY, false)};

  std::vector<double> shadowed[3];
  std::size_t shadowedCount = 0;
  std::size_t dropped = 0;
  renderAndSelect(appearances, verts, pixelShadowed, shadowed, shadowedCount, dropped);
  check(dropped == 0, "half-opaque blocker: droppedPathCount() is 0");
  check(shadowedCount >= kMinShadowedPixels, "half-opaque blocker: at least 100 shadowed pixels");

  double const expected = kExpectedLit * 0.75;
  checkAgainstConstant(shadowed, expected, 1e-4, "half-opaque blocker");
}

// ---- An opaque blocker: the same at Os 1 ----
void testOpaqueBlocker() {
  LambertShader const blockerShader(GMANColor(0.0f, 0.0f, 0.0f));
  gman::Appearance appearance;
  appearance.shader = asAppearanceShader(blockerShader);
  appearance.Os = GMANColor(1.0f, 1.0f, 1.0f);

  std::vector<gman::Appearance> const appearances = {appearance};
  std::vector<std::vector<GMANPoint>> const verts = {rectAt(kBlockerY, false)};

  std::vector<double> shadowed[3];
  std::size_t shadowedCount = 0;
  std::size_t dropped = 0;
  renderAndSelect(appearances, verts, pixelShadowed, shadowed, shadowedCount, dropped);
  check(dropped == 0, "opaque blocker: droppedPathCount() is 0");
  check(shadowedCount >= kMinShadowedPixels, "opaque blocker: at least 100 shadowed pixels");

  for (int c = 0; c < 3; ++c) {
    GmanMeanStderr const stat = meanStderr(shadowed[c]);
    std::printf("opaque blocker channel %d: mean %.8f\n", c, stat.mean);
    check(std::fabs(stat.mean) <= 1e-6, "opaque blocker: shadowed pixels read 0 within 1e-6");
  }
}

// ---- The cap: 16 black Os-0 blockers stacked 0.1 apart still pass
// light; 17 read black ----
void testCompositeCap() {
  LambertShader const blockerShader(GMANColor(0.0f, 0.0f, 0.0f));
  gman::Appearance appearance;
  appearance.shader = asAppearanceShader(blockerShader);
  appearance.Os = GMANColor(0.0f, 0.0f, 0.0f);

  std::vector<gman::Appearance> appearances16;
  std::vector<std::vector<GMANPoint>> verts16;
  for (int i = 0; i < gman::kMaxCompositeLayers; ++i) {
    appearances16.push_back(appearance);
    verts16.push_back(rectAt(kBlockerY + (RtFloat)i * 0.1f, false));
  }

  std::vector<double> shadowed16[3];
  std::size_t shadowedCount16 = 0;
  std::size_t dropped16 = 0;
  renderAndSelect(appearances16, verts16, pixelShadowed, shadowed16, shadowedCount16, dropped16);
  check(dropped16 == 0, "cap at 16: droppedPathCount() is 0");
  check(shadowedCount16 >= kMinShadowedPixels, "cap at 16: at least 100 shadowed pixels");
  checkAgainstConstant(shadowed16, kExpectedLit, 1e-4, "cap at 16");

  std::vector<gman::Appearance> appearances17 = appearances16;
  std::vector<std::vector<GMANPoint>> verts17 = verts16;
  appearances17.push_back(appearance);
  verts17.push_back(rectAt(kBlockerY + 16.0f * 0.1f, false));

  std::vector<double> shadowed17[3];
  std::size_t shadowedCount17 = 0;
  std::size_t dropped17 = 0;
  renderAndSelect(appearances17, verts17, pixelShadowed, shadowed17, shadowedCount17, dropped17);
  check(dropped17 == 0, "cap at 17: droppedPathCount() is 0");
  check(shadowedCount17 >= kMinShadowedPixels, "cap at 17: at least 100 shadowed pixels");
  for (int c = 0; c < 3; ++c) {
    GmanMeanStderr const stat = meanStderr(shadowed17[c]);
    check(std::fabs(stat.mean) <= 1e-6, "cap at 17: shadowed pixels read 0 within 1e-6");
  }
}

} // namespace

int main() {
  testGlassPane();
  testHalfOpaqueBlocker();
  testOpaqueBlocker();
  testCompositeCap();

  return checkSummary("the path tracer's own shadow walk: a glass pane passes (1 - F)^2, a half-opaque blocker "
                      "passes Os, an opaque blocker passes nothing, and the composite-layer cap ends at 16");
}
