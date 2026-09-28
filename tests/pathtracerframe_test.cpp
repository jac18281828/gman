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
 * The sample buffer's own frame-level invariants: an empty scene escapes to
 * the background at every pixel; the floor scene under no light reads its
 * reflectance times the background exactly, a zero-variance case for a
 * Lambert lobe under a constant environment; coverage weights color and
 * alpha by Os as decision 9 states; depth and antialiasing read the
 * geometric hit each pixel's cell carries; a repeat and a crop reproduce a
 * render bit for bit; an unresolvable indirect pass changes nothing but the
 * log; and a non-finite escape drops its own path without darkening the
 * image by more than its own share.
 */

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "check.h"
#include "gmanattributes.h"
#include "gmanframebuffer.h"
#include "gmanlightsourcemgr.h"
#include "gmanlog.h"
#include "gmanoptions.h"
#include "gmanparameterlist.h"
#include "gmanpathtracerenderer.h"
#include "gmanpoint.h"
#include "gmanraypolygon.h"
#include "gmanraysphere.h"
#include "gmanshaderenvironment.h"
#include "gmansurfaceshader.h"
#include "gmanvector.h"
#include "gmanvsperspective.h"
#include "ri.h"
#include "samplingstats.h"

namespace {

constexpr RtInt kDefaultSamples = 16;
GMANColor const kDefaultBackground(0.2f, 0.4f, 0.6f);

constexpr RtFloat kFloorXMin = -8.0f;
constexpr RtFloat kFloorXMax = 8.0f;
constexpr RtFloat kFloorY = -4.0f;
constexpr RtFloat kFloorZMin = 1.0f;
constexpr RtFloat kFloorZMax = 17.0f;
constexpr RtInt kFloorRes = 81;
constexpr std::size_t kMinMeasuredPixels = 400;

// The ray tracer's own polygon intersection carries its own float
// tolerance, so a corner within a hair's width of the true boundary can
// miss where this test's own double-precision plane intersection says it
// should hit; matches pathtracerdirect_test.cpp's own margin.
constexpr double kEdgeMargin = 1.0;

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

bool colorExactly(GMANColor const& a, GMANColor const& b) {
  return a.getRed() == b.getRed() && a.getGreen() == b.getGreen() && a.getBlue() == b.getBlue();
}

double channel(GMANColor const& c, int i) {
  if (i == 0) {
    return c.getRed();
  }
  return i == 1 ? c.getGreen() : c.getBlue();
}

double channel(GMANAlpha const& a, int i) {
  if (i == 0) {
    return a.getRed();
  }
  return i == 1 ? a.getGreen() : a.getBlue();
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

bool withinFloor(double x, double z, double margin) {
  return x >= (double)kFloorXMin + margin && x <= (double)kFloorXMax - margin && z >= (double)kFloorZMin + margin &&
         z <= (double)kFloorZMax - margin;
}

bool pixelMeasured(gman::VSPerspective& viewingSys, int px, int py) {
  for (int dy = 0; dy <= 1; ++dy) {
    for (int dx = 0; dx <= 1; ++dx) {
      GMANRay const corner = viewingSys.cameraRay((RtFloat)(px + dx), (RtFloat)(py + dy));
      double x, z;
      if (!intersectFloorPlane(corner, x, z) || !withinFloor(x, z, kEdgeMargin)) {
        return false;
      }
    }
  }
  return true;
}

// A pixel whose cell's four corner rays all point away from the floor
// plane (a positive y direction), so no ray through it ever meets the
// floor at all.
bool pixelClearsAbove(gman::VSPerspective& viewingSys, int px, int py) {
  for (int dy = 0; dy <= 1; ++dy) {
    for (int dx = 0; dx <= 1; ++dx) {
      GMANRay const corner = viewingSys.cameraRay((RtFloat)(px + dx), (RtFloat)(py + dy));
      if (!(corner.getDirection().getY() > 0.0f)) {
        return false;
      }
    }
  }
  return true;
}

GMANPathtraceRenderer* buildFloorRenderer(LambertShader const& shader, GMANColor const& os,
                                          std::vector<GMANLight const*> const& lights) {
  GMANPathtraceRenderer* renderer = new GMANPathtraceRenderer();
  GMANRayPolygon* floor = buildFloor();
  gman::Appearance appearance;
  appearance.shader = asAppearanceShader(shader);
  appearance.Os = os;
  appearance.lights = lights;
  floor->setAppearance(appearance);
  renderer->getWorldManager()->add(floor);
  return renderer;
}

std::string readFile(std::string const& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream contents;
  contents << in.rdbuf();
  return contents.str();
}

// ---- D.1: Escape ----
void checkEscape() {
  constexpr RtInt kRes = 16;
  GMANOptions options;
  options.setFormat(kRes, kRes, 1.0f);
  options.setPixelSamples(1.0f, 1.0f);
  options.setPixelFilter(RiBoxFilter, 1.0f, 1.0f);
  options.setPathtracerSamples(kDefaultSamples);
  options.setBackground(kDefaultBackground);

  GMANMatrix4 const identity;
  gman::VSPerspective viewingSys(kRes, kRes, squareScreenWindow(), identity, 90.0f, 0.5f, 50.0f);

  GMANPathtraceRenderer renderer;
  GMANFrameBuffer frameBuffer(kRes, kRes, options.getBackground());
  GMANAttributes const attr;
  renderer.render(&frameBuffer, &viewingSys, options, attr);

  bool everyPixelBackground = true;
  bool everyAlphaZero = true;
  bool everyDepthInfinite = true;
  for (int y = 0; y < kRes; ++y) {
    for (int x = 0; x < kRes; ++x) {
      if (!colorExactly(frameBuffer.getPixel(x, y), kDefaultBackground)) {
        everyPixelBackground = false;
      }
      GMANAlpha const a = frameBuffer.getAlpha(x, y);
      if (a.getRed() != 0.0f || a.getGreen() != 0.0f || a.getBlue() != 0.0f) {
        everyAlphaZero = false;
      }
      if (renderer.getDepth(x, y) != RI_INFINITY) {
        everyDepthInfinite = false;
      }
    }
  }
  check(everyPixelBackground, "escape: every pixel equals B exactly");
  check(everyAlphaZero, "escape: every alpha is 0");
  check(everyDepthInfinite, "escape: every depth is RI_INFINITY");
}

// ---- D.2: The environment ----
void checkEnvironment() {
  RtFloat const reflectance = 0.5f;
  LambertShader const shader(GMANColor(reflectance, reflectance, reflectance));
  std::vector<GMANLight const*> const noLights;
  std::unique_ptr<GMANPathtraceRenderer> renderer(buildFloorRenderer(shader, GMANColor(1.0f, 1.0f, 1.0f), noLights));

  GMANOptions options;
  options.setFormat(kFloorRes, kFloorRes, 1.0f);
  options.setPixelSamples(1.0f, 1.0f);
  options.setPixelFilter(RiBoxFilter, 1.0f, 1.0f);
  options.setPathtracerSamples(kDefaultSamples);
  options.setBackground(kDefaultBackground);

  GMANMatrix4 const identity;
  gman::VSPerspective viewingSys(kFloorRes, kFloorRes, squareScreenWindow(), identity, 90.0f, 0.5f, 50.0f);

  GMANFrameBuffer frameBuffer(kFloorRes, kFloorRes, options.getBackground());
  GMANAttributes const attr;
  renderer->render(&frameBuffer, &viewingSys, options, attr);

  GMANColor const expected(reflectance * kDefaultBackground.getRed(), reflectance * kDefaultBackground.getGreen(),
                           reflectance * kDefaultBackground.getBlue());

  std::vector<double> residuals[3];
  std::size_t measured = 0;
  for (int py = 0; py < kFloorRes; ++py) {
    for (int px = 0; px < kFloorRes; ++px) {
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
    checkNear(stat.mean, 0.0, stat.stderrOfMean, 1e-5,
              std::string("environment: the ") + channelName[c] + " channel's residual mean is within 5 sigma of 0");
  }

  bool foundClearPixel = false;
  for (int py = 0; py < kFloorRes && !foundClearPixel; ++py) {
    for (int px = 0; px < kFloorRes && !foundClearPixel; ++px) {
      if (!pixelClearsAbove(viewingSys, px, py)) {
        continue;
      }
      foundClearPixel = true;
      check(colorExactly(frameBuffer.getPixel(px, py), kDefaultBackground),
            "environment: a pixel whose cell never meets the floor equals B exactly");
      GMANAlpha const a = frameBuffer.getAlpha(px, py);
      check(a.getRed() == 0.0f && a.getGreen() == 0.0f && a.getBlue() == 0.0f, "environment: that pixel's alpha is 0");
    }
  }
  check(foundClearPixel, "environment setup: a pixel clearing the floor entirely exists");
}

// Shared coverage-case renderer and residual check, for D.3's two cases.
void renderAndCheckCoverage(GMANColor const& cs, GMANColor const& os, std::vector<GMANLight const*> const& lights,
                            GMANColor const& background, GMANColor const& expectedColor, GMANColor const& expectedAlpha,
                            double colorFloor, std::string const& label) {
  LambertShader const shader(cs);
  std::unique_ptr<GMANPathtraceRenderer> renderer(buildFloorRenderer(shader, os, lights));

  GMANOptions options;
  options.setFormat(kFloorRes, kFloorRes, 1.0f);
  options.setPixelSamples(1.0f, 1.0f);
  options.setPixelFilter(RiBoxFilter, 1.0f, 1.0f);
  options.setPathtracerSamples(kDefaultSamples);
  options.setBackground(background);

  GMANMatrix4 const identity;
  gman::VSPerspective viewingSys(kFloorRes, kFloorRes, squareScreenWindow(), identity, 90.0f, 0.5f, 50.0f);

  GMANFrameBuffer frameBuffer(kFloorRes, kFloorRes, options.getBackground());
  GMANAttributes const attr;
  renderer->render(&frameBuffer, &viewingSys, options, attr);

  std::vector<double> colorResiduals[3];
  std::vector<double> alphaResiduals[3];
  std::size_t measured = 0;
  for (int py = 0; py < kFloorRes; ++py) {
    for (int px = 0; px < kFloorRes; ++px) {
      if (!pixelMeasured(viewingSys, px, py)) {
        continue;
      }
      ++measured;
      GMANColor const p = frameBuffer.getPixel(px, py);
      GMANAlpha const a = frameBuffer.getAlpha(px, py);
      for (int c = 0; c < 3; ++c) {
        colorResiduals[c].push_back(channel(p, c) - channel(expectedColor, c));
        alphaResiduals[c].push_back(channel(a, c) - channel(expectedAlpha, c));
      }
    }
  }
  check(measured >= kMinMeasuredPixels, label + ": at least 400 pixels measured");
  check(renderer->droppedPathCount() == 0, label + ": droppedPathCount() is 0");

  char const* const channelName[3] = {"red", "green", "blue"};
  for (int c = 0; c < 3; ++c) {
    GmanMeanStderr const colorStat = meanStderr(colorResiduals[c]);
    checkNear(colorStat.mean, 0.0, colorStat.stderrOfMean, colorFloor,
              label + ": the " + channelName[c] + " channel's colour residual is within 5 sigma of 0");
    GmanMeanStderr const alphaStat = meanStderr(alphaResiduals[c]);
    checkNear(alphaStat.mean, 0.0, alphaStat.stderrOfMean, colorFloor,
              label + ": the " + channelName[c] + " channel's alpha residual is within 5 sigma of 0");
  }
}

// ---- D.3: Coverage ----
void checkCoverageUnlit() {
  std::vector<GMANLight const*> const noLights;
  GMANColor const os(0.25f, 0.25f, 0.25f);
  GMANColor const expectedColor(0.75f * kDefaultBackground.getRed(), 0.75f * kDefaultBackground.getGreen(),
                                0.75f * kDefaultBackground.getBlue());
  renderAndCheckCoverage(GMANColor(0.0f, 0.0f, 0.0f), os, noLights, kDefaultBackground, expectedColor, os, 1e-4,
                         "coverage unlit");
}

void checkCoverageLitAndColoured() {
  GMANLight const distant(GMAN_LIGHT_DISTANT, GMANColor(0.8f, 0.8f, 0.8f), GMANPoint(), GMANVector(0.0f, -1.0f, 0.0f));
  std::vector<GMANLight const*> const lights = {&distant};
  GMANColor const os(0.1f, 0.25f, 0.5f);
  GMANColor const black(0.0f, 0.0f, 0.0f);
  GMANColor const expectedColor(0.4f * os.getRed(), 0.4f * os.getGreen(), 0.4f * os.getBlue());
  renderAndCheckCoverage(GMANColor(0.5f, 0.5f, 0.5f), os, lights, black, expectedColor, os, 1e-4,
                         "coverage lit and coloured");
}

// ---- D.4: Depth ----
void checkDepth() {
  constexpr RtInt kRes = 16;
  constexpr RtFloat kZ = 5.0f;
  std::vector<GMANPoint> const verts = {GMANPoint(-10.0f, -10.0f, kZ), GMANPoint(-10.0f, 10.0f, kZ),
                                        GMANPoint(10.0f, 10.0f, kZ), GMANPoint(10.0f, -10.0f, kZ)};
  GMANRayPolygon* wall = new GMANRayPolygon(verts, GMANParameterList());
  LambertShader const shader(GMANColor(0.5f, 0.5f, 0.5f));
  gman::Appearance appearance;
  appearance.shader = asAppearanceShader(shader);
  appearance.Os = GMANColor(1.0f, 1.0f, 1.0f);
  wall->setAppearance(appearance);

  GMANPathtraceRenderer renderer;
  renderer.getWorldManager()->add(wall);

  GMANOptions options;
  options.setFormat(kRes, kRes, 1.0f);
  options.setPixelSamples(1.0f, 1.0f);
  options.setPixelFilter(RiBoxFilter, 1.0f, 1.0f);
  options.setPathtracerSamples(kDefaultSamples);

  GMANMatrix4 const identity;
  gman::VSPerspective viewingSys(kRes, kRes, squareScreenWindow(), identity, 90.0f, 0.5f, 50.0f);

  GMANFrameBuffer frameBuffer(kRes, kRes, options.getBackground());
  GMANAttributes const attr;
  renderer.render(&frameBuffer, &viewingSys, options, attr);

  bool everyDepthNear = true;
  bool everyAlphaOne = true;
  for (int y = 0; y < kRes; ++y) {
    for (int x = 0; x < kRes; ++x) {
      if (std::fabs(renderer.getDepth(x, y) - kZ) > 1e-4f) {
        everyDepthNear = false;
      }
      GMANAlpha const a = frameBuffer.getAlpha(x, y);
      if (a.getRed() != 1.0f || a.getGreen() != 1.0f || a.getBlue() != 1.0f) {
        everyAlphaOne = false;
      }
    }
  }
  check(everyDepthNear, "depth: every getDepth is 5 within 1e-4");
  check(everyAlphaOne, "depth: every alpha is 1");
}

// ---- D.5: Antialiasing ----
void checkAntialiasing() {
  constexpr RtInt kRes = 41;
  constexpr int kX0 = kRes / 2;
  constexpr RtFloat kZ0 = 10.0f;
  RtFloat const ndcEdge = -1.0f + 2.0f * ((RtFloat)kX0 + 0.3f) / (RtFloat)kRes;
  RtFloat const xEdge = ndcEdge * kZ0;

  std::vector<GMANPoint> const verts = {GMANPoint(xEdge, -100.0f, kZ0), GMANPoint(xEdge, 100.0f, kZ0),
                                        GMANPoint(xEdge + 100.0f, 100.0f, kZ0),
                                        GMANPoint(xEdge + 100.0f, -100.0f, kZ0)};
  GMANRayPolygon* wall = new GMANRayPolygon(verts, GMANParameterList());
  LambertShader const shader(GMANColor(0.5f, 0.5f, 0.5f));
  gman::Appearance appearance;
  appearance.shader = asAppearanceShader(shader);
  appearance.Os = GMANColor(1.0f, 1.0f, 1.0f);
  wall->setAppearance(appearance);

  GMANPathtraceRenderer renderer;
  renderer.getWorldManager()->add(wall);

  GMANOptions options;
  options.setFormat(kRes, kRes, 1.0f);
  options.setPixelSamples(1.0f, 1.0f);
  options.setPixelFilter(RiBoxFilter, 1.0f, 1.0f);
  options.setPathtracerSamples(kDefaultSamples);

  GMANMatrix4 const identity;
  gman::VSPerspective viewingSys(kRes, kRes, squareScreenWindow(), identity, 90.0f, 0.5f, 50.0f);

  GMANFrameBuffer frameBuffer(kRes, kRes, options.getBackground());
  GMANAttributes const attr;
  renderer.render(&frameBuffer, &viewingSys, options, attr);

  std::vector<double> alphaValues;
  for (int y = 0; y < kRes; ++y) {
    GMANAlpha const a = frameBuffer.getAlpha(kX0, y);
    alphaValues.push_back((double)a.getRed());
  }
  check(alphaValues.size() >= 16, "antialiasing: at least 16 pixels measured along the edge's own column");

  GmanMeanStderr const stat = meanStderr(alphaValues);
  checkNear(stat.mean, 0.7, stat.stderrOfMean, 1.0 / (double)kDefaultSamples,
            "antialiasing: the edge column's mean alpha is within 5 sigma of 0.7");
}

// The two-point-light floor scene C.4 uses, at a smaller format so D.6 to
// D.8 fit within the gate's own time bound; they compare renders with
// each other, never with an analytic value.
void buildTwoLightScene(GMANPathtraceRenderer& renderer, std::vector<GMANLight const*>& lightsOut,
                        GMANLight const*& firstLight, GMANLight const*& secondLight) {
  static GMANLight const light1(GMAN_LIGHT_POINT, GMANColor(4.0f, 4.0f, 4.0f), GMANPoint(-1.5f, -2.0f, 7.0f),
                                GMANVector());
  static GMANLight const light2(GMAN_LIGHT_POINT, GMANColor(2.0f, 2.0f, 2.0f), GMANPoint(2.0f, -3.0f, 10.0f),
                                GMANVector());
  static LambertShader const shader(GMANColor(0.5f, 0.5f, 0.5f));
  firstLight = &light1;
  secondLight = &light2;
  lightsOut = {&light1, &light2};
  GMANRayPolygon* floor = buildFloor();
  gman::Appearance appearance;
  appearance.shader = asAppearanceShader(shader);
  appearance.Os = GMANColor(1.0f, 1.0f, 1.0f);
  appearance.lights = lightsOut;
  floor->setAppearance(appearance);
  renderer.getWorldManager()->add(floor);
}

constexpr RtInt kRepeatRes = 41;
constexpr RtInt kRepeatSamples = 16;

GMANOptions twoLightOptions() {
  GMANOptions options;
  options.setFormat(kRepeatRes, kRepeatRes, 1.0f);
  options.setPixelSamples(1.0f, 1.0f);
  options.setPixelFilter(RiBoxFilter, 1.0f, 1.0f);
  options.setPathtracerSamples(kRepeatSamples);
  return options;
}

bool framesMatch(GMANFrameBuffer const& a, GMANFrameBuffer const& b, int width, int height) {
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      if (!colorExactly(a.getPixel(x, y), b.getPixel(x, y))) {
        return false;
      }
      GMANAlpha const aa = a.getAlpha(x, y);
      GMANAlpha const ba = b.getAlpha(x, y);
      if (aa.getRed() != ba.getRed() || aa.getGreen() != ba.getGreen() || aa.getBlue() != ba.getBlue()) {
        return false;
      }
    }
  }
  return true;
}

// ---- D.6: Repeat ----
void checkRepeat() {
  GMANMatrix4 const identity;
  gman::VSPerspective viewingSysA(kRepeatRes, kRepeatRes, squareScreenWindow(), identity, 90.0f, 0.5f, 50.0f);
  gman::VSPerspective viewingSysB(kRepeatRes, kRepeatRes, squareScreenWindow(), identity, 90.0f, 0.5f, 50.0f);

  GMANPathtraceRenderer rendererA;
  std::vector<GMANLight const*> lightsA;
  GMANLight const *l1a, *l2a;
  buildTwoLightScene(rendererA, lightsA, l1a, l2a);

  GMANPathtraceRenderer rendererB;
  std::vector<GMANLight const*> lightsB;
  GMANLight const *l1b, *l2b;
  buildTwoLightScene(rendererB, lightsB, l1b, l2b);

  GMANOptions const options = twoLightOptions();
  GMANFrameBuffer frameA(kRepeatRes, kRepeatRes, options.getBackground());
  GMANFrameBuffer frameB(kRepeatRes, kRepeatRes, options.getBackground());
  GMANAttributes const attr;
  rendererA.render(&frameA, &viewingSysA, options, attr);
  rendererB.render(&frameB, &viewingSysB, options, attr);

  check(framesMatch(frameA, frameB, kRepeatRes, kRepeatRes),
        "repeat: two renders of the same scene match bit for bit, colour and alpha");
}

// ---- D.7: Crop ----
void checkCrop() {
  GMANMatrix4 const identity;
  gman::VSPerspective viewingSysFull(kRepeatRes, kRepeatRes, squareScreenWindow(), identity, 90.0f, 0.5f, 50.0f);
  gman::VSPerspective viewingSysCrop(kRepeatRes, kRepeatRes, squareScreenWindow(), identity, 90.0f, 0.5f, 50.0f);

  GMANPathtraceRenderer rendererFull;
  std::vector<GMANLight const*> lightsFull;
  GMANLight const *l1f, *l2f;
  buildTwoLightScene(rendererFull, lightsFull, l1f, l2f);

  GMANPathtraceRenderer rendererCrop;
  std::vector<GMANLight const*> lightsCrop;
  GMANLight const *l1c, *l2c;
  buildTwoLightScene(rendererCrop, lightsCrop, l1c, l2c);

  GMANOptions const fullOptions = twoLightOptions();
  GMANFrameBuffer frameFull(kRepeatRes, kRepeatRes, fullOptions.getBackground());
  GMANAttributes const attr;
  rendererFull.render(&frameFull, &viewingSysFull, fullOptions, attr);

  GMANOptions cropOptions = twoLightOptions();
  cropOptions.setCropWindow(0.25f, 0.75f, 0.25f, 0.75f);
  GMANOptions::RasterInfo const raster = cropOptions.getRasterInfo();
  int const cropWidth = raster.rxmax - raster.rxmin + 1;
  int const cropHeight = raster.rymax - raster.rymin + 1;
  check(cropWidth > 0 && cropHeight > 0, "crop setup: the crop rectangle has a positive size");

  GMANFrameBuffer frameCrop(cropWidth, cropHeight, cropOptions.getBackground());
  rendererCrop.render(&frameCrop, &viewingSysCrop, cropOptions, attr);

  bool everyCroppedPixelMatches = true;
  for (int y = 0; y < cropHeight; ++y) {
    for (int x = 0; x < cropWidth; ++x) {
      int const fx = raster.rxmin + x;
      int const fy = raster.rymin + y;
      if (!colorExactly(frameCrop.getPixel(x, y), frameFull.getPixel(fx, fy))) {
        everyCroppedPixelMatches = false;
      }
      GMANAlpha const ca = frameCrop.getAlpha(x, y);
      GMANAlpha const fa = frameFull.getAlpha(fx, fy);
      if (ca.getRed() != fa.getRed() || ca.getGreen() != fa.getGreen() || ca.getBlue() != fa.getBlue()) {
        everyCroppedPixelMatches = false;
      }
    }
  }
  check(everyCroppedPixelMatches,
        "crop: every cropped pixel equals the full render's pixel at the same raster position bit for bit");
}

// ---- D.8: No indirect pass ----
void checkNoIndirectPass() {
  std::string const logPath = "pathtracerframe_indirect.log";
  std::remove(logPath.c_str());

  GMANLight const point(GMAN_LIGHT_POINT, GMANColor(4.0f, 4.0f, 4.0f), GMANPoint(0.0f, -2.0f, 8.0f), GMANVector());
  std::vector<GMANLight const*> const lights = {&point};
  LambertShader const shader(GMANColor(0.5f, 0.5f, 0.5f));

  GMANMatrix4 const identity;
  gman::VSPerspective viewingSysA(kRepeatRes, kRepeatRes, squareScreenWindow(), identity, 90.0f, 0.5f, 50.0f);
  gman::VSPerspective viewingSysB(kRepeatRes, kRepeatRes, squareScreenWindow(), identity, 90.0f, 0.5f, 50.0f);

  GMANPathtraceRenderer rendererA;
  GMANRayPolygon* floorA = buildFloor();
  gman::Appearance appearanceA;
  appearanceA.shader = asAppearanceShader(shader);
  appearanceA.Os = GMANColor(1.0f, 1.0f, 1.0f);
  appearanceA.lights = lights;
  floorA->setAppearance(appearanceA);
  rendererA.getWorldManager()->add(floorA);

  GMANPathtraceRenderer rendererB;
  GMANRayPolygon* floorB = buildFloor();
  gman::Appearance appearanceB = appearanceA;
  floorB->setAppearance(appearanceB);
  rendererB.getWorldManager()->add(floorB);

  GMANOptions const withoutPass = twoLightOptions();
  GMANOptions withPass = twoLightOptions();
  withPass.setIndirectPass("nosuchpass");

  GMANFrameBuffer frameA(kRepeatRes, kRepeatRes, withoutPass.getBackground());
  GMANAttributes const attr;
  rendererA.render(&frameA, &viewingSysA, withoutPass, attr);

  setLogFile(logPath.c_str());
  setScreenOutput(false);
  GMANFrameBuffer frameB(kRepeatRes, kRepeatRes, withPass.getBackground());
  rendererB.render(&frameB, &viewingSysB, withPass, attr);
  setLogFile("/dev/null");
  setScreenOutput(true);

  check(framesMatch(frameA, frameB, kRepeatRes, kRepeatRes),
        "no indirect pass: rendering with an unresolvable indirect pass matches without it, bit for bit");

  std::string const log = readFile(logPath);
  std::size_t const firstHit = log.find("nosuchpass");
  check(firstHit != std::string::npos, "no indirect pass: the log names nosuchpass");
  check(firstHit == std::string::npos || log.find("nosuchpass", firstHit + 1) == std::string::npos,
        "no indirect pass: the log holds exactly one warning naming nosuchpass");
}

// ---- D.9: Drops ----
void checkDrops() {
  constexpr RtInt kRes = 16;
  constexpr RtFloat kSphereRadius = 10.0f;
  constexpr RtFloat kReflectance = 0.75f;
  constexpr RtFloat kIntensity = 100.0f;
  RtFloat const nan = std::numeric_limits<RtFloat>::quiet_NaN();

  GMANOptions options;
  options.setFormat(kRes, kRes, 1.0f);
  options.setPixelSamples(1.0f, 1.0f);
  options.setPixelFilter(RiBoxFilter, 1.0f, 1.0f);
  options.setPathtracerSamples(kDefaultSamples);
  options.setBackground(GMANColor(nan, nan, nan));

  GMANMatrix4 const identity;
  gman::VSPerspective viewingSys(kRes, kRes, squareScreenWindow(), identity, 90.0f, 0.5f, 50.0f);

  GMANPathtraceRenderer renderer;
  LambertShader const shader(GMANColor(kReflectance, kReflectance, kReflectance));
  // zmin above -radius: the sphere's own missing cap opens behind the eye,
  // at negative z, where no camera ray ever looks.
  GMANRaySphere* sphere = new GMANRaySphere(kSphereRadius, -8.0f, kSphereRadius, 360.0f, GMANParameterList());
  GMANLight const light(GMAN_LIGHT_POINT, GMANColor(kIntensity, kIntensity, kIntensity), GMANPoint(0.0f, 0.0f, 0.0f),
                        GMANVector());
  gman::Appearance appearance;
  appearance.shader = asAppearanceShader(shader);
  appearance.Os = GMANColor(1.0f, 1.0f, 1.0f);
  appearance.lights = {&light};
  sphere->setAppearance(appearance);
  renderer.getWorldManager()->add(sphere);

  std::string const logPath = "pathtracerframe_drops.log";
  std::remove(logPath.c_str());
  setLogFile(logPath.c_str());
  setScreenOutput(false);

  GMANFrameBuffer frameBuffer(kRes, kRes, options.getBackground());
  GMANAttributes const attr;
  renderer.render(&frameBuffer, &viewingSys, options, attr);

  setLogFile("/dev/null");
  setScreenOutput(true);

  std::size_t const dropped = renderer.droppedPathCount();
  std::size_t const totalPaths = (std::size_t)kRes * (std::size_t)kRes * (std::size_t)kDefaultSamples;
  check(dropped > 0, "drops: droppedPathCount() is above 0");
  check(dropped < totalPaths, "drops: droppedPathCount() is below the frame's path count");

  bool everyPixelFinite = true;
  double alphaSum = 0.0;
  for (int y = 0; y < kRes; ++y) {
    for (int x = 0; x < kRes; ++x) {
      GMANColor const p = frameBuffer.getPixel(x, y);
      if (!std::isfinite(p.getRed()) || !std::isfinite(p.getGreen()) || !std::isfinite(p.getBlue())) {
        everyPixelFinite = false;
      }
      GMANAlpha const a = frameBuffer.getAlpha(x, y);
      alphaSum += (double)a.getRed();
    }
  }
  check(everyPixelFinite, "drops: every pixel is finite");

  double const dropSumFromAlpha = (double)kDefaultSamples * ((double)(kRes * kRes) - alphaSum);
  check(std::fabs(dropSumFromAlpha - (double)dropped) <= 0.5,
        "drops: the sum over pixels of N*(1 - alpha) equals droppedPathCount() within 0.5");

  std::string const log = readFile(logPath);
  check(log.find(std::to_string(dropped)) != std::string::npos,
        "drops: the log holds one warning naming the dropped count");
}

} // namespace

int main() {
  checkEscape();
  checkEnvironment();
  checkCoverageUnlit();
  checkCoverageLitAndColoured();
  checkDepth();
  checkAntialiasing();
  checkRepeat();
  checkCrop();
  checkNoIndirectPass();
  checkDrops();

  return checkSummary("the path tracer's own frame: escape, environment, coverage, depth, antialiasing, repeat, "
                      "crop, an unresolvable indirect pass and non-finite drops each hold their own invariant");
}
