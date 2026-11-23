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
#include <numbers>
#include <string>
#include <vector>

#include <tiffio.h>

#include "check.h"
#include "goldenimage.h"
#include "rungman.h"

namespace {

constexpr std::size_t kImageSize = 64;
constexpr double kHalfWidth = 1.0;
constexpr double kEdgeMargin = 2.0 / 32.0;

constexpr double kRadius = 0.75;
constexpr int kPrismFacets = 8;
constexpr double kFacetAngle = 2.0 * std::numbers::pi / kPrismFacets;
constexpr double kFirstVertexAngle = kFacetAngle / 2.0;
constexpr uint32_t kFirstRow = 16;
constexpr uint32_t kLastRow = 47;

constexpr double kHoleHalfWidth = 0.3;
constexpr double kFaceHalfWidth = 0.8;
constexpr double kHoleNormalTilt = std::numbers::pi / 3.0;
constexpr uint32_t kCentreColumn = 32;
constexpr uint32_t kCentreRow = 32;

constexpr double kTolerance = 0.02;
constexpr char const* kSummary =
    "a polygon's per-vertex normals shade it smoothly, whichever way it winds and across its holes";

struct Pixel {
  uint32_t column;
  uint32_t row;
};

struct Vertex {
  double x;
  double z;
};

struct Distance {
  double max;
  double mean;
};

double columnX(uint32_t column) {
  return -kHalfWidth + (column + 0.5) * (2.0 * kHalfWidth / static_cast<double>(kImageSize));
}

double rowY(uint32_t row) { return kHalfWidth - (row + 0.5) * (2.0 * kHalfWidth / static_cast<double>(kImageSize)); }

std::vector<Vertex> prismRing() {
  std::vector<Vertex> ring;
  for (int k = 0; k <= kPrismFacets; ++k) {
    double const angle = kFirstVertexAngle + k * kFacetAngle;
    ring.push_back({kRadius * std::sin(angle), -kRadius * std::cos(angle)});
  }
  return ring;
}

double silhouetteX(std::vector<Vertex> const& ring) {
  double widest = 0.0;
  for (Vertex const& vertex : ring) {
    widest = std::fmax(widest, std::fabs(vertex.x));
  }
  return widest;
}

double predictedPrism(std::vector<Vertex> const& ring, double x) {
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
  return std::fmax(0.0, -nearest.z / std::hypot(nearest.x, nearest.z));
}

std::vector<Pixel> prismBand(std::vector<Vertex> const& ring) {
  std::vector<Pixel> band;
  double const reach = silhouetteX(ring) - kEdgeMargin;
  for (uint32_t row = kFirstRow; row <= kLastRow; ++row) {
    for (uint32_t column = 0; column < kImageSize; ++column) {
      if (std::fabs(columnX(column)) <= reach) {
        band.push_back({column, row});
      }
    }
  }
  return band;
}

std::vector<Pixel> holeBand() {
  std::vector<Pixel> band;
  for (uint32_t row = 0; row < kImageSize; ++row) {
    for (uint32_t column = 0; column < kImageSize; ++column) {
      double const reach = std::fmax(std::fabs(columnX(column)), std::fabs(rowY(row)));
      if (reach >= kHoleHalfWidth + kEdgeMargin && reach <= kFaceHalfWidth - kEdgeMargin) {
        band.push_back({column, row});
      }
    }
  }
  return band;
}

template <typename Predict>
Distance distanceFromPrediction(GmanImage const& image, std::vector<Pixel> const& band, Predict predict) {
  Distance distance{0.0, 0.0};
  double sum = 0.0;
  for (Pixel const& pixel : band) {
    double const measured = TIFFGetR(image.at(pixel.column, pixel.row)) / 255.0;
    double const gap = std::fabs(measured - predict(pixel));
    distance.max = std::fmax(distance.max, gap);
    sum += gap;
  }
  distance.mean = sum / static_cast<double>(band.size());
  return distance;
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

void checkPrism(std::string const& gman, std::string const& renderer, std::string const& ribDir,
                std::string const& name) {
  GmanImage const image = render(gman, renderer, ribDir, name);
  if (!usable(image)) {
    return;
  }
  std::vector<Vertex> const ring = prismRing();
  std::vector<Pixel> const band = prismBand(ring);
  check(band.size() == 40 * (kLastRow - kFirstRow + 1),
        name + ": the band spans 40 columns of rows " + std::to_string(kFirstRow) + "-" + std::to_string(kLastRow));
  Distance const distance = distanceFromPrediction(
      image, band, [&ring](Pixel const& pixel) { return predictedPrism(ring, columnX(pixel.column)); });
  check(distance.max <= kTolerance, name + ": every band pixel within " + std::to_string(kTolerance) +
                                        " of its smooth prediction, max " + std::to_string(distance.max) + " mean " +
                                        std::to_string(distance.mean));
}

void checkHoleVertexNormals(std::string const& gman, std::string const& renderer, std::string const& ribDir) {
  std::string const name = "zbuffervertexnormals_hole";
  GmanImage const image = render(gman, renderer, ribDir, name);
  if (!usable(image)) {
    return;
  }
  double const tilted = std::cos(kHoleNormalTilt);
  Distance const distance = distanceFromPrediction(image, holeBand(), [tilted](Pixel const&) { return tilted; });
  check(distance.max <= kTolerance, name + ": every pixel between the hole and the edge within " +
                                        std::to_string(kTolerance) + " of the tilted normal's " +
                                        std::to_string(tilted) + ", max " + std::to_string(distance.max) + " mean " +
                                        std::to_string(distance.mean));
  check(TIFFGetR(image.at(kCentreColumn, kCentreRow)) == 0, name + ": the hole stays open");
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

  checkPrism(gman, renderer, ribDir, "zbuffervertexnormals");
  checkPrism(gman, renderer, ribDir, "zbuffervertexnormals_reversed");
  if (renderer == "gmanzbuffer") {
    checkHoleVertexNormals(gman, renderer, ribDir);
  }

  return checkSummary(kSummary);
}
