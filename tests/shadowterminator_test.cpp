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

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <numbers>
#include <string>
#include <vector>

#include <tiffio.h>

#include "check.h"
#include "goldenimage.h"
#include "rungman.h"

namespace {

constexpr double kRadius = 0.75;
constexpr std::size_t kImageSize = 64;
constexpr double kHalfWidth = 1.0;
constexpr double kPrismFacets = 8.0;
constexpr double kFacetAngle = 2.0 * std::numbers::pi / kPrismFacets;
constexpr double kTiltAngle = std::numbers::pi / 32.0;

constexpr double kRatioTolerance = 0.1;
constexpr double kShadowedFraction = 0.05;
constexpr char const* kSummary =
    "a smooth mesh's terminator renders lit, its back side stays lit and a blocker still shadows it";

struct Band {
  uint32_t firstColumn;
  uint32_t lastColumn;
  uint32_t firstRow;
  uint32_t lastRow;
};

constexpr Band kTerminatorBand{33, 37, 16, 47};
constexpr Band kLitFaceBand{40, 51, 16, 47};
constexpr Band kBackSideBand{16, 47, 16, 47};

struct Vertex {
  double x;
  double z;
};

struct Direction {
  double x;
  double z;
};

double columnX(uint32_t column) {
  return -kHalfWidth + (column + 0.5) * (2.0 * kHalfWidth / static_cast<double>(kImageSize));
}

Vertex ringVertex(double angle) { return {kRadius * std::sin(angle), kRadius * std::cos(angle)}; }

std::vector<Vertex> prismRing() {
  std::vector<Vertex> ring;
  double const first = std::numbers::pi + kTiltAngle + kFacetAngle / 2.0;
  for (int k = 0; k <= static_cast<int>(kPrismFacets); ++k) {
    ring.push_back(ringVertex(first + k * kFacetAngle));
  }
  return ring;
}

std::vector<Vertex> halfShellRing() {
  std::vector<Vertex> ring;
  for (int k = -2; k <= 2; ++k) {
    ring.push_back(ringVertex(k * kFacetAngle));
  }
  return ring;
}

Direction outwardThroughNearestFace(std::vector<Vertex> const& ring, double x) {
  Vertex nearest{0.0, 0.0};
  bool found = false;
  for (std::size_t i = 0; i + 1 < ring.size(); ++i) {
    Vertex const& a = ring[i];
    Vertex const& b = ring[i + 1];
    if (a.x == b.x || x < std::fmin(a.x, b.x) || x > std::fmax(a.x, b.x)) {
      continue;
    }
    double const along = (x - a.x) / (b.x - a.x);
    Vertex const hit{x, a.z + along * (b.z - a.z)};
    if (!found || hit.z < nearest.z) {
      nearest = hit;
      found = true;
    }
  }
  double const length = std::hypot(nearest.x, nearest.z);
  return {nearest.x / length, nearest.z / length};
}

double predictedPrism(double x) {
  Direction const outward = outwardThroughNearestFace(prismRing(), x);
  return std::fmax(0.0, outward.x);
}

double predictedHalfShell(double x) {
  Direction const outward = outwardThroughNearestFace(halfShellRing(), x);
  return std::fmax(0.0, outward.z);
}

template <typename Predict> double predictedMean(Band const& band, Predict predict) {
  double sum = 0.0;
  std::size_t count = 0;
  for (uint32_t row = band.firstRow; row <= band.lastRow; ++row) {
    for (uint32_t column = band.firstColumn; column <= band.lastColumn; ++column) {
      sum += predict(columnX(column));
      ++count;
    }
  }
  return sum / static_cast<double>(count);
}

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

void checkBlockerShadows(GmanImage const& blocked, double litFace) {
  for (Band const& band : {kTerminatorBand, kLitFaceBand}) {
    double const mean = measuredMean(blocked, band);
    check(mean <= kShadowedFraction * litFace, "blocked: columns " + std::to_string(band.firstColumn) + "-" +
                                                   std::to_string(band.lastColumn) + " stay shadowed, mean " +
                                                   std::to_string(mean) + " against the unblocked face " +
                                                   std::to_string(litFace));
  }
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

  GmanImage const prism = render(gman, renderer, ribDir, "shadowterminator");
  GmanImage const backside = render(gman, renderer, ribDir, "shadowterminator_backside");
  GmanImage const blocked = render(gman, renderer, ribDir, "shadowterminator_blocked");
  if (!usable(prism) || !usable(backside) || !usable(blocked)) {
    return checkSummary(kSummary);
  }

  double const litFace = measuredMean(prism, kLitFaceBand);
  double const litFacePredicted = predictedMean(kLitFaceBand, predictedPrism);
  check(litFace > 0.0, "the face turned to the light is lit, mean " + std::to_string(litFace));
  if (litFace > 0.0) {
    double const litFaceScale = litFace / litFacePredicted;

    double const terminator = measuredMean(prism, kTerminatorBand);
    double const terminatorRatio = terminator / predictedMean(kTerminatorBand, predictedPrism) / litFaceScale;
    check(std::fabs(terminatorRatio - 1.0) <= kRatioTolerance,
          "terminator band: shading prediction met, ratio " + std::to_string(terminatorRatio) + " against 1 within " +
              std::to_string(kRatioTolerance));

    double const backSide = measuredMean(backside, kBackSideBand);
    double const backSideRatio = backSide / predictedMean(kBackSideBand, predictedHalfShell) / litFaceScale;
    check(backSideRatio >= 1.0 - kRatioTolerance, "back side: stays lit, ratio " + std::to_string(backSideRatio) +
                                                      " at least " + std::to_string(1.0 - kRatioTolerance));

    checkBlockerShadows(blocked, litFace);
  }

  return checkSummary(kSummary);
}
