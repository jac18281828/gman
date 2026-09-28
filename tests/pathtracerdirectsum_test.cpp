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
 * Next-event estimation on the floor scene: two point lights sum their own
 * terms, and ambientlight lights nothing, with a warning naming it and the
 * count skipped.
 */

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
#include "gmanpathtracerenderer.h"
#include "gmanpoint.h"
#include "gmanraypolygon.h"
#include "gmanvector.h"
#include "gmanvsperspective.h"
#include "pathtracerscene.h"
#include "ri.h"

namespace {

constexpr RtFloat kReflectance = 0.5f; // Kd = 1, Cs = 0.5
constexpr RtInt kSamples = 64;
constexpr std::size_t kMinMeasuredPixels = 400;

// Two lights: the sum of both terms.
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

// Counts non-overlapping occurrences of needle in haystack.
std::size_t countOccurrences(std::string const& haystack, std::string const& needle) {
  std::size_t count = 0;
  std::size_t pos = 0;
  while ((pos = haystack.find(needle, pos)) != std::string::npos) {
    ++count;
    pos += needle.size();
  }
  return count;
}

// ambientlight lights nothing: one warning names it and the count skipped.
void testAmbientLightsNothing() {
  std::string const logPath = "pathtracerdirectsum_ambient.log";
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

  std::shared_ptr<GMANSurfaceShader const> const shader = loadShader("matte", matteParams(1.0f));
  GMANPathtraceRenderer renderer;
  GMANRayPolygon* floor = buildFloor();
  gman::Appearance appearance;
  appearance.shader = shader;
  appearance.Cs = GMANColor(kReflectance, kReflectance, kReflectance);
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
  check(countOccurrences(log1, "ambientlight lights nothing") == 1 &&
            log1.find("skipped 1 light(s)") != std::string::npos,
        "ambientlight: exactly one warning logs, naming ambientlight and the count 1");

  // Two ambient lights: one warning naming 2.
  std::remove(logPath.c_str());
  setLogFile(logPath.c_str());
  setScreenOutput(false);

  GMANLight const ambient2(GMAN_LIGHT_AMBIENT, GMANColor(1.0f, 1.0f, 1.0f), GMANPoint(), GMANVector());
  std::vector<GMANLight const*> const twoAmbient = {&ambient, &ambient2};
  GMANPathtraceRenderer renderer2;
  GMANRayPolygon* floor2 = buildFloor();
  gman::Appearance appearance2;
  appearance2.shader = shader;
  appearance2.Cs = GMANColor(kReflectance, kReflectance, kReflectance);
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
  check(countOccurrences(log2, "ambientlight lights nothing") == 1 &&
            log2.find("skipped 2 light(s)") != std::string::npos,
        "ambientlight: two ambient lights log exactly one warning naming the count 2");
}

} // namespace

int main() {
  testTwoLights();
  testAmbientLightsNothing();

  return checkSummary("next-event estimation on the floor scene sums two lights' own terms, and ambientlight "
                      "lights nothing");
}
