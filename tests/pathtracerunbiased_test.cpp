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
 * A slot's mean under a positive PixelVariance matches the fixed-count mean.
 * One matte hairline torus, its tube about a fifth of a slot wide, faces the
 * camera under a distant light over a black background, so radiance and
 * coverage are nonzero only on the paths that hit it, a rare event per path.
 * Rendered in process at one slot per pixel, so a pixel reads its slot
 * bit-equal, with minsamples 4 so a pilot often misses the ring. For the red
 * coverage and each radiance channel the per-slot difference between a
 * stopped render and the fixed-count render has a mean within four standard
 * errors of 0, the paired z of its slots.
 */

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <format>
#include <limits>
#include <string>
#include <vector>

#include "check.h"
#include "gmanattributes.h"
#include "gmancolor.h"
#include "gmanframebuffer.h"
#include "gmanlightsourcemgr.h"
#include "gmanmatrix4.h"
#include "gmanoptions.h"
#include "gmanparameterlist.h"
#include "gmanpathtracerenderer.h"
#include "gmanpoint.h"
#include "gmanraytorus.h"
#include "gmanshading.h"
#include "gmantransform.h"
#include "gmanvector.h"
#include "gmanvsperspective.h"
#include "maketransform.h"
#include "pathtracerscene.h"
#include "ri.h"

namespace {

constexpr RtInt kFrameSize = 160;
constexpr RtInt kSamples = 64;
constexpr RtInt kMinSamples = 4;
constexpr RtFloat kSceneNear = 0.5f;
constexpr RtFloat kSceneFar = 50.0f;
constexpr RtFloat kViewFov = 90.0f;
constexpr RtFloat kFilterWidth = 1.0f;

constexpr RtFloat kRingDepth = 0.8f;
constexpr RtFloat kRingMajorRadius = 0.7f;
constexpr RtFloat kRingMinorRadius = 0.001f;
constexpr RtFloat kFullTurn = 360.0f;
constexpr RtFloat kHalfTurn = 180.0f;
constexpr RtFloat kReflectance = 0.5f;
constexpr RtFloat kLightIntensity = 1.0f;
constexpr RtFloat kLightDirectionX = 0.3f;
constexpr RtFloat kLightDirectionY = -0.4f;
constexpr RtFloat kLightDirectionZ = 1.0f;

constexpr std::size_t kMinimumRingSlots = 300;
constexpr RtFloat kModerateVariance = 0.01f;
constexpr RtFloat kTightVariance = 1.0e-6f;
constexpr double kZBound = 4.0;
constexpr std::size_t kQuantities = 4;

GMANColor const kBlack(0.0f, 0.0f, 0.0f);

struct Frame {
  std::array<std::vector<double>, kQuantities> quantities;
  std::vector<std::uint32_t> counts;
};

constexpr std::array<char const*, kQuantities> kQuantityNames = {"coverage red", "radiance red", "radiance green",
                                                                 "radiance blue"};

struct HairlineScene {
  GMANLight const distantLight;
  GMANMatrix4 const identity;
  gman::VSPerspective viewingSys;
  GMANPathtraceRenderer renderer;

  HairlineScene()
      : distantLight(GMAN_LIGHT_DISTANT, GMANColor(kLightIntensity, kLightIntensity, kLightIntensity), GMANPoint(),
                     GMANVector(kLightDirectionX, kLightDirectionY, kLightDirectionZ)),
        viewingSys(kFrameSize, kFrameSize, squareScreenWindow(), identity, kViewFov, kSceneNear, kSceneFar) {
    GMANMatrix4 place;
    place.trans(0.0f, 0.0f, kRingDepth);
    auto const ring = new GMANRayTorus(kRingMajorRadius, kRingMinorRadius, -kHalfTurn, kHalfTurn, kFullTurn,
                                       GMANParameterList(), makeTransform(place));
    gman::Appearance appearance;
    appearance.shader = loadShader("matte", matteParams(1.0f));
    appearance.Cs = GMANColor(kReflectance, kReflectance, kReflectance);
    appearance.Os = GMANColor(1.0f, 1.0f, 1.0f);
    appearance.lights = {&distantLight};
    ring->setAppearance(appearance);
    renderer.getWorldManager()->add(ring);
  }

