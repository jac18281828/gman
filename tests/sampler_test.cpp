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
 * gman::sampleHash, gman::unitFloat, gman::sample1D and gman::sample2D:
 * a counter-based sampler that is a pure function of its arguments, so
 * reproduction depends only on the arguments, never on call order.
 */

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "check.h"
#include "gmanparallel.h"
#include "gmansampling.h"
#include "samplingstats.h"

namespace {

constexpr std::uint32_t kSeed = 12345u;

struct Pixel {
  RtInt x, y;
};

// floor(value * count) covers {0, ..., count-1} exactly once: the
// defining property of a stratified pattern.
bool strataFilledOnce(std::vector<RtFloat> const& values, std::uint32_t count) {
  std::vector<bool> seen(count, false);
  for (RtFloat v : values) {
    if (v < 0.0f || v >= 1.0f) {
      return false;
    }
    auto const stratum = static_cast<std::uint32_t>(v * static_cast<RtFloat>(count));
    if (stratum >= count || seen[stratum]) {
      return false;
    }
    seen[stratum] = true;
  }
  for (bool s : seen) {
    if (!s) {
      return false;
    }
  }
  return true;
}

// unitFloat pins its five named values exactly.
void checkUnitFloatExact() {
  check(gman::unitFloat(0u) == 0.0f, "unitFloat(0) == 0");
  check(gman::unitFloat(0xFFu) == 0.0f, "unitFloat(0xFF) == 0");
  check(gman::unitFloat(0x100u) == 0x1p-24f, "unitFloat(0x100) == 2^-24");
  check(gman::unitFloat(0xFFFFFFFFu) == 1.0f - 0x1p-24f, "unitFloat(0xFFFFFFFF) == 1 - 2^-24");
  check(gman::unitFloat(0x80000000u) == 0.5f, "unitFloat(0x80000000) == 0.5");
}

// sampleHash reproduces across repeated calls; four re-pinned tuples
// match values from this implementation, none zero, including the
// all-zero tuple: the chain's nonzero initial state keeps it off 0.
void checkSampleHashReproducesAndPins() {
  std::uint32_t const a = gman::sampleHash(7u, 3, 4, 5u, 6u);
  std::uint32_t const b = gman::sampleHash(7u, 3, 4, 5u, 6u);
  check(a == b, "sampleHash repeats its output for the same arguments");

  check(gman::sampleHash(1u, 2, 3, 4u, 5u) == 0xacdc0dc0u, "sampleHash(1, 2, 3, 4, 5) is pinned and nonzero");
  check(gman::sampleHash(42u, 100, 200, 7u, 1u) == 0x0fb7defau, "sampleHash(42, 100, 200, 7, 1) is pinned and nonzero");
  check(gman::sampleHash(0u, 0, 0, 0u, 0u) == 0x0eaa7511u, "sampleHash(0, 0, 0, 0, 0) is pinned and nonzero");
  check(gman::sampleHash(0xdeadbeefu, -5, 17, 999u, 3u) == 0x5744b158u,
        "sampleHash(0xdeadbeef, -5, 17, 999, 3) is pinned and nonzero");
}

// unitFloat(sampleHash(...)) is uniform on [0, 1): in range, mean and
// variance within 5sigma of 1/2 and 1/12, and a 16-bin histogram within
// 5sigma.
void checkUnitFloatUniformity() {
  constexpr std::uint32_t kCount = 1u << 16;
  std::vector<double> draws(kCount);
  std::vector<int> histogram(16, 0);
  bool inRange = true;

  for (std::uint32_t i = 0; i < kCount; ++i) {
    RtFloat const u = gman::unitFloat(gman::sampleHash(kSeed, 11, 22, i, 3u));
    inRange = inRange && u >= 0.0f && u < 1.0f;
    draws[i] = static_cast<double>(u);
    histogram[static_cast<std::size_t>(u * 16.0f)]++;
  }
  check(inRange, "unitFloat(sampleHash(...)) over 2^16 draws stays in [0, 1)");

  GmanMeanStderr const mean = meanStderr(draws);
  checkNear(mean.mean, 0.5, mean.stderrOfMean, 1e-4, "unitFloat draws: mean within 5sigma of 1/2");

  std::vector<double> sqDev(kCount);
  for (std::uint32_t i = 0; i < kCount; ++i) {
    sqDev[i] = (draws[i] - 0.5) * (draws[i] - 0.5);
  }
  GmanMeanStderr const variance = meanStderr(sqDev);
  checkNear(variance.mean, 1.0 / 12.0, variance.stderrOfMean, 1e-4, "unitFloat draws: variance within 5sigma of 1/12");

  bool histOk = true;
  double const p = 1.0 / 16.0;
  double const sigma = std::sqrt(p * (1.0 - p) / static_cast<double>(kCount));
  for (int bin = 0; bin < 16; ++bin) {
    double const fraction = static_cast<double>(histogram[static_cast<std::size_t>(bin)]) / static_cast<double>(kCount);
    histOk = histOk && std::fabs(fraction - p) <= 5.0 * sigma;
  }
  check(histOk, "unitFloat draws: 16-bin histogram within 5sigma of uniform");
}

// sample1D fills every stratum once at N=16 and N=13, three pixels, two
// dimensions.
void checkSample1DFillsStrata() {
  std::vector<Pixel> const pixels = {{0, 0}, {17, 5}, {640, 480}};
  std::vector<std::uint32_t> const dims = {0u, 9u};
  std::vector<std::uint32_t> const counts = {16u, 13u};

  bool strataOk = true;
  for (Pixel const& px : pixels) {
    for (std::uint32_t dim : dims) {
      for (std::uint32_t n : counts) {
        std::vector<RtFloat> values(n);
        for (std::uint32_t s = 0; s < n; ++s) {
          values[s] = gman::sample1D(kSeed, px.x, px.y, s, n, dim);
        }
        strataOk = strataOk && strataFilledOnce(values, n);
      }
    }
  }
  check(strataOk, "sample1D fills every stratum once at N=16 and N=13, three pixels, two dimensions");
}

// Independence: five sampleHash pairings correlate within 5sigma of 0
// over 2^16 pixels at one sample index.
void checkSampleHashPairIndependence() {
  constexpr std::uint32_t kCount = 1u << 16;

  auto checkPairCorrelation = [](std::vector<double> const& a, std::vector<double> const& b, std::string const& what) {
    GmanMeanStderr const stat = correlationStderr(a, b);
    checkNear(stat.mean, 0.0, stat.stderrOfMean, 1e-3, what);
  };

  std::vector<double> hashDimD(kCount), hashDimD1(kCount);
  std::vector<double> hashPixel(kCount), hashPixelXp1(kCount);
  std::vector<double> hashPixelXY(kCount), hashPixelYX(kCount);
  std::vector<double> hashXDim(kCount), hashDimXSwapped(kCount);
  std::vector<double> hashSeedS(kCount), hashSeedS1(kCount);

  for (std::uint32_t i = 0; i < kCount; ++i) {
    auto const x = static_cast<RtInt>(i);
    hashDimD[i] = gman::unitFloat(gman::sampleHash(kSeed, x, 5, 0u, 3u));
    hashDimD1[i] = gman::unitFloat(gman::sampleHash(kSeed, x, 5, 0u, 4u));
    hashPixel[i] = gman::unitFloat(gman::sampleHash(kSeed, x, 5, 0u, 3u));
    hashPixelXp1[i] = gman::unitFloat(gman::sampleHash(kSeed, x + 1, 5, 0u, 3u));
    auto const yScrambled = static_cast<RtInt>((i * 2654435761u) % 100000u);
    hashPixelXY[i] = gman::unitFloat(gman::sampleHash(kSeed, x, yScrambled, 0u, 3u));
    hashPixelYX[i] = gman::unitFloat(gman::sampleHash(kSeed, yScrambled, x, 0u, 3u));
    hashXDim[i] = gman::unitFloat(gman::sampleHash(kSeed, x, 5, 0u, 7u));
    hashDimXSwapped[i] = gman::unitFloat(gman::sampleHash(kSeed, 7, 5, 0u, static_cast<std::uint32_t>(x)));
    hashSeedS[i] = gman::unitFloat(gman::sampleHash(kSeed, x, 5, 0u, 3u));
    hashSeedS1[i] = gman::unitFloat(gman::sampleHash(kSeed + 1, x, 5, 0u, 3u));
  }

  checkPairCorrelation(hashDimD, hashDimD1, "sampleHash: dimension d vs d+1 are independent");
  checkPairCorrelation(hashPixel, hashPixelXp1, "sampleHash: pixel (x,y) vs (x+1,y) are independent");
  checkPairCorrelation(hashPixelXY, hashPixelYX, "sampleHash: pixel (x,y) vs (y,x) are independent");
  checkPairCorrelation(hashXDim, hashDimXSwapped, "sampleHash: x vs the dimension swapped are independent");
  checkPairCorrelation(hashSeedS, hashSeedS1, "sampleHash: seed s vs s+1 are independent");
}

// Independence: sample1D against sample2D's u1 at one dimension.
void checkSample1DSample2DIndependence() {
  constexpr std::uint32_t kCount = 1u << 16;
  std::vector<double> sample1DValues(kCount), sample2DU1Values(kCount);
  for (std::uint32_t i = 0; i < kCount; ++i) {
    auto const x = static_cast<RtInt>(i);
    sample1DValues[i] = gman::sample1D(kSeed, x, 5, 0u, 16u, 3u);
    sample2DU1Values[i] = gman::sample2D(kSeed, x, 5, 0u, 16u, 3u).u1;
  }
  GmanMeanStderr const stat = correlationStderr(sample1DValues, sample2DU1Values);
  checkNear(stat.mean, 0.0, stat.stderrOfMean, 1e-3, "sample1D vs sample2D's u1 are independent");
}

// sample2D fills every m*n cell and every N-stratum of each axis once,
// for one (pixel, dimension) pair.
bool sample2DFillsGridOnce(std::uint32_t sampleCount, std::uint32_t mCols, std::uint32_t nRows, RtInt x, RtInt y,
                           std::uint32_t dim) {
  std::vector<bool> cellSeen(static_cast<std::size_t>(mCols) * nRows, false);
  std::vector<bool> u1Seen(sampleCount, false);
  std::vector<bool> u2Seen(sampleCount, false);
  for (std::uint32_t s = 0; s < sampleCount; ++s) {
    gman::Sample2D const p = gman::sample2D(kSeed, x, y, s, sampleCount, dim);
    if (p.u1 < 0.0f || p.u1 >= 1.0f || p.u2 < 0.0f || p.u2 >= 1.0f) {
      return false;
    }
    auto const cx = static_cast<std::uint32_t>(p.u1 * static_cast<RtFloat>(mCols));
    auto const cy = static_cast<std::uint32_t>(p.u2 * static_cast<RtFloat>(nRows));
    std::size_t const cell = static_cast<std::size_t>(cx) * nRows + cy;
    if (cx >= mCols || cy >= nRows || cellSeen[cell]) {
      return false;
    }
    cellSeen[cell] = true;

    auto const u1Stratum = static_cast<std::uint32_t>(p.u1 * static_cast<RtFloat>(sampleCount));
    auto const u2Stratum = static_cast<std::uint32_t>(p.u2 * static_cast<RtFloat>(sampleCount));
    if (u1Stratum >= sampleCount || u1Seen[u1Stratum] || u2Stratum >= sampleCount || u2Seen[u2Stratum]) {
      return false;
    }
    u1Seen[u1Stratum] = true;
    u2Seen[u2Stratum] = true;
  }
  for (bool seen : u1Seen) {
    if (!seen) {
      return false;
    }
  }
  for (bool seen : u2Seen) {
    if (!seen) {
      return false;
    }
  }
  return true;
}

void checkSample2DFillsGrid() {
  std::vector<Pixel> const pixels = {{0, 0}, {17, 5}, {640, 480}};
  std::vector<std::uint32_t> const dims = {0u, 9u};

  bool ok16 = true;
  bool ok12 = true;
  for (Pixel const& px : pixels) {
    for (std::uint32_t dim : dims) {
      ok16 = ok16 && sample2DFillsGridOnce(16u, 4u, 4u, px.x, px.y, dim);
      ok12 = ok12 && sample2DFillsGridOnce(12u, 3u, 4u, px.x, px.y, dim);
    }
  }
  check(ok16, "sample2D fills every 4x4 cell and every 16-stratum of each axis at N=16");
  check(ok12, "sample2D fills every 3x4 cell and every 12-stratum of each axis at N=12");
}

// sample2D at N=7 stays in [0, 1)^2 over 2^16 pixels.
void checkSample2DBoundsAtN7() {
  constexpr std::uint32_t kCount = 1u << 16;
  bool boundsOk = true;
  for (std::uint32_t i = 0; i < kCount; ++i) {
    auto const x = static_cast<RtInt>(i);
    for (std::uint32_t s = 0; s < 7u; ++s) {
      gman::Sample2D const p = gman::sample2D(kSeed, x, 0, s, 7u, 0u);
      boundsOk = boundsOk && p.u1 >= 0.0f && p.u1 < 1.0f && p.u2 >= 0.0f && p.u2 < 1.0f;
    }
  }
  check(boundsOk, "sample2D at N=7 stays in [0, 1)^2 over 2^16 pixels");
}

// Correlation: grouping one pixel's sample2D points by row, every point
// shares its row's fine-x index; grouped by column, every point shares
// its column's fine-y index. Kensler's cmj: one row shuffle sets every
// column's sub-position in a row, one column shuffle sets every row's.
bool sample2DRowColumnCorrelated(std::uint32_t sampleCount, std::uint32_t mCols, std::uint32_t nRows) {
  std::vector<int> fineXByRow(nRows, -1);
  std::vector<int> fineYByColumn(mCols, -1);
  for (std::uint32_t s = 0; s < sampleCount; ++s) {
    gman::Sample2D const p = gman::sample2D(kSeed, 3, 4, s, sampleCount, 1u);
    auto const row = std::min(static_cast<std::uint32_t>(p.u2 * static_cast<RtFloat>(nRows)), nRows - 1);
    auto const column = std::min(static_cast<std::uint32_t>(p.u1 * static_cast<RtFloat>(mCols)), mCols - 1);
    auto const fineX = static_cast<int>(p.u1 * static_cast<RtFloat>(mCols * nRows)) % static_cast<int>(nRows);
    auto const fineY = static_cast<int>(p.u2 * static_cast<RtFloat>(mCols * nRows)) % static_cast<int>(mCols);
    if (fineXByRow[row] == -1) {
      fineXByRow[row] = fineX;
    } else if (fineXByRow[row] != fineX) {
      return false;
    }
    if (fineYByColumn[column] == -1) {
      fineYByColumn[column] = fineY;
    } else if (fineYByColumn[column] != fineY) {
      return false;
    }
  }
  return true;
}

void checkSample2DRowColumnCorrelation() {
  check(sample2DRowColumnCorrelated(16u, 4u, 4u), "sample2D at N=16: fine-x by row, fine-y by column are constant");
  check(sample2DRowColumnCorrelated(12u, 3u, 4u), "sample2D at N=12: fine-x by row, fine-y by column are constant");
}

// Pattern decorrelation: the (cell, u1-stratum) sequence at N=16 differs
// between two pixels and between two dimensions at one pixel; so does
// sample1D's stratum-index sequence.
void checkPatternDecorrelation() {
  constexpr std::uint32_t kN = 16u;

  auto cellStratumSequence = [](RtInt x, RtInt y, std::uint32_t dim) {
    std::vector<std::pair<int, int>> seq;
    seq.reserve(kN);
    for (std::uint32_t s = 0; s < kN; ++s) {
      gman::Sample2D const p = gman::sample2D(kSeed, x, y, s, kN, dim);
      int const cell = static_cast<int>(p.u1 * 4.0f) * 4 + static_cast<int>(p.u2 * 4.0f);
      int const u1Stratum = static_cast<int>(p.u1 * static_cast<RtFloat>(kN));
      seq.emplace_back(cell, u1Stratum);
    }
    return seq;
  };

  auto sample1DSequence = [](RtInt x, RtInt y, std::uint32_t dim) {
    std::vector<int> seq;
    seq.reserve(kN);
    for (std::uint32_t s = 0; s < kN; ++s) {
      RtFloat const v = gman::sample1D(kSeed, x, y, s, kN, dim);
      seq.push_back(static_cast<int>(v * static_cast<RtFloat>(kN)));
    }
    return seq;
  };

  auto const seqPixelA = cellStratumSequence(0, 0, 3u);
  auto const seqPixelB = cellStratumSequence(11, 23, 3u);
  auto const seqDimB = cellStratumSequence(0, 0, 8u);
  check(seqPixelA != seqPixelB, "sample2D: (cell, u1-stratum) sequence differs between two pixels");
  check(seqPixelA != seqDimB, "sample2D: (cell, u1-stratum) sequence differs between two dimensions");

  auto const s1dPixelA = sample1DSequence(0, 0, 3u);
  auto const s1dPixelB = sample1DSequence(11, 23, 3u);
  auto const s1dDimB = sample1DSequence(0, 0, 8u);
  check(s1dPixelA != s1dPixelB, "sample1D: stratum-index sequence differs between two pixels");
  check(s1dPixelA != s1dDimB, "sample1D: stratum-index sequence differs between two dimensions");
}

// The canonical (x, y, sampleIndex) a flat buffer index unpacks to under
// row-major and 8x8-tile order, shared by checkScheduleIndependence.
std::tuple<RtInt, RtInt, std::uint32_t> rowOrder(RtInt index, RtInt side, std::uint32_t samples) {
  RtInt const y = index / (side * static_cast<RtInt>(samples));
  RtInt const rem = index % (side * static_cast<RtInt>(samples));
  RtInt const x = rem / static_cast<RtInt>(samples);
  return {x, y, static_cast<std::uint32_t>(rem % static_cast<RtInt>(samples))};
}

std::tuple<RtInt, RtInt, std::uint32_t> tileOrder(RtInt index, RtInt side, std::uint32_t samples) {
  constexpr RtInt kTileSide = 8;
  RtInt const tilesPerSide = side / kTileSide;
  RtInt const pixelsPerTile = kTileSide * kTileSide;

  RtInt const pixelIndex = index / static_cast<RtInt>(samples);
  auto const s = static_cast<std::uint32_t>(index % static_cast<RtInt>(samples));
  RtInt const tileIndex = pixelIndex / pixelsPerTile;
  RtInt const withinTile = pixelIndex % pixelsPerTile;
  RtInt const y = (tileIndex / tilesPerSide) * kTileSide + withinTile / kTileSide;
  RtInt const x = (tileIndex % tilesPerSide) * kTileSide + withinTile % kTileSide;
  return {x, y, s};
}

// Schedule independence: a serial fill and parallelFor fills (1, 3, 8
// workers, row and 8x8-tile order) land the same buffer, bit for bit.
void checkScheduleIndependence() {
  constexpr RtInt kSide = 64;
  constexpr std::uint32_t kSamples = 16u;
  constexpr std::uint32_t kDim = 2u;
  constexpr RtInt kTotal = kSide * kSide * static_cast<RtInt>(kSamples);

  auto canonicalIndex = [](RtInt x, RtInt y, std::uint32_t s) {
    return (y * kSide + x) * static_cast<RtInt>(kSamples) + static_cast<RtInt>(s);
  };

  std::vector<gman::Sample2D> serial(static_cast<std::size_t>(kTotal));
  for (RtInt y = 0; y < kSide; ++y) {
    for (RtInt x = 0; x < kSide; ++x) {
      for (std::uint32_t s = 0; s < kSamples; ++s) {
        serial[static_cast<std::size_t>(canonicalIndex(x, y, s))] = gman::sample2D(kSeed, x, y, s, kSamples, kDim);
      }
    }
  }

  bool allMatch = true;
  auto runAndCompare = [&](auto const& order, RtInt workers) {
    std::vector<gman::Sample2D> buffer(static_cast<std::size_t>(kTotal));
    gman::parallelFor(
        kTotal,
        [&](RtInt index, RtInt) {
          auto const [x, y, s] = order(index, kSide, kSamples);
          buffer[static_cast<std::size_t>(canonicalIndex(x, y, s))] = gman::sample2D(kSeed, x, y, s, kSamples, kDim);
        },
        workers);
    for (std::size_t i = 0; i < buffer.size(); ++i) {
      allMatch = allMatch && buffer[i].u1 == serial[i].u1 && buffer[i].u2 == serial[i].u2;
    }
  };

  for (RtInt workers : {1, 3, 8}) {
    runAndCompare(rowOrder, workers);
    runAndCompare(tileOrder, workers);
  }
  check(allMatch, "sample2D: serial and parallelFor fills match bit for bit at 1, 3 and 8 workers, row and tile order");
}

// Over pixels and (dimension, sample2D-column) pairs, counts how many
// pixels a reference pixel's mapping predicts exactly: a broken hash
// ties dimensions together via one shared function; a well-mixed one
// gives each pixel its own, so almost none match.
template <class PairAt> double globalFunctionMatchFraction(PairAt pairAt, int pixels, std::uint32_t domain) {
  std::vector<int> reference(domain, -1);
  for (std::uint32_t s = 0; s < domain; ++s) {
    auto const [a, b] = pairAt(0, s);
    reference[static_cast<std::size_t>(a)] = b;
  }
  int matches = 0;
  for (int px = 1; px < pixels; ++px) {
    bool same = true;
    for (std::uint32_t s = 0; s < domain && same; ++s) {
      auto const [a, b] = pairAt(px, s);
      same = reference[static_cast<std::size_t>(a)] == b;
    }
    matches += same ? 1 : 0;
  }
  return static_cast<double>(matches) / static_cast<double>(pixels - 1);
}

// Within-pixel independence: no pixel-independent function predicts one
// dimension's stratum sequence from another's, or sample2D's column from
// sample1D's stratum, for more than 1% of pixels.
void checkWithinPixelIndependence() {
  constexpr std::uint32_t kN = 16u;
  constexpr int kPixels = 1 << 14;

  double const sample1DFraction = globalFunctionMatchFraction(
      [](RtInt px, std::uint32_t s) {
        return std::pair(static_cast<int>(gman::sample1D(kSeed, px, 0, s, kN, 2u) * kN),
                         static_cast<int>(gman::sample1D(kSeed, px, 0, s, kN, 3u) * kN));
      },
      kPixels, kN);
  checkNear(sample1DFraction, 0.0, 0.0, 0.01, "sample1D: dimension d+1's stratum order is not fixed by d's");

  double const sample2DFraction = globalFunctionMatchFraction(
      [](RtInt px, std::uint32_t s) {
        return std::pair(static_cast<int>(gman::sample2D(kSeed, px, 0, s, kN, 2u).u1 * 4.0f),
                         static_cast<int>(gman::sample2D(kSeed, px, 0, s, kN, 3u).u1 * 4.0f));
      },
      kPixels, 4u);
  checkNear(sample2DFraction, 0.0, 0.0, 0.01, "sample2D: dimension d+1's u1 column is not fixed by d's");

  double const crossFraction = globalFunctionMatchFraction(
      [](RtInt px, std::uint32_t s) {
        return std::pair(static_cast<int>(gman::sample1D(kSeed, px, 0, s, kN, 2u) * kN),
                         static_cast<int>(gman::sample2D(kSeed, px, 0, s, kN, 2u).u1 * 4.0f));
      },
      kPixels, kN);
  checkNear(crossFraction, 0.0, 0.0, 0.01, "sample2D(d)'s u1 column is not fixed by sample1D(d)'s stratum");
}

// Permutation reach: Kensler's permute() for a 16-element stratum order
// draws from at most 2^14 distinct permutations, whatever seed drives
// it. 2^14 pixel-keyed draws against that many outcomes is a birthday
// draw, expected near 1 - 1/e (about 63%) distinct; the check floors
// well under that expectation.
void checkPermutationReach() {
  constexpr std::uint32_t kN = 16u;
  constexpr int kPixels = 1 << 14;
  std::set<std::array<int, kN>> orders;
  for (int px = 0; px < kPixels; ++px) {
    std::array<int, kN> order{};
    for (std::uint32_t s = 0; s < kN; ++s) {
      order[s] = static_cast<int>(gman::sample1D(kSeed, px, 0, s, kN, 5u) * kN);
    }
    orders.insert(order);
  }
  double const fraction = static_cast<double>(orders.size()) / kPixels;
  check(fraction >= 0.5, "sample1D: stratum orders are at least 50% distinct over 2^14 pixels at N=16");
}

// A sampleCount of 0 behaves as sampleCount 1 at sampleIndex 0.
void checkZeroSampleCount() {
  check(gman::sample1D(1u, 0, 0, 0u, 0u, 0u) == gman::sample1D(1u, 0, 0, 0u, 1u, 0u),
        "sample1D at sampleCount=0 equals sampleCount=1, sampleIndex=0");
  gman::Sample2D const a = gman::sample2D(1u, 0, 0, 0u, 0u, 0u);
  gman::Sample2D const b = gman::sample2D(1u, 0, 0, 0u, 1u, 0u);
  check(a.u1 == b.u1 && a.u2 == b.u2, "sample2D at sampleCount=0 equals sampleCount=1, sampleIndex=0");
}

// sample2D at very large sampleCount returns promptly and in [0, 1)^2:
// isqrtFloor and its grid math compute in 64 bits, so neither overflows.
void checkLargeSampleCounts() {
  for (std::uint32_t count : {4294836225u, 0xFFFFFFFFu}) {
    gman::Sample2D const p = gman::sample2D(1u, 0, 0, 0u, count, 0u);
    bool const inBounds = p.u1 >= 0.0f && p.u1 < 1.0f && p.u2 >= 0.0f && p.u2 < 1.0f;
    check(inBounds, "sample2D at sampleCount=" + std::to_string(count) + " lies in [0, 1)^2");
  }
}

// clampBelowOne's window grows with sampleCount: at 4294836225,
// ulp(count)/2 covers the jitter's rounding window, and these inputs
// land exactly on the unclamped value 1.0f. Both stay below 1 with the
// clamp.
void checkClampBelowOne() {
  RtFloat const s1 = gman::sample1D(12345u, 191, 0, 50543188u, 4294836225u, 0u);
  check(s1 < 1.0f, "sample1D(12345, 191, 0, 50543188, 4294836225, 0) stays below 1");
  gman::Sample2D const s2 = gman::sample2D(12345u, 13, 0, 41770753u, 4294836225u, 0u);
  check(s2.u1 < 1.0f, "sample2D(12345, 13, 0, 41770753, 4294836225, 0).u1 stays below 1");
}

// sample2D's grid shape, recomputed here the way the implementation
// derives it, so a per-pattern check can recover which cell a draw
// landed in: m columns, n rows, one row short by m - r columns when
// r != m.
struct Sample2DGrid {
  std::uint32_t m, n, r;
};

Sample2DGrid sample2DGridShape(std::uint32_t count) {
  std::uint32_t m = 1;
  while ((m + 1) * (m + 1) <= count) {
    ++m;
  }
  std::uint32_t const n = (count + m - 1) / m;
  std::uint32_t const r = count - m * (n - 1);
  return {m, n, r};
}

// FNV-1a over sample2D's own (u1, u2) bit patterns at one fixed pattern
// set: three seeds, an 8x8 pixel block, one dimension.
std::uint64_t sample2DDigest(std::uint32_t sampleCount) {
  constexpr std::uint64_t kFnvOffset = 1469598103934665603ull;
  constexpr std::uint64_t kFnvPrime = 1099511628211ull;
  static_assert(sizeof(RtFloat) == sizeof(std::uint32_t));

  std::uint64_t digest = kFnvOffset;
  for (std::uint32_t seed : {0u, 1u, 7u}) {
    for (RtInt y = 0; y < 8; ++y) {
      for (RtInt x = 0; x < 8; ++x) {
        for (std::uint32_t s = 0; s < sampleCount; ++s) {
          gman::Sample2D const draw = gman::sample2D(seed, x, y, s, sampleCount, 3u);
          std::uint32_t bits[2];
          std::memcpy(&bits[0], &draw.u1, sizeof(std::uint32_t));
          std::memcpy(&bits[1], &draw.u2, sizeof(std::uint32_t));
          auto const* p = reinterpret_cast<unsigned char const*>(bits);
          for (std::size_t k = 0; k < sizeof(bits); ++k) {
            digest ^= p[k];
            digest *= kFnvPrime;
          }
        }
      }
    }
  }
  return digest;
}

// A full grid's draws stay bit-identical to before the partial-grid
// unbiasing: the digest recomputed here matches the one recorded against
// the unfixed sampler at every full-grid N this file also exercises.
void checkSample2DFullGridDigestUnchanged() {
  check(sample2DDigest(2u) == 0xba6af1756462eca3ull, "sample2D digest at N=2 (full grid) is unchanged");
  check(sample2DDigest(3u) == 0x9b5e109097ba73a2ull, "sample2D digest at N=3 (full grid) is unchanged");
  check(sample2DDigest(12u) == 0x14e14c4ef1dc9839ull, "sample2D digest at N=12 (full grid) is unchanged");
  check(sample2DDigest(16u) == 0x9343324c925d6067ull, "sample2D digest at N=16 (full grid) is unchanged");
  check(sample2DDigest(64u) == 0xc8797082dcdcc6a3ull, "sample2D digest at N=64 (full grid) is unchanged");
  check(sample2DDigest(256u) == 0x976fce4eb95f0768ull, "sample2D digest at N=256 (full grid) is unchanged");
}

// A mean and its standard error accumulated one value at a time
// (Welford's update), so a check over millions of draws stores none.
struct RunningMeanStderr {
  std::uint64_t count = 0;
  double mean = 0.0;
  double sumSquaredDeviations = 0.0;

