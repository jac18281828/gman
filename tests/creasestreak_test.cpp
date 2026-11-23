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

constexpr std::size_t kImageSize = 64;
constexpr double kRatioTolerance = 0.1;
constexpr double kShadowedFraction = 0.05;
constexpr char const* kSummary =
    "a shadow ray leaving a mesh's facets passes the facet it exits through and nothing else";

struct Band {
  uint32_t firstColumn;
  uint32_t lastColumn;
  uint32_t firstRow;
  uint32_t lastRow;
};

constexpr Band kTerminatorBand{22, 30, 16, 47};
constexpr Band kLitFaceBand{34, 46, 16, 47};
constexpr Band kBackSideBand{18, 45, 16, 47};

double measuredMean(GmanImage const& image, Band const& band) {
  double sum = 0.0;
  std::size_t count = 0;
  for (uint32_t row = band.firstRow; row <= band.lastRow; ++row) {
    for (uint32_t column = band.firstColumn; column <= band.lastColumn; ++column) {
      sum += TIFFGetR(image.at(column, row)) / 255.0;
      ++count;
    }
  }
  return sum / static_cast<double>(count);
}

bool usable(GmanImage const& image) { return image.ok && image.width == kImageSize && image.height == kImageSize; }

GmanImage render(std::string const& gman, std::string const& renderer, std::string const& ribDir,
                 std::string const& name) {
  std::remove((name + ".tif").c_str());
  int const status = runGman(gman, {"-r", renderer, ribDir + "/" + name + ".rib"}).exitStatus;
  check(status == 0, name + ": renders");
  GmanImage image = readGmanTIFF(name + ".tif");
  check(usable(image), name + ": TIFF read back at 64 x 64");
  return image;
}

void checkRatioNearOne(std::string const& label, double mean, double reference) {
  double const ratio = mean / reference;
  check(std::fabs(ratio - 1.0) <= kRatioTolerance, label + ": terminator band matches the open reference, ratio " +
                                                       std::to_string(ratio) + " against 1 within " +
                                                       std::to_string(kRatioTolerance));
}

void checkStaysDark(std::string const& label, double mean, double reference) {
  check(mean <= kShadowedFraction * reference, label + ": stays shadowed, mean " + std::to_string(mean) +
                                                   " against the reference " + std::to_string(reference));
}

} // namespace

int main(int argc, char* argv[]) {
  if (argc < 4) {
    std::fprintf(stderr, "usage: %s <gman-binary> <tests/rib-dir> <renderer>\n", argv[0]);
    return 2;
  }
  std::string const gman = argv[1];
  std::string const ribDir = argv[2];
  std::string const renderer = argv[3];

  GmanImage const fold = render(gman, renderer, ribDir, "creasestreak");
  GmanImage const open = render(gman, renderer, ribDir, "creasestreak_open");
  GmanImage const reversed = render(gman, renderer, ribDir, "creasestreak_reversed");
  GmanImage const blocked = render(gman, renderer, ribDir, "creasestreak_blocked");
  GmanImage const far = render(gman, renderer, ribDir, "creasestreak_far");
  GmanImage const facing = render(gman, renderer, ribDir, "creasestreak_facing");
  GmanImage const other = render(gman, renderer, ribDir, "creasestreak_other");
  GmanImage const backside = render(gman, renderer, ribDir, "creasestreak_backside");
  GmanImage const backsideOpen = render(gman, renderer, ribDir, "creasestreak_backside_open");
  for (GmanImage const* image : {&fold, &open, &reversed, &blocked, &far, &facing, &other, &backside, &backsideOpen}) {
    if (!usable(*image)) {
      return checkSummary(kSummary);
    }
  }

  double const openTerminator = measuredMean(open, kTerminatorBand);
  check(openTerminator > 0.0, "the open reference's terminator band is lit, mean " + std::to_string(openTerminator));
  if (openTerminator > 0.0) {
    checkRatioNearOne("fold", measuredMean(fold, kTerminatorBand), openTerminator);
    checkRatioNearOne("reversed fold", measuredMean(reversed, kTerminatorBand), openTerminator);

    double const litFace = measuredMean(fold, kLitFaceBand);
    check(litFace > 0.0, "the fold's face turned to the light is lit, mean " + std::to_string(litFace));
    if (litFace > 0.0) {
      checkStaysDark("blocked, terminator band", measuredMean(blocked, kTerminatorBand), openTerminator);
      checkStaysDark("blocked, lit face", measuredMean(blocked, kLitFaceBand), litFace);
    }

    checkStaysDark("far, terminator band", measuredMean(far, kTerminatorBand), openTerminator);
    checkStaysDark("facing, terminator band", measuredMean(facing, kTerminatorBand), openTerminator);
    checkStaysDark("other, terminator band", measuredMean(other, kTerminatorBand), openTerminator);
  }

  double const backsideReference = measuredMean(backsideOpen, kBackSideBand);
  check(backsideReference > 0.0, "the back side reference is lit, mean " + std::to_string(backsideReference));
  if (backsideReference > 0.0) {
    checkStaysDark("back side", measuredMean(backside, kBackSideBand), backsideReference);
  }

  return checkSummary(kSummary);
}
