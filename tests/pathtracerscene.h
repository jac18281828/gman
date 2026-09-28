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

#pragma once

/*
 * The path tracer's own test floor scene, shared by the integrator tests
 * that render it: a square Polygon at y = kFloorY spanning x in
 * [kFloorXMin, kFloorXMax] and z in [kFloorZMin, kFloorZMax], with a
 * double-precision plane intersection and a measured-pixel test to match.
 * Also the shipped-shader loader every integrator test uses in place of a
 * hand-written closure, and the direct-lighting tests' own render, analytic
 * expectation and residual check against it.
 */

#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "check.h"
#include "gmanattributes.h"
#include "gmancolor.h"
#include "gmandictionary.h"
#include "gmanframebuffer.h"
#include "gmanlightsourcemgr.h"
#include "gmanoptions.h"
#include "gmanparameterlist.h"
#include "gmanpathtracerenderer.h"
#include "gmanpoint.h"
#include "gmanray.h"
#include "gmanraypolygon.h"
#include "gmansurfaceshader.h"
#include "gmanvector.h"
#include "gmanvsperspective.h"
#include "ri.h"
#include "samplingstats.h"

inline constexpr RtFloat kFloorXMin = -8.0f;
inline constexpr RtFloat kFloorXMax = 8.0f;
inline constexpr RtFloat kFloorY = -4.0f;
inline constexpr RtFloat kFloorZMin = 1.0f;
inline constexpr RtFloat kFloorZMax = 17.0f;

// The ray tracer's own polygon intersection carries its own float
// tolerance, so a corner within kEdgeMargin of the true boundary can miss
// where this test's own double-precision plane intersection says it
// should hit.
inline constexpr double kEdgeMargin = 1.0;

inline GMANOptions::ScreenWindowStruct squareScreenWindow() {
  GMANOptions::ScreenWindowStruct sw;
  sw.left = -1.0f;
  sw.right = 1.0f;
  sw.bottom = -1.0f;
  sw.top = 1.0f;
  return sw;
}

inline GMANRayPolygon* buildFloor() {
  std::vector<GMANPoint> const verts = {
      GMANPoint(kFloorXMin, kFloorY, kFloorZMin), GMANPoint(kFloorXMin, kFloorY, kFloorZMax),
      GMANPoint(kFloorXMax, kFloorY, kFloorZMax), GMANPoint(kFloorXMax, kFloorY, kFloorZMin)};
  return new GMANRayPolygon(verts, GMANParameterList());
}

// The ray-plane intersection with y = kFloorY, in double; false when the
// ray is parallel to the floor or the plane lies behind its origin.
inline bool intersectFloorPlane(GMANRay const& ray, double& outX, double& outZ) {
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

inline bool withinFloor(double x, double z, double margin) {
  return x >= (double)kFloorXMin + margin && x <= (double)kFloorXMax - margin && z >= (double)kFloorZMin + margin &&
         z <= (double)kFloorZMax - margin;
}

// A pixel is measured when its cell's four corner rays all hit the floor,
// clear of its edge by kEdgeMargin.
inline bool pixelMeasured(gman::VSPerspective& viewingSys, int px, int py) {
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

inline double channel(GMANColor const& c, int i) {
  if (i == 0) {
    return c.getRed();
  }
  return i == 1 ? c.getGreen() : c.getBlue();
}

inline double channel(GMANAlpha const& a, int i) {
  if (i == 0) {
    return a.getRed();
  }
  return i == 1 ? a.getGreen() : a.getBlue();
}

// Loads a shipped shader plugin by its RIB token, as Surface "name" would;
// the returned shader outlives the GMANAttributes that loaded it, since
// getSurface's shared_ptr aliases the plugin module itself.
inline std::shared_ptr<GMANSurfaceShader const> loadShader(std::string const& name, GMANParameterList const& params) {
  GMANAttributes attributes;
  attributes.setSurface(name, params);
  return attributes.getSurface(0.0);
}

// Surface "matte" "float Kd" [kd]'s own parameter list.
inline GMANParameterList matteParams(RtFloat kd) {
  static GMANDictionary dictionary;
  RtToken tokens[1] = {RI_KD};
  RtPointer parms[1] = {&kd};
  return GMANParameterList(dictionary, 1, tokens, parms);
}

// The mean, over a 16 x 16 midpoint grid of pixel (px, py)'s cell, of the
// analytic radiance summed over lights: rho * Cl(p) * |N . L|, Cl and L
// from GMANLight::sample, N = (0, 1, 0), rho = 0.5 (Kd 1, Cs 0.5).
inline GMANColor analyticExpected(gman::VSPerspective& viewingSys, int px, int py,
                                  std::vector<GMANLight const*> const& lights) {
  constexpr int kMidGrid = 16;
  constexpr RtFloat kReflectance = 0.5f;
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

// Renders the floor, Kd 1 and Cs 0.5 so rho = 0.5, lit by lights, at 81 x
// 81 and N = 64, and reports each measured pixel's residual (pixel -
// expected) per channel, plus the count measured.
inline void renderAndCollectResiduals(std::vector<GMANLight const*> const& lights, std::vector<double> residuals[3],
                                      std::vector<double> expectedByChannel[3], std::size_t& measuredCount,
                                      std::size_t& droppedCount) {
  constexpr RtInt kRes = 81;
  constexpr RtFloat kReflectance = 0.5f;
  constexpr RtInt kSamples = 64;

  GMANOptions options;
  options.setFormat(kRes, kRes, 1.0f);
  options.setPixelSamples(1.0f, 1.0f);
  options.setPixelFilter(RiBoxFilter, 1.0f, 1.0f);
  options.setPathtracerSamples(kSamples);

  GMANMatrix4 const identity;
  gman::VSPerspective viewingSys(kRes, kRes, squareScreenWindow(), identity, 90.0f, 0.5f, 50.0f);

  GMANPathtraceRenderer renderer;
  GMANRayPolygon* floor = buildFloor();
  gman::Appearance appearance;
  appearance.shader = loadShader("matte", matteParams(1.0f));
  appearance.Cs = GMANColor(kReflectance, kReflectance, kReflectance);
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

// Prints and asserts each channel's residual mean against relativeFloor
// times the expected mean's own magnitude, within 5 sigma of 0.
inline void checkResiduals(std::vector<double> residuals[3], std::vector<double> expectedByChannel[3],
                           double relativeFloor, std::string const& label) {
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
