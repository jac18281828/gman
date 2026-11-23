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

#include <algorithm>
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
constexpr double kFacetAngle = std::numbers::pi / 4.0;
constexpr double kTiltAngle = std::numbers::pi / 32.0;
constexpr int kVertexColumns = 6;

constexpr unsigned char kTexelRed = 191;
constexpr unsigned char kTexelGreen = 128;
constexpr unsigned char kTexelBlue = 238;

constexpr double kLitFaceTolerance = 0.02;
constexpr double kBeyondTerminatorFraction = 0.05;
constexpr std::size_t kBounceImageSize = 48;
constexpr uint32_t kInteriorHalfWidth = 9;
constexpr double kBounceBlack = 1.0 / 255.0;
constexpr char const* kPathTracer = "gmanpathtracer";
constexpr char const* kSummary =
    "a normal map's light fades to zero at the smooth terminator, and the lit face keeps its light";

struct Band {
  uint32_t firstColumn;
  uint32_t lastColumn;
  uint32_t firstRow;
  uint32_t lastRow;
};

constexpr Band kBeyondTerminatorBand{26, 30, 16, 47};
constexpr Band kLitFaceBand{44, 49, 16, 47};

struct Vertex {
  double x;
  double z;
};

struct Direction {
  double x;
  double z;
};

double decode(unsigned char channel) { return 2.0 * (channel / 255.0) - 1.0; }

double columnX(uint32_t column) {
  return -kHalfWidth + (column + 0.5) * (2.0 * kHalfWidth / static_cast<double>(kImageSize));
}

std::vector<Vertex> prismRing() {
  std::vector<Vertex> ring;
  for (int k = 0; k < kVertexColumns; ++k) {
    double const angle = kTiltAngle + (k - 2.5) * kFacetAngle;
    ring.push_back({kRadius * std::sin(angle), -kRadius * std::cos(angle)});
  }
  return ring;
}

Direction radialThroughNearestFace(std::vector<Vertex> const& ring, double x) {
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

double bumpTerm(double baseDotLight, double bentDotLight, double baseDotBent) {
  double const g = baseDotLight / (bentDotLight * baseDotBent);
  if (g <= 0.0) {
    return 0.0;
  }
  if (g >= 1.0) {
    return 1.0;
  }
  return -g * g * g + g * g + g;
}

double predictedMapped(double x) {
  Direction const normal = radialThroughNearestFace(prismRing(), x);
  double const tilt = std::atan2(decode(kTexelRed), decode(kTexelBlue));
  Direction const tangent{-normal.z, normal.x};
  Direction const bent{std::cos(tilt) * normal.x + std::sin(tilt) * tangent.x,
                       std::cos(tilt) * normal.z + std::sin(tilt) * tangent.z};
  double const bentDotLight = bent.x;
  return std::fmax(0.0, bentDotLight) * bumpTerm(normal.x, bentDotLight, std::cos(tilt));
}

double predictedUnmapped(double x) { return std::fmax(0.0, radialThroughNearestFace(prismRing(), x).x); }

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

double interiorMean(GmanImage const& image) {
  uint32_t const first = image.width / 2 - kInteriorHalfWidth;
  uint32_t const last = image.width / 2 + kInteriorHalfWidth;
  double sum = 0.0;
  for (uint32_t y = first; y < last; ++y) {
    for (uint32_t x = first; x < last; ++x) {
      sum += TIFFGetR(image.at(x, y)) / 255.0;
    }
  }
  return sum / ((last - first) * (last - first));
}

bool writeTiltMap(std::string const& path) {
  TIFF* tif = TIFFOpen(path.c_str(), "w");
  if (tif == nullptr) {
    return false;
  }
  TIFFSetField(tif, TIFFTAG_IMAGEWIDTH, (uint32_t)1);
  TIFFSetField(tif, TIFFTAG_IMAGELENGTH, (uint32_t)1);
  TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, 8);
  TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, 3);
  TIFFSetField(tif, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT);
  TIFFSetField(tif, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB);
  TIFFSetField(tif, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
  TIFFSetField(tif, TIFFTAG_ROWSPERSTRIP, 1);
  unsigned char row[3] = {kTexelRed, kTexelGreen, kTexelBlue};
  bool const ok = TIFFWriteScanline(tif, row, 0, 0) >= 0;
  TIFFClose(tif);
  return ok;
}

bool usable(GmanImage const& image, std::size_t size) {
  return image.ok && image.width == size && image.height == size;
}

GmanImage render(std::string const& gman, std::string const& renderer, std::string const& ribDir,
                 std::string const& name, std::size_t size) {
  std::remove((name + ".tif").c_str());
  int const status = runGman(gman, {"-r", renderer, ribDir + "/" + name + ".rib"}).exitStatus;
  check(status == 0, name + ": renders");
  GmanImage image = readGmanTIFF(name + ".tif");
  check(usable(image, size), name + ": TIFF read back at " + std::to_string(size) + " x " + std::to_string(size));
  return image;
}

void checkBounceCardBlack(GmanImage const& bounce) {
  double const mean = interiorMean(bounce);
  check(mean < kBounceBlack,
        "bounce card: its interior stays black, mean " + std::to_string(mean * 255.0) + " of 255 against 1");
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

  check(writeTiltMap("bumpterminator_tilt.tif"), "the tilt map writes");

  GmanImage const mapped = render(gman, renderer, ribDir, "bumpterminator", kImageSize);
  GmanImage const unmapped = render(gman, renderer, ribDir, "bumpterminator_nomap", kImageSize);
  if (usable(mapped, kImageSize) && usable(unmapped, kImageSize)) {
    double const litFace = measuredMean(mapped, kLitFaceBand);
    double const litFaceUnmapped = measuredMean(unmapped, kLitFaceBand);
    check(litFace > 0.0, "mapped: the face turned to the light is lit, mean " + std::to_string(litFace));
    check(litFaceUnmapped > 0.0,
          "unmapped: the face turned to the light is lit, mean " + std::to_string(litFaceUnmapped));
    if (litFace > 0.0 && litFaceUnmapped > 0.0) {
      double const beyond = measuredMean(mapped, kBeyondTerminatorBand);
      check(beyond <= kBeyondTerminatorFraction * litFace,
            "beyond the terminator: the band stays dark, mean " + std::to_string(beyond) + " against the lit face " +
                std::to_string(litFace) + ", ratio " + std::to_string(beyond / litFace));

      double const mappedScale = litFace / predictedMean(kLitFaceBand, predictedMapped);
      double const unmappedScale = litFaceUnmapped / predictedMean(kLitFaceBand, predictedUnmapped);
      double const ratio = mappedScale / unmappedScale;
      check(std::fabs(ratio - 1.0) <= kLitFaceTolerance, "lit face: the term leaves the lit face its light, ratio " +
                                                             std::to_string(ratio) + " against 1 within " +
                                                             std::to_string(kLitFaceTolerance));
    }
  }

  if (renderer == kPathTracer) {
    GmanImage const bounce = render(gman, renderer, ribDir, "bumpterminator_bounce", kBounceImageSize);
    if (usable(bounce, kBounceImageSize)) {
      checkBounceCardBlack(bounce);
    }
  }

  return checkSummary(kSummary);
}
