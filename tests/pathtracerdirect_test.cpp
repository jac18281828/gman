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
 * Next-event estimation on the floor scene: a point, a spot and a
 * distant light each match rho * Cl(p) * cos(theta) at a measured pixel,
 * Cl from GMANLight::sample; two point lights sum their own terms; and
 * ambientlight lights nothing, with a warning naming it and the count
 * skipped.
 */

#include <cmath>
#include <cstdio>
#include <fstream>
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
constexpr int kMidGrid = 16;
constexpr std::size_t kMinMeasuredPixels = 400;

constexpr RtFloat kFloorXMin = -8.0f;
constexpr RtFloat kFloorXMax = 8.0f;
constexpr RtFloat kFloorY = -4.0f;
constexpr RtFloat kFloorZMin = 1.0f;
constexpr RtFloat kFloorZMax = 17.0f;

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

GMANRayPolygon* buildFloor() {
  std::vector<GMANPoint> const verts = {
      GMANPoint(kFloorXMin, kFloorY, kFloorZMin), GMANPoint(kFloorXMin, kFloorY, kFloorZMax),
      GMANPoint(kFloorXMax, kFloorY, kFloorZMax), GMANPoint(kFloorXMax, kFloorY, kFloorZMin)};
  return new GMANRayPolygon(verts, GMANParameterList());
}

// The ray-plane intersection with y = kFloorY, in double; false when the
// ray is parallel to the floor or the plane lies behind its origin.
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

// A pixel is measured when its cell's four corner rays all hit the floor,
// clear of its edge by kEdgeMargin: the ray tracer's own polygon
// intersection carries its own float tolerance, so a corner within a
// hair's width of the true boundary can miss where this test's own
// double-precision plane intersection says it should hit.
constexpr double kEdgeMargin = 1.0;

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

// The mean, over a kMidGrid x kMidGrid midpoint grid of pixel (px, py)'s
// cell, of the analytic radiance summed over lights: rho * Cl(p) * |N . L|,
// Cl and L from GMANLight::sample, N = (0, 1, 0).
GMANColor analyticExpected(gman::VSPerspective& viewingSys, int px, int py,
                           std::vector<GMANLight const*> const& lights) {
  double sum[3] = {0.0, 0.0, 0.0};
  for (int sy = 0; sy < kMidGrid; ++sy) {
    for (int sx = 0; sx < kMidGrid; ++sx) {
      RtFloat const rx = (RtFloat)px + ((RtFloat)sx + 0.5f) / (RtFloat)kMidGrid;
      RtFloat const ry = (RtFloat)py + ((RtFloat)sy + 0.5f) / (RtFloat)kMidGrid;
      GMANRay const ray = viewingSys.cameraRay(rx, ry);
      double x, z;
      if (!intersectFloorPlane(ray, x, z)) {
        continue;
      }
      GMANPoint const p((RtFloat)x, kFloorY, (RtFloat)z);
      for (GMANLight const* light : lights) {
        GMANVector l;
        GMANColor cl;
        light->sample(p, l, cl);
        l.normalize();
        double const cosTheta = std::fabs((double)l.getY());
        sum[0] += (double)kReflectance * (double)cl.getRed() * cosTheta;
        sum[1] += (double)kReflectance * (double)cl.getGreen() * cosTheta;
        sum[2] += (double)kReflectance * (double)cl.getBlue() * cosTheta;
      }
    }
  }
  double const n = (double)(kMidGrid * kMidGrid);
  return GMANColor((RtFloat)(sum[0] / n), (RtFloat)(sum[1] / n), (RtFloat)(sum[2] / n));
}

double channel(GMANColor const& c, int i) {
  if (i == 0) {
    return c.getRed();
  }
  return i == 1 ? c.getGreen() : c.getBlue();
}

std::shared_ptr<GMANSurfaceShader const> asAppearanceShader(GMANSurfaceShader const& shader) {
  return std::shared_ptr<GMANSurfaceShader const>(&shader, [](GMANSurfaceShader const*) {});
}

