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
 * The glass shader's eta, Kr and Kt move both renderers alike. A pane of two
 * glass faces before a red backdrop shows a blue panel, outside the camera's
 * frustum, only in its reflection: the red channel reads the light the pane
 * transmits and the blue channel the light it reflects. Each scene renders
 * under both renderers, cropped to one block of pixels, beside twins that
 * drop the reflected panel, the backdrop or the pane. At defaults the ray
 * tracer's block agrees with the path tracer's; Kr 0 leaves the reflection
 * of the twin without the panel and Kr 0.5 half of it; Kt 0 leaves the
 * transmission of the twin without the backdrop and Kt 0.5 a quarter of it,
 * one Kt per face; a second eta shows a different backdrop colour through a
 * sphere and eta 1 makes the pane vanish; and Kt and eta set the light that
 * crosses a pane to a shadowed receiver, (Kt * (1 - F))^2 of it, Kr none.
 *
 * kTolerance is at least twice the largest disagreement the two renderers show
 * over every block and channel this test reads, which the info line it prints
 * reports, and every effect it asserts exceeds ten times it.
 */

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <format>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include <tiffio.h>

#include "check.h"
#include "goldenimage.h"
#include "rungman.h"

namespace {

constexpr int kBlockHalfWidth = 2;
constexpr auto kBlockWidth = static_cast<std::uint32_t>(2 * kBlockHalfWidth + 1);
constexpr int kFrameWidth = 64;
constexpr int kFrameHeight = 48;
constexpr double kCropMargin = 0.25;
constexpr int kTwinSamples = 64;
constexpr double kTolerance = 1.2;
constexpr double kEffectFactor = 10.0;

constexpr double kIndex = 1.5;
constexpr double kSecondIndex = 1.2;
constexpr double kIndexMatched = 1.0;
constexpr double kHalf = 0.5;

constexpr std::size_t kRed = 0;
constexpr std::size_t kBlue = 2;

char const* const kGlassLine = "Surface \"glass\"";
char const* const kRayTracer = "gmanraytracer";
char const* const kPathTracer = "gmanpathtracer";

using Mean = std::array<double, 3>;

struct Block {
  int x;
  int y;
};

struct Scene {
  char const* fixture;
  Block block;
};

struct Settings {
  std::optional<double> eta;
  std::optional<double> kr;
  std::optional<double> kt;
};

struct Variant {
  Settings settings;
  std::vector<std::string> without;
};

struct Reading {
  Mean ray{};
  Mean path{};
};

struct Paths {
  std::string gman;
  std::string ribDir;
};

Scene const kPane = {"glassparams_pane", {52, 24}};
Scene const kSphere = {"glassparams_sphere", {24, 24}};
Scene const kShadow = {"glassparams_shadow", {32, 24}};

double largestDisagreement = 0.0;

std::string readFile(std::string const& path) {
  std::ifstream in(path);
  std::ostringstream text;
  text << in.rdbuf();
  return text.str();
}

bool writeFile(std::string const& path, std::string const& text) {
  std::ofstream out(path);
  out << text;
  return static_cast<bool>(out);
}

bool startsWith(std::string const& line, std::string const& prefix) { return line.rfind(prefix, 0) == 0; }

std::string glassLine(Settings const& settings) {
  std::string line = kGlassLine;
  if (settings.eta) {
    line += std::format(" \"float eta\" [{}]", *settings.eta);
  }
  if (settings.kr) {
    line += std::format(" \"Kr\" [{}]", *settings.kr);
  }
  if (settings.kt) {
    line += std::format(" \"float Kt\" [{}]", *settings.kt);
  }
  return line;
}

std::string cropLine(Block const& block) {
  const auto left = block.x - kBlockHalfWidth - kCropMargin;
  const auto right = block.x + kBlockHalfWidth + 1 - kCropMargin;
  const auto top = block.y - kBlockHalfWidth - kCropMargin;
  const auto bottom = block.y + kBlockHalfWidth + 1 - kCropMargin;
  return std::format("CropWindow {:.6f} {:.6f} {:.6f} {:.6f}\n", left / kFrameWidth, right / kFrameWidth,
                     top / kFrameHeight, bottom / kFrameHeight);
}

// The fixture's text with the lines between "## begin <name>" and
// "## end <name>" dropped for each name given.
std::string dropBlocks(std::string const& fixture, std::vector<std::string> const& names) {
  std::istringstream in(fixture);
  std::string out;
  std::string line;
  std::string skipping;
  while (std::getline(in, line)) {
    if (!skipping.empty()) {
      if (line == "## end " + skipping) {
        skipping.clear();
      }
      continue;
    }
    const auto begun = std::ranges::find_if(names, [&](auto const& name) { return line == "## begin " + name; });
    if (begun != names.end()) {
      skipping = *begun;
      continue;
    }
    out += line + "\n";
  }
  return out;
}

// The fixture's text with the blocks the variant leaves out dropped, its glass
// line replaced, its display named display and its frame cropped to the block.
// A twin, which leaves something out, renders at kTwinSamples: the channel read
// from it is black or crosses no glass, so the path tracer samples nothing
// noisy there.
std::string variantRib(std::string const& fixture, Variant const& variant, Block const& block,
                       std::string const& display) {
  std::istringstream in(dropBlocks(fixture, variant.without));
  std::string out;
  std::string line;
  while (std::getline(in, line)) {
    if (line == kGlassLine) {
      line = glassLine(variant.settings);
    } else if (startsWith(line, "Display ")) {
      line = std::format("Display \"{}\" \"file\" \"rgba\"", display);
    } else if (startsWith(line, "Option \"pathtracer\"") && !variant.without.empty()) {
      line = std::format("Option \"pathtracer\" \"integer samples\" [{}]", kTwinSamples);
    }
    out += line + "\n";
    if (startsWith(line, "Format ")) {
      out += cropLine(block);
    }
  }
  return out;
}

Mean blockMean(GmanImage const& image) {
  Mean mean{};
  for (std::uint32_t y = 0; y < image.height; ++y) {
    for (std::uint32_t x = 0; x < image.width; ++x) {
      const auto px = image.at(x, y);
      mean[0] += TIFFGetR(px);
      mean[1] += TIFFGetG(px);
      mean[2] += TIFFGetB(px);
    }
  }
  const auto count = static_cast<double>(image.width) * static_cast<double>(image.height);
  for (auto& channel : mean) {
    channel /= count;
  }
  return mean;
}

std::optional<Mean> renderMean(Paths const& paths, std::string const& renderer, std::string const& name,
                               std::string const& fixture, Variant const& variant, Block const& block) {
  const auto rib = std::format("{}_{}.rib", name, renderer);
  const auto tif = std::format("{}_{}.tif", name, renderer);
  std::remove(tif.c_str());
  if (!writeFile(rib, variantRib(fixture, variant, block, tif))) {
    check(false, rib + " writes");
    return std::nullopt;
  }
  const auto status = runGman(paths.gman, {"-r", renderer, rib}).exitStatus;
  check(status == 0, renderer + " renders " + name);
  const auto image = readGmanTIFF(tif);
  check(image.ok, renderer + " " + name + ": TIFF read back");
  if (!image.ok) {
    return std::nullopt;
  }
  check(image.width == kBlockWidth && image.height == kBlockWidth, renderer + " " + name + ": the crop is one " +
                                                                       std::to_string(kBlockWidth) + " by " +
                                                                       std::to_string(kBlockWidth) + " block");
  return blockMean(image);
}

std::optional<Reading> readScene(Paths const& paths, Scene const& scene, std::string const& name,
                                 Variant const& variant = {}) {
  const auto fixture = readFile(paths.ribDir + "/" + scene.fixture + ".rib");
  check(!fixture.empty(), std::string(scene.fixture) + " reads");
  const auto ray = renderMean(paths, kRayTracer, name, fixture, variant, scene.block);
  const auto path = renderMean(paths, kPathTracer, name, fixture, variant, scene.block);
  if (!ray || !path) {
    return std::nullopt;
  }
  Reading reading;
  reading.ray = *ray;
  reading.path = *path;
  return reading;
}

bool within(double a, double b) { return std::abs(a - b) <= kTolerance; }

// Records the disagreement of one channel a check reads.
void note(Reading const& reading, std::size_t channel) {
  largestDisagreement = std::max(largestDisagreement, std::abs(reading.ray[channel] - reading.path[channel]));
}

void checkAgreement(std::string const& label, Reading const& reading) {
  for (std::size_t channel = 0; channel < reading.ray.size(); ++channel) {
    note(reading, channel);
    check(within(reading.ray[channel], reading.path[channel]),
          std::format("{}: channel {} agrees across renderers, ray tracer {:.2f}, path tracer {:.2f}", label, channel,
                      reading.ray[channel], reading.path[channel]));
  }
}

// Each renderer's channel reads ratio times its own reference reading.
void checkRatio(std::string const& label, Reading const& reading, Reading const& reference, double ratio,
                std::size_t channel) {
  note(reading, channel);
  note(reference, channel);
  check(within(reading.ray[channel], ratio * reference.ray[channel]),
        std::format("{}: the ray tracer reads {:.2f}, {} times {:.2f}", label, reading.ray[channel], ratio,
                    reference.ray[channel]));
  check(within(reading.path[channel], ratio * reference.path[channel]),
        std::format("{}: the path tracer reads {:.2f}, {} times {:.2f}", label, reading.path[channel], ratio,
                    reference.path[channel]));
}

// Each renderer's channel differs between two readings by more than ten
// tolerances, so the assertions beside it distinguish a real effect.
void checkEffect(std::string const& label, Reading const& a, Reading const& b, std::size_t channel) {
  note(a, channel);
  note(b, channel);
  check(std::abs(a.ray[channel] - b.ray[channel]) > kEffectFactor * kTolerance,
        std::format("{}: the ray tracer's effect {:.2f} exceeds ten tolerances", label,
                    std::abs(a.ray[channel] - b.ray[channel])));
  check(std::abs(a.path[channel] - b.path[channel]) > kEffectFactor * kTolerance,
        std::format("{}: the path tracer's effect {:.2f} exceeds ten tolerances", label,
                    std::abs(a.path[channel] - b.path[channel])));
}

void checkPane(Paths const& paths) {
  const auto defaults = readScene(paths, kPane, "pane_default");
  const auto kr0 = readScene(paths, kPane, "pane_kr0", {.settings = {.kr = 0.0}});
  const auto kr05 = readScene(paths, kPane, "pane_kr05", {.settings = {.kr = kHalf}});
  const auto kt0 = readScene(paths, kPane, "pane_kt0", {.settings = {.kt = 0.0}});
  const auto kt05 = readScene(paths, kPane, "pane_kt05", {.settings = {.kt = kHalf}});
  const auto eta1 = readScene(paths, kPane, "pane_eta1", {.settings = {.eta = kIndexMatched}});
  const auto noPanel = readScene(paths, kPane, "pane_nopanel", {.without = {"panel"}});
  const auto noBackdrop = readScene(paths, kPane, "pane_nobackdrop", {.without = {"backdrop"}});
  const auto noPane = readScene(paths, kPane, "pane_nopane", {.without = {"pane"}});
  if (!defaults || !kr0 || !kr05 || !kt0 || !kt05 || !eta1 || !noPanel || !noBackdrop || !noPane) {
    return;
  }

  checkAgreement("pane at defaults", *defaults);

  checkEffect("Kr 0 against the twin without the panel", *defaults, *noPanel, kBlue);
  checkRatio("Kr 0 leaves the twin without the panel's reflection", *kr0, *noPanel, 1.0, kBlue);
  checkRatio("Kr 0.5 halves the reflection", *kr05, *defaults, kHalf, kBlue);

  checkEffect("Kt 0 against the twin without the backdrop", *defaults, *noBackdrop, kRed);
  checkRatio("Kt 0 leaves the twin without the backdrop's transmission", *kt0, *noBackdrop, 1.0, kRed);
  checkRatio("Kt 0.5 quarters the transmission, one Kt per face", *kt05, *defaults, kHalf * kHalf, kRed);

  checkEffect("eta 1 against the pane's reflection", *defaults, *noPane, kBlue);
  checkEffect("eta 1 against the pane's transmission", *defaults, *noPane, kRed);
  checkRatio("eta 1 removes the pane's reflection", *eta1, *noPane, 1.0, kBlue);
  checkRatio("eta 1 removes the pane's transmission", *eta1, *noPane, 1.0, kRed);
}

void checkSphere(Paths const& paths) {
  const auto first = readScene(paths, kSphere, "sphere_first", {.settings = {.eta = kIndex}});
  const auto second = readScene(paths, kSphere, "sphere_second", {.settings = {.eta = kSecondIndex}});
  if (!first || !second) {
    return;
  }

  checkAgreement("sphere at eta 1.5", *first);
  checkAgreement("sphere at eta 1.2", *second);
  checkEffect("eta 1.5 against 1.2 in the red channel", *first, *second, kRed);
  checkEffect("eta 1.5 against 1.2 in the blue channel", *first, *second, kBlue);
}

double reflectance(double index) {
  const auto ratio = (index - 1.0) / (index + 1.0);
  return ratio * ratio;
}

void checkShadow(Paths const& paths) {
  const auto bare = readScene(paths, kShadow, "shadow_bare", {.without = {"pane"}});
  const auto defaults = readScene(paths, kShadow, "shadow_default");
  const auto kt05 = readScene(paths, kShadow, "shadow_kt05", {.settings = {.kt = kHalf}});
  const auto kt0 = readScene(paths, kShadow, "shadow_kt0", {.settings = {.kt = 0.0}});
  const auto kr0 = readScene(paths, kShadow, "shadow_kr0", {.settings = {.kr = 0.0}});
  const auto eta1 = readScene(paths, kShadow, "shadow_eta1", {.settings = {.eta = kIndexMatched}});
  if (!bare || !defaults || !kt05 || !kt0 || !kr0 || !eta1) {
    return;
  }

  const auto pass = 1.0 - reflectance(kIndex);
  checkAgreement("shadow at defaults", *defaults);
  checkAgreement("shadow at Kt 0.5", *kt05);
  checkAgreement("shadow at Kt 0", *kt0);
  checkAgreement("shadow at Kr 0", *kr0);
  checkAgreement("shadow at eta 1", *eta1);

  checkEffect("Kt 1 against the unshadowed receiver", *bare, *defaults, kRed);
  checkEffect("Kt 0.5 against the unshadowed receiver", *bare, *kt05, kRed);
  checkEffect("Kt 0 against the unshadowed receiver", *bare, *kt0, kRed);
  checkRatio("Kt 1 passes (1 - F)^2 of the light", *defaults, *bare, pass * pass, kRed);
  checkRatio("Kt 0.5 passes (Kt * (1 - F))^2 of the light", *kt05, *bare, kHalf * pass * kHalf * pass, kRed);
  checkRatio("Kt 0 passes none of the light", *kt0, *bare, 0.0, kRed);
  checkRatio("Kr 0 leaves the light the pane passes", *kr0, *bare, pass * pass, kRed);
  checkRatio("eta 1 passes all the light", *eta1, *bare, 1.0, kRed);
}

} // namespace

int main(int argc, char* argv[]) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s <gman-binary> <tests/rib-dir>\n", argv[0]);
    return 2;
  }
  const auto paths = Paths{argv[1], argv[2]};

  checkPane(paths);
  checkSphere(paths);
  checkShadow(paths);

  std::printf("info: the renderers' largest disagreement over every block and channel read is %.3f; kTolerance is "
              "%.3f\n",
              largestDisagreement, kTolerance);

  return checkSummary("glass's eta, Kr and Kt move the ray tracer and the path tracer alike, shadows included");
}
