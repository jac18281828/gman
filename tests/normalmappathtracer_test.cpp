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
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>

#include <tiffio.h>

#include "check.h"
#include "goldenimage.h"
#include "rungman.h"

namespace {

constexpr unsigned char kTexelRed = 191;
constexpr unsigned char kTexelGreen = 128;
constexpr unsigned char kTexelBlue = 238;

constexpr uint32_t kInteriorHalfWidth = 9;

constexpr double kHalfSqrt = 0.70710678118654752440;
constexpr double kDirectTolerance = 0.01;

constexpr double kWallZ = -4.0;
constexpr double kWallXMin = 2.0;
constexpr double kWallXMax = 10.0;
constexpr double kWallHalfHeight = 10.0;
constexpr int kWallGrid = 400;
constexpr double kBounceTolerance = 0.12;

struct Direction {
  double x;
  double y;
  double z;
};

double dot(Direction const& a, Direction const& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

double decode(unsigned char channel) { return 2.0 * (channel / 255.0) - 1.0; }

Direction bentNormal() {
  Direction const n{decode(kTexelRed), -decode(kTexelGreen), -decode(kTexelBlue)};
  double const length = std::sqrt(dot(n, n));
  return {n.x / length, n.y / length, n.z / length};
}

Direction const kCardNormal{0.0, 0.0, -1.0};

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

std::optional<double> cardMean(std::string const& gman, std::string const& ribDir, std::string const& name) {
  std::remove((name + ".tif").c_str());
  int const status = runGman(gman, {"-r", "gmanpathtracer", ribDir + "/" + name + ".rib"}).exitStatus;
  check(status == 0, name + ": renders under -r gmanpathtracer");
  GmanImage const image = readGmanTIFF(name + ".tif");
  check(image.ok, name + ": TIFF read back");
  if (status != 0 || !image.ok) {
    return std::nullopt;
  }

  uint32_t const first = image.width / 2 - kInteriorHalfWidth;
  uint32_t const last = image.width / 2 + kInteriorHalfWidth;
  double sum = 0.0;
  for (uint32_t y = first; y < last; ++y) {
    for (uint32_t x = first; x < last; ++x) {
      sum += TIFFGetR(image.at(x, y));
    }
  }
  return sum / ((last - first) * (last - first));
}

double wallCosineIntegral(Direction const& n) {
  double const cellWidth = (kWallXMax - kWallXMin) / kWallGrid;
  double const cellHeight = 2.0 * kWallHalfHeight / kWallGrid;
  double sum = 0.0;
  for (int i = 0; i < kWallGrid; ++i) {
    for (int j = 0; j < kWallGrid; ++j) {
      Direction const point{kWallXMin + (i + 0.5) * cellWidth, -kWallHalfHeight + (j + 0.5) * cellHeight, kWallZ};
      double const distance = std::sqrt(dot(point, point));
      Direction const toPoint{point.x / distance, point.y / distance, point.z / distance};
      double const solidAngle = std::fabs(kWallZ) / distance * cellWidth * cellHeight / (distance * distance);
      sum += std::fmax(0.0, dot(n, toPoint)) * solidAngle;
    }
  }
  return sum;
}

void checkRatio(std::optional<double> mapped, std::optional<double> unmapped, double predicted, double tolerance,
                std::string const& what) {
  if (!mapped || !unmapped) {
    return;
  }
  check(*unmapped > 0.0, what + ": the unmapped card is lit, mean " + std::to_string(*unmapped));
  if (!(*unmapped > 0.0)) {
    return;
  }
  double const ratio = *mapped / *unmapped;
  check(std::fabs(ratio - predicted) <= tolerance, what + ": mapped over unmapped mean " + std::to_string(ratio) +
                                                       " vs predicted " + std::to_string(predicted) + " within " +
                                                       std::to_string(tolerance));
}

} // namespace

int main(int argc, char* argv[]) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s <gman-binary> <rib-dir>\n", argv[0]);
    return 2;
  }
  std::string const gman = argv[1];
  std::string const ribDir = argv[2];

  check(writeTiltMap("normalmap_pt_tilt.tif"), "the tilt map writes");

  Direction const bent = bentNormal();

  Direction const lightDirection{kHalfSqrt, 0.0, -kHalfSqrt};
  double const directPredicted = dot(bent, lightDirection) / dot(kCardNormal, lightDirection);
  checkRatio(cardMean(gman, ribDir, "normalmap_card"), cardMean(gman, ribDir, "normalmap_card_nomap"), directPredicted,
             kDirectTolerance, "light sample");

  double const bouncePredicted = wallCosineIntegral(bent) / wallCosineIntegral(kCardNormal);
  checkRatio(cardMean(gman, ribDir, "normalmap_bounce"), cardMean(gman, ribDir, "normalmap_bounce_nomap"),
             bouncePredicted, kBounceTolerance, "BSDF bounce");

  return checkSummary("gman -r gmanpathtracer weights light and BSDF samples by the bent normal");
}