// Renders the floor lit by lights and reports each measured pixel's
// residual (pixel - expected) per channel, plus the count measured.
void renderAndCollectResiduals(std::vector<GMANLight const*> const& lights, std::vector<double> residuals[3],
                               std::vector<double> expectedByChannel[3], std::size_t& measuredCount,
                               std::size_t& droppedCount) {
  GMANOptions options;
  options.setFormat(kRes, kRes, 1.0f);
  options.setPixelSamples(1.0f, 1.0f);
  options.setPixelFilter(RiBoxFilter, 1.0f, 1.0f);
  options.setPathtracerSamples(kSamples);

  GMANMatrix4 const identity;
  gman::VSPerspective viewingSys(kRes, kRes, squareScreenWindow(), identity, 90.0f, 0.5f, 50.0f);

  LambertShader const shader(GMANColor(kReflectance, kReflectance, kReflectance));
  GMANPathtraceRenderer renderer;
  GMANRayPolygon* floor = buildFloor();
  gman::Appearance appearance;
  appearance.shader = asAppearanceShader(shader);
  appearance.Os = GMANColor(1.0f, 1.0f, 1.0f);
  appearance.lights = lights;
  floor->setAppearance(appearance);
  renderer.getWorldManager()->add(floor);

  GMANFrameBuffer frameBuffer(kRes, kRes, options.getBackground());
  GMANAttributes const attr;
  renderer.render(&frameBuffer, &viewingSys, options, attr);
  droppedCount = renderer.droppedPathCount();

  measuredCount = 0;
  for (int py = 0; py < kRes; ++py) {
    for (int px = 0; px < kRes; ++px) {
      if (!pixelMeasured(viewingSys, px, py)) {
        continue;
      }
      ++measuredCount;
      GMANColor const expected = analyticExpected(viewingSys, px, py, lights);
      GMANColor const actual = frameBuffer.getPixel(px, py);
      for (int c = 0; c < 3; ++c) {
        residuals[c].push_back(channel(actual, c) - channel(expected, c));
        expectedByChannel[c].push_back(channel(expected, c));
      }
    }
  }
}

void checkResiduals(std::vector<double> residuals[3], std::vector<double> expectedByChannel[3], double relativeFloor,
                    std::string const& label) {
  char const* const channelName[3] = {"red", "green", "blue"};
  for (int c = 0; c < 3; ++c) {
    GmanMeanStderr const residualStat = meanStderr(residuals[c]);
    GmanMeanStderr const expectedStat = meanStderr(expectedByChannel[c]);
    double const floorAbs = relativeFloor * std::fabs(expectedStat.mean);
    std::printf("%s %s: residual mean %.6f (%.3f sigma), expected mean %.6f\n", label.c_str(), channelName[c],
                residualStat.mean,
                residualStat.stderrOfMean > 0.0 ? residualStat.mean / residualStat.stderrOfMean : 0.0,
                expectedStat.mean);
    checkNear(residualStat.mean, 0.0, residualStat.stderrOfMean, floorAbs,
              label + ": the " + channelName[c] + " channel's residual mean is within 5 sigma of 0");
  }
}

// ---- check 3: one light at a time ----
void testOneLight() {
  struct Case {
    char const* name;
    GMANLight light;
  };
  Case cases[] = {
      {"point", GMANLight(GMAN_LIGHT_POINT, GMANColor(4.0f, 4.0f, 4.0f), GMANPoint(0.0f, -2.0f, 8.0f), GMANVector())},
      {"spot", GMANLight(GMAN_LIGHT_SPOT, GMANColor(4.0f, 4.0f, 4.0f), GMANPoint(0.0f, -2.0f, 8.0f),
                         GMANVector(0.0f, -1.0f, 0.0f), 0.6f, 0.15f, 2.0f)},
      {"distant",
       GMANLight(GMAN_LIGHT_DISTANT, GMANColor(0.8f, 0.8f, 0.8f), GMANPoint(), GMANVector(0.0f, -1.0f, 0.0f))},
  };

  for (auto const& c : cases) {
    std::vector<GMANLight const*> const lights = {&c.light};
    std::vector<double> residuals[3];
    std::vector<double> expected[3];
    std::size_t measured = 0;
    std::size_t dropped = 0;
    renderAndCollectResiduals(lights, residuals, expected, measured, dropped);
    check(dropped == 0, std::string("one light, ") + c.name + ": droppedPathCount() is 0");

    check(measured >= kMinMeasuredPixels, std::string("one light, ") + c.name + ": at least 400 pixels measured");
    checkResiduals(residuals, expected, 1e-4, std::string("one light, ") + c.name);
  }
}

