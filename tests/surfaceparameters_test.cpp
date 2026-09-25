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
 * Proves every surface shades with the parameter list its own RiSurface
 * passed, under both renderers. Three fixtures, each a distinct form of
 * the defect measured 2026-09-22: two surfaces naming one shader, a
 * surface restored by AttributeEnd, and the default surface after a
 * parameterized "matte". Every fixture puts its dim sphere (Kd 0.1) at
 * x=-1.2 (pixel column 50) and its bright one (Kd 1, the default matte's
 * included) at x=1.2 (column 150); Ks 0 and no ambient light make
 * brightness linear in Kd, so a correct render's dim/bright ratio sits
 * near 0.1. A build that shares one list across surfaces instead puts
 * both spheres near the same brightness, ratio near 1 -- well outside the
 * tolerance below. That reproduces on fixture 1 only under the ray
 * tracer, and on fixtures 2 and 3 under both
 * renderers.
 */

#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>

#include <sys/wait.h>

#include <tiffio.h>

#include "check.h"
#include "goldenimage.h"

namespace {

constexpr int kLeftX = 50;
constexpr int kRightX = 150;
constexpr int kY = 50;
constexpr int kSampleRadius = 1; // averages a 3x3 window, centred on kY

// The predicted ratio is 0.1 (Kd 0.1 over Kd 1); this window is wide
// enough to clear the measured rounding (0.0986) and still reject the
// defect's near-1 ratio by a wide margin.
constexpr double kRatioLow = 0.03;
constexpr double kRatioHigh = 0.3;

struct Fixture {
  char const* name;  // base filename under tests/rib/, without ".rib"
  char const* label; // names the surface arrangement this fixture pins
  bool leftIsDim;    // true: the Kd 0.1 sphere sits at x=-1.2 (column 50)
};

Fixture const kFixtures[] = {
    {"surfaceparameters_two_surfaces", "fixture 1 (two surfaces, one shader)", true},
    {"surfaceparameters_restored", "fixture 2 (a surface restored by AttributeEnd)", true},
    {"surfaceparameters_default", "fixture 3 (the default surface)", false},
};

int runGman(std::string const& command) {
  int const status = std::system(command.c_str());
  return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

double averageRed(GmanImage const& img, int cx, int cy) {
  double sum = 0;
  int count = 0;
  for (int dy = -kSampleRadius; dy <= kSampleRadius; ++dy) {
    for (int dx = -kSampleRadius; dx <= kSampleRadius; ++dx) {
      sum += TIFFGetR(img.at((uint32_t)(cx + dx), (uint32_t)(cy + dy)));
      ++count;
    }
  }
  return sum / count;
}

// Asserts img's two spheres shade with their own Kd: the dim/bright ratio
// their positions predict, within (kRatioLow, kRatioHigh).
void checkRatio(Fixture const& fx, char const* renderer, GmanImage const& img) {
  double const leftAvg = averageRed(img, kLeftX, kY);
  double const rightAvg = averageRed(img, kRightX, kY);
  double const dimAvg = fx.leftIsDim ? leftAvg : rightAvg;
  double const brightAvg = fx.leftIsDim ? rightAvg : leftAvg;
  double const ratio = dimAvg / brightAvg;
  std::printf("%s under %s: dim=%.2f bright=%.2f ratio=%.4f (want (%.2f, %.2f))\n", fx.label, renderer, dimAvg,
              brightAvg, ratio, kRatioLow, kRatioHigh);
  check(ratio > kRatioLow && ratio < kRatioHigh,
        std::string(fx.label) + " under " + renderer + ": each sphere shades with its own surface's parameters");
}

} // namespace

int main(int argc, char* argv[]) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s <gman-binary> <rib-dir>\n", argv[0]);
    return 2;
  }
  std::string const gman = argv[1];
  std::string const ribDir = argv[2];

  for (Fixture const& fx : kFixtures) {
    std::string const rib = ribDir + "/" + fx.name + ".rib";
    std::string const outTif = std::string(fx.name) + ".tif";
    std::string const rayTif = std::string(fx.name) + "_raytracer.tif";
    std::string const zTif = std::string(fx.name) + "_zbuffer.tif";

    std::remove(outTif.c_str());
    std::remove(rayTif.c_str());
    std::remove(zTif.c_str());

    int const rayStatus = runGman("\"" + gman + "\" -r gmanraytracer \"" + rib + "\" >/dev/null 2>&1");
    check(rayStatus == 0,
          std::string(fx.label) + ": renders under -r gmanraytracer (exit " + std::to_string(rayStatus) + ")");
    check(std::rename(outTif.c_str(), rayTif.c_str()) == 0,
          std::string(fx.label) + ": raytracer output renames to " + rayTif);

    int const zStatus = runGman("\"" + gman + "\" \"" + rib + "\" >/dev/null 2>&1");
    check(zStatus == 0,
          std::string(fx.label) + ": renders under the default z-buffer (exit " + std::to_string(zStatus) + ")");
    check(std::rename(outTif.c_str(), zTif.c_str()) == 0,
          std::string(fx.label) + ": z-buffer output renames to " + zTif);

    GmanImage const rayImg = readGmanTIFF(rayTif);
    GmanImage const zImg = readGmanTIFF(zTif);
    check(rayImg.ok, std::string(fx.label) + ": raytracer image reads back");
    check(zImg.ok, std::string(fx.label) + ": z-buffer image reads back");
    if (!rayImg.ok || !zImg.ok) {
      continue;
    }

    checkRatio(fx, "gmanraytracer", rayImg);
    checkRatio(fx, "z-buffer", zImg);
  }

  return checkSummary("Every surface shades with its own RiSurface's parameter list, under both renderers");
}