  Frame render(RtFloat variance) {
    GMANOptions options;
    options.setFormat(kFrameSize, kFrameSize, 1.0f);
    options.setPathtracerSamples(kSamples);
    options.setBackground(kBlack);
    options.setPixelFilter(RiBoxFilter, kFilterWidth, kFilterWidth);
    options.setPixelSamples(1.0f, 1.0f);
    options.setPixelVariance(variance);
    options.setNamedOption("pathtracer", "minsamples", std::vector<RtInt>{kMinSamples});

    GMANFrameBuffer frameBuffer(kFrameSize, kFrameSize, kBlack);
    GMANAttributes const attributes;
    renderer.render(&frameBuffer, &viewingSys, options, attributes);

    Frame frame;
    frame.counts = renderer.slotPathCounts();
    for (RtInt y = 0; y < kFrameSize; ++y) {
      for (RtInt x = 0; x < kFrameSize; ++x) {
        GMANColor const& colour = frameBuffer.getPixel(x, y);
        frame.quantities[0].push_back(frameBuffer.getAlpha(x, y).getRed());
        frame.quantities[1].push_back(colour.getRed());
        frame.quantities[2].push_back(colour.getGreen());
        frame.quantities[3].push_back(colour.getBlue());
      }
    }
    return frame;
  }
};

double meanOf(std::vector<double> const& values) {
  double sum = 0.0;
  for (double const value : values) {
    sum += value;
  }
  return sum / static_cast<double>(values.size());
}

double pairedZ(std::vector<double> const& stopped, std::vector<double> const& fixed) {
  double const count = static_cast<double>(stopped.size());
  double mean = 0.0;
  for (std::size_t i = 0; i < stopped.size(); ++i) {
    mean += (stopped[i] - fixed[i]) / count;
  }
  double sumSquares = 0.0;
  for (std::size_t i = 0; i < stopped.size(); ++i) {
    double const deviation = stopped[i] - fixed[i] - mean;
    sumSquares += deviation * deviation;
  }
  double const standardDeviation = std::sqrt(sumSquares / (count - 1.0));
  if (standardDeviation > 0.0) {
    return mean / (standardDeviation / std::sqrt(count));
  }
  return mean == 0.0 ? 0.0 : std::copysign(std::numeric_limits<double>::infinity(), mean);
}

std::size_t slotsWithCoverage(Frame const& frame) {
  std::size_t covered = 0;
  for (double const alpha : frame.quantities[0]) {
    covered += alpha > 0.0 ? 1 : 0;
  }
  return covered;
}

bool someSlotStoppedEarly(Frame const& frame) {
  for (std::uint32_t const count : frame.counts) {
    if (count < static_cast<std::uint32_t>(kSamples)) {
      return true;
    }
  }
  return false;
}

void checkUnbiased(Frame const& stopped, Frame const& fixed, RtFloat variance) {
  for (std::size_t q = 0; q < kQuantities; ++q) {
    double const z = pairedZ(stopped.quantities[q], fixed.quantities[q]);
    double const stoppedMean = meanOf(stopped.quantities[q]);
    double const fixedMean = meanOf(fixed.quantities[q]);
    std::printf("PixelVariance %g, %s: stopped mean %.6g, fixed mean %.6g, difference %+.6g, z %+.2f\n",
                static_cast<double>(variance), kQuantityNames[q], stoppedMean, fixedMean, stoppedMean - fixedMean, z);
    check(std::fabs(z) <= kZBound, std::format("a stopped slot's {} matches the fixed-count mean within |z| <= 4 at "
                                               "PixelVariance {}",
                                               kQuantityNames[q], variance));
  }
}

int runChecks() {
  HairlineScene scene;
  Frame const fixed = scene.render(0.0f);
  std::size_t const covered = slotsWithCoverage(fixed);
  check(covered >= kMinimumRingSlots,
        "setup: the fixed-count render has at least 300 slots with coverage (" + std::to_string(covered) + ")");

  Frame const moderate = scene.render(kModerateVariance);
  check(someSlotStoppedEarly(moderate), "setup: some slot stops below the maximum at PixelVariance 0.01");
  Frame const tight = scene.render(kTightVariance);

  checkUnbiased(moderate, fixed, kModerateVariance);
  checkUnbiased(tight, fixed, kTightVariance);
  return checkSummary("a slot stopped under a positive PixelVariance has the fixed-count mean");
}

} // namespace

int main() { return runChecks(); }