  void add(double value) {
    ++count;
    double const delta = value - mean;
    mean += delta / static_cast<double>(count);
    sumSquaredDeviations += delta * (value - mean);
  }

  GmanMeanStderr result() const {
    double const variance = sumSquaredDeviations / static_cast<double>(count - 1);
    return {mean, std::sqrt(variance / static_cast<double>(count))};
  }
};

// A partial grid's u1 and u2 stay unbiased across patterns: at
// N = 2, 3, 5, 32, 128 and 512, over at least 2^20 draws spread across
// distinct pixels, the mean of each and a 4x4 joint histogram match a
// uniform draw within 5 sigma.
void checkSample2DPartialGridUnbiased() {
  constexpr std::uint32_t kMinDraws = 1u << 20;
  constexpr std::uint32_t kWidth = 4096u;
  for (std::uint32_t n : {2u, 3u, 5u, 32u, 128u, 512u}) {
    std::uint32_t const patterns = (kMinDraws + n - 1) / n;
    RunningMeanStderr u1Stat;
    RunningMeanStderr u2Stat;
    int hist[4][4] = {};

    for (std::uint32_t p = 0; p < patterns; ++p) {
      auto const x = static_cast<RtInt>(p % kWidth);
      auto const y = static_cast<RtInt>(p / kWidth);
      for (std::uint32_t s = 0; s < n; ++s) {
        gman::Sample2D const draw = gman::sample2D(kSeed, x, y, s, n, 40u);
        u1Stat.add(static_cast<double>(draw.u1));
        u2Stat.add(static_cast<double>(draw.u2));
        int const hb1 = std::min(3, static_cast<int>(draw.u1 * 4.0f));
        int const hb2 = std::min(3, static_cast<int>(draw.u2 * 4.0f));
        hist[hb1][hb2]++;
      }
    }

    GmanMeanStderr const u1 = u1Stat.result();
    checkNear(u1.mean, 0.5, u1.stderrOfMean, 1e-4,
              "sample2D u1 mean is unbiased across patterns at N=" + std::to_string(n));
    GmanMeanStderr const u2 = u2Stat.result();
    checkNear(u2.mean, 0.5, u2.stderrOfMean, 1e-4,
              "sample2D u2 mean is unbiased across patterns at N=" + std::to_string(n));

    auto const total = static_cast<double>(u1Stat.count);
    bool histOk = true;
    double const p = 1.0 / 16.0;
    double const sigma = std::sqrt(p * (1.0 - p) / total);
    for (int a = 0; a < 4; ++a) {
      for (int b = 0; b < 4; ++b) {
        double const fraction = static_cast<double>(hist[a][b]) / total;
        histOk = histOk && std::fabs(fraction - p) <= std::max(5.0 * sigma, 1e-4);
      }
    }
    check(histOk, "sample2D: 4x4 (u1, u2) histogram is flat within 5sigma at N=" + std::to_string(n));
  }
}

// The strata a draw u may fall in among count equal strata of [0, 1): one,
// unless u sits within one float ulp of a stratum boundary, where float
// rounding can place it in either adjacent stratum.
struct StratumRange {
  std::uint32_t lo, hi;
};

StratumRange stratumRange(RtFloat u, std::uint32_t count) {
  auto const stratumOf = [count](RtFloat v) {
    return std::min(count - 1, static_cast<std::uint32_t>(v * static_cast<RtFloat>(count)));
  };
  return {stratumOf(std::nextafter(u, 0.0f)), stratumOf(std::nextafter(u, 1.0f))};
}

// True when some assignment of each boundary draw to one of its two strata
// makes the sorted per-stratum counts equal expected (sorted ascending). A
// set with no boundary draw has exactly one assignment.
bool stratumCountsMatch(std::vector<StratumRange> const& draws, std::uint32_t strata,
                        std::vector<std::uint32_t> const& expected) {
  constexpr std::size_t kMaxBoundaryDraws = 16;

  std::vector<std::uint32_t> fixedCounts(strata, 0);
  std::vector<StratumRange> boundary;
  for (StratumRange const& draw : draws) {
    if (draw.lo == draw.hi) {
      ++fixedCounts[draw.lo];
    } else {
      boundary.push_back(draw);
    }
  }
  if (boundary.size() > kMaxBoundaryDraws) {
    return false;
  }

  for (std::uint32_t choice = 0; choice < (1u << boundary.size()); ++choice) {
    std::vector<std::uint32_t> counts = fixedCounts;
    for (std::size_t b = 0; b < boundary.size(); ++b) {
      ++counts[(choice >> b) & 1u ? boundary[b].hi : boundary[b].lo];
    }
    std::sort(counts.begin(), counts.end());
    if (counts == expected) {
      return true;
    }
  }
  return false;
}

// Per-pattern row and column counts against the grid's own layout: every
// row full but one, every column holding n or n-1 samples. Each count
// matches exactly, sorted; only a draw at a stratum boundary may count in
// either adjacent stratum.
bool sample2DPartialLayoutHolds(std::uint32_t sampleCount, RtInt x, RtInt y) {
  Sample2DGrid const grid = sample2DGridShape(sampleCount);
  std::vector<StratumRange> columns;
  std::vector<StratumRange> rows;
  for (std::uint32_t s = 0; s < sampleCount; ++s) {
    gman::Sample2D const draw = gman::sample2D(kSeed, x, y, s, sampleCount, 41u);
    columns.push_back(stratumRange(draw.u1, grid.m));
    rows.push_back(stratumRange(draw.u2, grid.n));
  }

  std::vector<std::uint32_t> expectedRows(grid.n, grid.m);
  if (grid.r != grid.m) {
    expectedRows[0] = grid.r;
  }
  std::vector<std::uint32_t> expectedCols(grid.r, grid.n);
  expectedCols.resize(grid.m, grid.n - 1);
  std::sort(expectedRows.begin(), expectedRows.end());
  std::sort(expectedCols.begin(), expectedCols.end());

  return stratumCountsMatch(rows, grid.n, expectedRows) && stratumCountsMatch(columns, grid.m, expectedCols);
}

void checkSample2DPartialGridLayout() {
  std::vector<Pixel> const pixels = {{0, 0}, {17, 5}, {640, 480}, {99, 1}, {4000, 4000}};
  for (std::uint32_t n : {2u, 3u, 5u, 32u, 128u, 512u}) {
    bool ok = true;
    for (Pixel const& px : pixels) {
      ok = ok && sample2DPartialLayoutHolds(n, px.x, px.y);
    }
    check(ok, "sample2D: per-pattern row and column counts match the grid's own layout at N=" + std::to_string(n));
  }
}

// The short row's own index, and which of its columns stay filled, are
// chosen uniformly per pattern: over many patterns at N=32 (m=5, n=7,
// r=2), each row index is short about 1/7 of the time and each column is
// among the missing m-r about (m-r)/m of the time, both within 5 sigma.
void checkSample2DShortRowAndColumnsUniform() {
  constexpr std::uint32_t kN = 32u;
  Sample2DGrid const grid = sample2DGridShape(kN);
  constexpr int kPatterns = 1 << 13;
  std::vector<int> shortRowFrequency(grid.n, 0);
  std::vector<int> missingColumnFrequency(grid.m, 0);

  for (int p = 0; p < kPatterns; ++p) {
    auto const x = static_cast<RtInt>(p);
    std::vector<std::uint32_t> rowCounts(grid.n, 0);
    std::vector<std::uint32_t> colCounts(grid.m, 0);
    for (std::uint32_t s = 0; s < kN; ++s) {
      gman::Sample2D const draw = gman::sample2D(kSeed, x, 0, s, kN, 42u);
      auto const col = std::min(grid.m - 1, static_cast<std::uint32_t>(draw.u1 * static_cast<RtFloat>(grid.m)));
      auto const row = std::min(grid.n - 1, static_cast<std::uint32_t>(draw.u2 * static_cast<RtFloat>(grid.n)));
      ++colCounts[col];
      ++rowCounts[row];
    }
    for (std::uint32_t j = 0; j < grid.n; ++j) {
      if (rowCounts[j] < grid.m) {
        shortRowFrequency[static_cast<std::size_t>(j)]++;
      }
    }
    for (std::uint32_t i = 0; i < grid.m; ++i) {
      if (colCounts[i] < grid.n) {
        missingColumnFrequency[static_cast<std::size_t>(i)]++;
      }
    }
  }

  bool rowUniform = true;
  double const pRow = 1.0 / static_cast<double>(grid.n);
  double const sigmaRow = std::sqrt(pRow * (1.0 - pRow) / static_cast<double>(kPatterns));
  for (std::uint32_t j = 0; j < grid.n; ++j) {
    double const fraction =
        static_cast<double>(shortRowFrequency[static_cast<std::size_t>(j)]) / static_cast<double>(kPatterns);
    rowUniform = rowUniform && std::fabs(fraction - pRow) <= 5.0 * sigmaRow;
  }
  check(rowUniform, "sample2D at N=32: the short row's index is uniform across patterns within 5sigma");

  bool colUniform = true;
  double const pCol = static_cast<double>(grid.m - grid.r) / static_cast<double>(grid.m);
  double const sigmaCol = std::sqrt(pCol * (1.0 - pCol) / static_cast<double>(kPatterns));
  for (std::uint32_t i = 0; i < grid.m; ++i) {
    double const fraction =
        static_cast<double>(missingColumnFrequency[static_cast<std::size_t>(i)]) / static_cast<double>(kPatterns);
    colUniform = colUniform && std::fabs(fraction - pCol) <= 5.0 * sigmaCol;
  }
  check(colUniform,
        "sample2D at N=32: the short row's missing columns are a uniform subset across patterns within 5sigma");
}

} // namespace

int main() {
  checkUnitFloatExact();
  checkSampleHashReproducesAndPins();
  checkUnitFloatUniformity();
  checkSample1DFillsStrata();
  checkSampleHashPairIndependence();
  checkSample1DSample2DIndependence();
  checkSample2DFillsGrid();
  checkSample2DBoundsAtN7();
  checkSample2DRowColumnCorrelation();
  checkPatternDecorrelation();
  checkScheduleIndependence();
  checkWithinPixelIndependence();
  checkPermutationReach();
  checkZeroSampleCount();
  checkLargeSampleCounts();
  checkClampBelowOne();
  checkSample2DFullGridDigestUnchanged();
  checkSample2DPartialGridUnbiased();
  checkSample2DPartialGridLayout();
  checkSample2DShortRowAndColumnsUniform();

  return checkSummary("gman's sampler holds its statistical and schedule contracts");
}