// ---- check 4: two lights ----
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

// ---- check 5: ambientlight lights nothing ----
void testAmbientLightsNothing() {
  std::string const logPath = "pathtracerdirect_ambient.log";
  std::remove(logPath.c_str());
  setLogFile(logPath.c_str());
  setScreenOutput(false);

  GMANLight const ambient(GMAN_LIGHT_AMBIENT, GMANColor(1.0f, 1.0f, 1.0f), GMANPoint(), GMANVector());
  std::vector<GMANLight const*> const lights = {&ambient};

  GMANOptions options;
  options.setFormat(16, 16, 1.0f);
  options.setPixelSamples(1.0f, 1.0f);
  options.setPixelFilter(RiBoxFilter, 1.0f, 1.0f);
  options.setPathtracerSamples(kSamples);

  GMANMatrix4 const identity;
  gman::VSPerspective viewingSys(16, 16, squareScreenWindow(), identity, 90.0f, 0.5f, 50.0f);

  LambertShader const shader(GMANColor(kReflectance, kReflectance, kReflectance));
  GMANPathtraceRenderer renderer;
  GMANRayPolygon* floor = buildFloor();
  gman::Appearance appearance;
  appearance.shader = asAppearanceShader(shader);
  appearance.Os = GMANColor(1.0f, 1.0f, 1.0f);
  appearance.lights = lights;
  floor->setAppearance(appearance);
  renderer.getWorldManager()->add(floor);

  GMANFrameBuffer frameBuffer(16, 16, options.getBackground());
  GMANAttributes const attr;
  renderer.render(&frameBuffer, &viewingSys, options, attr);

  bool everyPixelBlack = true;
  for (int y = 0; y < 16; ++y) {
    for (int x = 0; x < 16; ++x) {
      GMANColor const p = frameBuffer.getPixel(x, y);
      if (p.getRed() != 0.0f || p.getGreen() != 0.0f || p.getBlue() != 0.0f) {
        everyPixelBlack = false;
      }
    }
  }
  check(everyPixelBlack, "ambientlight: every pixel is exactly black");
  check(renderer.droppedPathCount() == 0, "ambientlight: droppedPathCount() is 0");

  setLogFile("/dev/null");
  setScreenOutput(true);

  std::ifstream in1(logPath, std::ios::binary);
  std::ostringstream contents1;
  contents1 << in1.rdbuf();
  std::string const log1 = contents1.str();
  check(log1.find("ambientlight") != std::string::npos && log1.find('1') != std::string::npos,
        "ambientlight: the log holds one warning naming ambientlight and 1");

  // Two ambient lights: one warning naming 2.
  std::remove(logPath.c_str());
  setLogFile(logPath.c_str());
  setScreenOutput(false);

  GMANLight const ambient2(GMAN_LIGHT_AMBIENT, GMANColor(1.0f, 1.0f, 1.0f), GMANPoint(), GMANVector());
  std::vector<GMANLight const*> const twoAmbient = {&ambient, &ambient2};
  GMANPathtraceRenderer renderer2;
  GMANRayPolygon* floor2 = buildFloor();
  gman::Appearance appearance2;
  appearance2.shader = asAppearanceShader(shader);
  appearance2.Os = GMANColor(1.0f, 1.0f, 1.0f);
  appearance2.lights = twoAmbient;
  floor2->setAppearance(appearance2);
  renderer2.getWorldManager()->add(floor2);

  GMANFrameBuffer frameBuffer2(16, 16, options.getBackground());
  renderer2.render(&frameBuffer2, &viewingSys, options, attr);

  setLogFile("/dev/null");
  setScreenOutput(true);

  std::ifstream in2(logPath, std::ios::binary);
  std::ostringstream contents2;
  contents2 << in2.rdbuf();
  std::string const log2 = contents2.str();
  check(log2.find("ambientlight") != std::string::npos && log2.find('2') != std::string::npos,
        "ambientlight: two ambient lights log one warning naming 2");
}

} // namespace

int main() {
  testOneLight();
  testTwoLights();
  testAmbientLightsNothing();

  return checkSummary("next-event estimation on the floor scene matches rho * Cl(p) * cos(theta) for a point, a "
                      "spot and a distant light, sums two lights' own terms, and ambientlight lights nothing");
}
