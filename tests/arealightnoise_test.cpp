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
 * The ray tracer's area-light noise near an emitter and far from it. Near:
 * tests/rib/arealight_nearfield.rib's matte sphere 0.3 above a large panel,
 * and a copy with a radius-4 disk in the panel's place, each read on the
 * sphere's lit underside, where a draw's weight varies most across the
 * emitter. Far: tests/rib/rayarealight_panel.rib and rayarealight_disk.rib
 * without their blocker, read on the wall's lit block. Each case renders at a
 * low draw
 * count and at 1,024 draws, every reference pixel in its block exceeds 16
 * levels, and the root-mean-square over the block's pixels and channels of
 * (low - reference) / reference stays within its bound: 0.12 at 16 draws near
 * the emitter, 0.02 at 4 draws far from it.
 */

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <limits>
#include <map>
#include <sstream>
#include <string>
#include <vector>

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

constexpr Block kUnderside = {26, 26, 12, 10};
constexpr Block kWall = {24, 4, 16, 8};
constexpr std::uint32_t kNearDraws = 16;
constexpr std::uint32_t kFarDraws = 4;
constexpr std::uint32_t kReferenceDraws = 1024;
constexpr double kNearBound = 0.12;
constexpr double kFarBound = 0.02;
constexpr double kSetupFloor = 16.0;
constexpr std::size_t kChannels = 3;

constexpr char const* kRayTracer = "gmanraytracer";
constexpr char const* kNearFixture = "arealight_nearfield";
constexpr char const* kDrawsPrefix = "Option \"raytracer\" \"integer arealightsamples\"";
constexpr char const* kDisplayPrefix = "Display ";
constexpr char const* kBlockerLine = "Translate 0 1.4 -2";
constexpr char const* kPanelLine = "Polygon \"P\" [-4.2 -1.3 -3.2  -4.2 -1.3 3.2  4.2 -1.3 3.2  4.2 -1.3 -3.2]";
constexpr char const* kDiskLines = "Translate 0 -1.3 0\nRotate 90 1 0 0\nDisk 0 4 360";

// One noise case: the fixture its variant copies, the lines it replaces (a
// value may hold several lines), whether it drops the blocker's attribute
// block, the block it reads, its low draw count and its bound.
struct NoiseCase {
  std::string name;
  std::string fixture;
  std::map<std::string, std::string> edits;
  bool dropBlocker;
  Block block;
  std::uint32_t draws;
  double bound;
};

std::string readFile(std::string const& path) {
  std::ifstream in(path);
  std::ostringstream contents;
  contents << in.rdbuf();
  return contents.str();
}

void writeFile(std::string const& path, std::string const& text) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out << text;
}

bool startsWith(std::string const& line, std::string const& prefix) { return line.rfind(prefix, 0) == 0; }

// The variant of c at draws draws, writing name.tif: c's fixture with its
// Display and draw-count lines rewritten, each line that equals a key of its
// edits replaced by that key's value and, when c drops the blocker, the
// AttributeBegin block holding kBlockerLine dropped. An edit, a Display line,
// a draw-count line or a blocker the fixture lacks fails a check, so a
// variant never renders unedited.
std::string variantText(std::string const& ribDir, NoiseCase const& c, std::uint32_t draws, std::string const& name) {
  std::istringstream in(readFile(ribDir + "/" + c.fixture + ".rib"));
  std::ostringstream out;
  std::map<std::string, bool> found;
  bool display = false;
  bool count = false;
  bool blockerDropped = false;
  std::vector<std::string> attributeBlock;
  std::string line;
  while (std::getline(in, line)) {
    if (startsWith(line, kDisplayPrefix)) {
      line = "Display \"" + name + ".tif\" \"file\" \"rgba\"";
      display = true;
    } else if (startsWith(line, kDrawsPrefix)) {
      line = std::string(kDrawsPrefix) + " [" + std::to_string(draws) + "]";
      count = true;
    } else if (auto const edit = c.edits.find(line); edit != c.edits.end()) {
      found[line] = true;
      line = edit->second;
    }
    if (c.dropBlocker && (line == "AttributeBegin" || !attributeBlock.empty())) {
      attributeBlock.push_back(line);
      if (line != "AttributeEnd") {
        continue;
      }
      bool blocker = false;
      for (std::string const& held : attributeBlock) {
        blocker = blocker || held == kBlockerLine;
      }
      blockerDropped = blockerDropped || blocker;
      if (!blocker) {
        for (std::string const& held : attributeBlock) {
          out << held << "\n";
        }
      }
      attributeBlock.clear();
      continue;
    }
    out << line << "\n";
  }
  for (auto const& edit : c.edits) {
    check(found[edit.first], c.name + ": the fixture holds the line " + edit.first);
  }
  check(display && count, c.name + ": the fixture holds a Display line and a draw-count line");
  if (c.dropBlocker) {
    check(blockerDropped, c.name + ": the fixture's blocker is dropped");
  }
  return out.str();
}

