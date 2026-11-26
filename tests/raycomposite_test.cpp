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
 * A partly opaque surface that a glass or mirror ray reaches no longer hides
 * what lies behind it under the ray tracer. Each tests/rib/raycomposite_*.rib
 * fixture puts a black Opacity 0.5 panel before a lit wall: raycomposite_glass
 * shows it directly on the right of the frame and through a clear glass slab
 * on the left, raycomposite_mirror in a 45 degree mirror. The ray tracer's
 * block through the slab or the mirror agrees with the path tracer's within
 * 1.5%, which covers the path tracer's noise and the wall-slab-wall bounce the
 * ray tracer lacks, and reads half of the same block in the panel's bare twin
 * to within 0.01, with no path tracer to add noise. The bare twins also prove
 * the directly seen panel composites at half of its twin's wall and the slab
 * passes the two faces' transmittance. raycomposite_ambient runs under the ray
 * tracer alone, since the path tracer reads no ambientlight: a translucent red
 * sphere before a green wall, seen directly and in a mirror of Kr 0.8, whose
 * mirrored block reads 0.8 times its direct block in every channel.
 */

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

#include <tiffio.h>

#include "check.h"
#include "goldenimage.h"
#include "rungman.h"

namespace {

struct Block {
  std::uint32_t x;
  std::uint32_t y;
  std::uint32_t width;
  std::uint32_t height;
};

constexpr Block kGlassThroughSlab = {6, 12, 16, 24};
constexpr Block kGlassDirect = {42, 12, 16, 24};
constexpr Block kMirrorBlock = {54, 14, 8, 20};
constexpr Block kAmbientDirect = {16, 22, 4, 4};
constexpr Block kAmbientMirrored = {48, 22, 4, 4};

constexpr double kOracleTolerance = 0.015;
constexpr double kOracleFloor = 64.0;
constexpr double kHalfOfBare = 0.5;
constexpr double kHalfTolerance = 0.01;
constexpr double kGlassRatioLow = 0.90;
constexpr double kGlassRatioHigh = 0.94;
constexpr double kAmbientRedFloor = 150.0;
constexpr double kAmbientGreenFloor = 60.0;
constexpr double kMirrorKr = 0.8;
constexpr double kMirroredTolerance = 1.5;

constexpr std::size_t kChannels = 3;
constexpr std::size_t kRed = 0;
constexpr std::size_t kGreen = 1;

char const* const kRayTracer = "gmanraytracer";
char const* const kPathTracer = "gmanpathtracer";

using Mean = std::array<double, kChannels>;

double channelOf(std::uint32_t pixel, std::size_t channel) {
  if (channel == kRed) {
    return TIFFGetR(pixel);
  }
  if (channel == kGreen) {
    return TIFFGetG(pixel);
  }
  return TIFFGetB(pixel);
}

Mean blockMean(GmanImage const& image, Block const& block) {
  Mean mean{};
  for (std::uint32_t y = block.y; y < block.y + block.height; ++y) {
    for (std::uint32_t x = block.x; x < block.x + block.width; ++x) {
      for (std::size_t channel = 0; channel < kChannels; ++channel) {
        mean[channel] += channelOf(image.at(x, y), channel);
      }
    }
  }
  const auto count = static_cast<double>(block.width) * static_cast<double>(block.height);
  for (double& value : mean) {
    value /= count;
  }
  return mean;
}

GmanImage render(std::string const& gman, std::string const& ribDir, std::string const& renderer,
                 std::string const& name) {
  const auto tif = name + ".tif";
  std::remove(tif.c_str());
  const auto run = runGman(gman, {"-r", renderer, ribDir + "/" + name + ".rib"});
  check(run.exitStatus == 0, renderer + " renders " + name + " (exit " + std::to_string(run.exitStatus) + ")");
  GmanImage image = readGmanTIFF(tif);
  check(image.ok, renderer + ": " + tif + " reads back");
  return image;
}

struct Twin {
  GmanImage ray;
  GmanImage path;
};

struct Scene {
  Twin panel;
  GmanImage bare;
};

bool usable(Scene const& scene) { return scene.panel.ray.ok && scene.panel.path.ok && scene.bare.ok; }

Twin renderTwin(std::string const& gman, std::string const& ribDir, std::string const& name) {
  Twin twin;
  twin.ray = render(gman, ribDir, kRayTracer, name);
  twin.path = render(gman, ribDir, kPathTracer, name);
  return twin;
}

std::string channelLabel(std::string const& what, std::size_t channel) {
  return what + ", channel " + std::to_string(channel) + ": ";
}

void checkFloor(std::string const& what, Mean const& mean, double floor) {
  for (std::size_t channel = 0; channel < kChannels; ++channel) {
    check(mean[channel] > floor, channelLabel(what, channel) + "setup: the block exceeds " + std::to_string(floor) +
                                     " levels (" + std::to_string(mean[channel]) + ")");
  }
}

void checkAgainstPath(std::string const& what, Mean const& ray, Mean const& path) {
  for (std::size_t channel = 0; channel < kChannels; ++channel) {
    const auto excess = ray[channel] / path[channel] - 1.0;
    std::printf("%s: channel %zu: ray %.4f, path %.4f, ratio - 1 %+.4f, tolerance %.3f\n", what.c_str(), channel,
                ray[channel], path[channel], excess, kOracleTolerance);
    check(std::fabs(excess) <= kOracleTolerance,
          channelLabel(what, channel) + "the ray tracer's block is within " + std::to_string(kOracleTolerance * 100.0) +
              "% of the path tracer's (" + std::to_string(excess * 100.0) + "%)");
  }
}

void checkHalfOfBare(std::string const& what, Mean const& panel, Mean const& bare) {
  for (std::size_t channel = 0; channel < kChannels; ++channel) {
    const auto ratio = panel[channel] / bare[channel];
    std::printf("%s: channel %zu: panel %.4f, bare %.4f, ratio %.4f\n", what.c_str(), channel, panel[channel],
                bare[channel], ratio);
    check(std::fabs(ratio - kHalfOfBare) <= kHalfTolerance, channelLabel(what, channel) + "the panel's block is " +
                                                                std::to_string(ratio) +
                                                                " of its bare twin's, 0.5 within 0.01");
  }
}

void checkGlass(std::string const& gman, std::string const& ribDir) {
  Scene scene;
  scene.panel = renderTwin(gman, ribDir, "raycomposite_glass");
  scene.bare = render(gman, ribDir, kRayTracer, "raycomposite_glass_bare");
  if (!usable(scene)) {
    return;
  }

  const auto panelDirect = blockMean(scene.panel.ray, kGlassDirect);
  const auto bareDirect = blockMean(scene.bare, kGlassDirect);
  const auto bareThroughSlab = blockMean(scene.bare, kGlassThroughSlab);
  const auto panelRay = blockMean(scene.panel.ray, kGlassThroughSlab);
  const auto panelPath = blockMean(scene.panel.path, kGlassThroughSlab);

  checkHalfOfBare("glass setup, the panel seen directly", panelDirect, bareDirect);
  for (std::size_t channel = 0; channel < kChannels; ++channel) {
    const auto passed = bareThroughSlab[channel] / bareDirect[channel];
    check(passed >= kGlassRatioLow && passed <= kGlassRatioHigh, channelLabel("glass setup", channel) +
                                                                     "the bare slab passes " + std::to_string(passed) +
                                                                     " of the direct view, in [0.90, 0.94]");
  }
  checkFloor("glass setup, the path tracer's block through the slab", panelPath, kOracleFloor);

  checkAgainstPath("glass a, the panel through the slab", panelRay, panelPath);
  checkHalfOfBare("glass b, the panel through the slab over its bare twin", panelRay, bareThroughSlab);
}

void checkMirror(std::string const& gman, std::string const& ribDir) {
  Scene scene;
  scene.panel = renderTwin(gman, ribDir, "raycomposite_mirror");
  scene.bare = render(gman, ribDir, kRayTracer, "raycomposite_mirror_bare");
  if (!usable(scene)) {
    return;
  }

  const auto bareRay = blockMean(scene.bare, kMirrorBlock);
  const auto panelRay = blockMean(scene.panel.ray, kMirrorBlock);
  const auto panelPath = blockMean(scene.panel.path, kMirrorBlock);

  checkFloor("mirror setup, the bare twin under the ray tracer", bareRay, kOracleFloor);
  checkFloor("mirror setup, the path tracer's block", panelPath, kOracleFloor);

  checkAgainstPath("mirror a, the panel in the mirror", panelRay, panelPath);
  checkHalfOfBare("mirror b, the panel in the mirror over its bare twin", panelRay, bareRay);
}

void checkAmbient(std::string const& gman, std::string const& ribDir) {
  const auto image = render(gman, ribDir, kRayTracer, "raycomposite_ambient");
  if (!image.ok) {
    return;
  }

  const auto direct = blockMean(image, kAmbientDirect);
  const auto mirrored = blockMean(image, kAmbientMirrored);
  check(direct[kRed] > kAmbientRedFloor,
        "ambient setup: the direct block's red exceeds 150 (" + std::to_string(direct[kRed]) + ")");
  check(direct[kGreen] > kAmbientGreenFloor,
        "ambient setup: the direct block's green exceeds 60 (" + std::to_string(direct[kGreen]) + ")");

  for (std::size_t channel = 0; channel < kChannels; ++channel) {
    const auto expected = kMirrorKr * direct[channel];
    std::printf("ambient: channel %zu: direct %.4f, mirrored %.4f, 0.8 x direct %.4f\n", channel, direct[channel],
                mirrored[channel], expected);
    check(std::fabs(mirrored[channel] - expected) <= kMirroredTolerance,
          channelLabel("ambient", channel) + "the mirrored block " + std::to_string(mirrored[channel]) +
              " is within 1.5 of 0.8 times the direct block's " + std::to_string(expected));
  }
}

} // namespace

int main(int argc, char* argv[]) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s <gman-binary> <tests/rib dir>\n", argv[0]);
    return 2;
  }
  const auto gman = std::string(argv[1]);
  const auto ribDir = std::string(argv[2]);

  checkGlass(gman, ribDir);
  checkMirror(gman, ribDir);
  checkAmbient(gman, ribDir);

  return checkSummary("the ray tracer composites a partly opaque surface that a glass or mirror ray reaches, "
                      "agreeing with the path tracer through a slab and a mirror");
}