double channelOf(std::uint32_t pixel, std::size_t channel) {
  if (channel == 0) {
    return TIFFGetR(pixel);
  }
  if (channel == 1) {
    return TIFFGetG(pixel);
  }
  return TIFFGetB(pixel);
}

GmanImage render(std::string const& gman, std::string const& ribDir, NoiseCase const& c, std::uint32_t draws) {
  const auto name = c.name + "_" + std::to_string(draws);
  writeFile(name + ".rib", variantText(ribDir, c, draws, name));
  std::remove((name + ".tif").c_str());
  GMANRunResult const run = runGman(gman, {"-r", kRayTracer, name + ".rib"});
  check(run.exitStatus == 0, name + " renders (exit " + std::to_string(run.exitStatus) + ")");
  GmanImage image = readGmanTIFF(name + ".tif");
  check(image.ok, name + ".tif reads back");
  return image;
}

// Renders c at its draw count and at kReferenceDraws, and checks the block's
// relative root-mean-square deviation against its bound.
void checkCase(std::string const& gman, std::string const& ribDir, NoiseCase const& c) {
  GmanImage const low = render(gman, ribDir, c, c.draws);
  GmanImage const reference = render(gman, ribDir, c, kReferenceDraws);
  if (!low.ok || !reference.ok) {
    return;
  }
  double sum = 0.0;
  double floor = std::numeric_limits<double>::max();
  for (std::uint32_t y = c.block.y; y < c.block.y + c.block.height; ++y) {
    for (std::uint32_t x = c.block.x; x < c.block.x + c.block.width; ++x) {
      for (std::size_t channel = 0; channel < kChannels; ++channel) {
        const auto expected = channelOf(reference.at(x, y), channel);
        const auto deviation = (channelOf(low.at(x, y), channel) - expected) / expected;
        floor = std::min(floor, expected);
        sum += deviation * deviation;
      }
    }
  }
  const auto rms =
      std::sqrt(sum / (static_cast<double>(c.block.width) * static_cast<double>(c.block.height) * kChannels));
  std::printf("%s: rms %.4f at %u draws against %u, bound %.2f; darkest reference level %.0f\n", c.name.c_str(), rms,
              c.draws, kReferenceDraws, c.bound, floor);
  check(floor > kSetupFloor, c.name + " setup: every reference pixel in the block exceeds 16 levels");
  check(rms <= c.bound, c.name + ": the block's relative rms at " + std::to_string(c.draws) + " draws is at most " +
                            std::to_string(c.bound) + " (" + std::to_string(rms) + ")");
}

} // namespace

int main(int argc, char* argv[]) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s <gman-binary> <tests/rib dir>\n", argv[0]);
    return 2;
  }
  std::string const gman = argv[1];
  std::string const ribDir = argv[2];

  std::vector<NoiseCase> const cases = {
      {"nearfield_panel", kNearFixture, {}, false, kUnderside, kNearDraws, kNearBound},
      {"nearfield_disk", kNearFixture, {{kPanelLine, kDiskLines}}, false, kUnderside, kNearDraws, kNearBound},
      {"farfield_panel", "rayarealight_panel", {}, true, kWall, kFarDraws, kFarBound},
      {"farfield_disk", "rayarealight_disk", {}, true, kWall, kFarDraws, kFarBound},
  };
  for (NoiseCase const& c : cases) {
    checkCase(gman, ribDir, c);
  }

  return checkSummary("the ray tracer's area-light noise stays within its bounds near an emitter and far from it");
}
