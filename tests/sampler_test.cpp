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

  MeanStderr const mean = meanStderr(draws);
  checkNear(mean.mean, 0.5, mean.stderrOfMean, 1e-4, "unitFloat draws: mean within 5sigma of 1/2");

  std::vector<double> sqDev(kCount);
  for (std::uint32_t i = 0; i < kCount; ++i) {
    sqDev[i] = (draws[i] - 0.5) * (draws[i] - 0.5);
  }
  MeanStderr const variance = meanStderr(sqDev);
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
    MeanStderr const stat = correlationStderr(a, b);
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
  MeanStderr const stat = correlationStderr(sample1DValues, sample2DU1Values);
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

// Permutation reach: Kensler's permute() for length 16 has a fixed image
// of at most 2^14 distinct permutations -- independently verified, its
// 32-bit seed selects among at most 16384 sixteen-element permutations
// regardless of how the seed is derived -- so 2^14 pixel-keyed draws hit
// a birthday bound near 63% distinct, not 99%. fa2bc5a's T-function
// reached only 128 of 16384 (0.78%); this floor sits far above that and
// comfortably below the unreachable ceiling.
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

// The reviewer's two inputs do not reach the rounding edge in this
// implementation's arithmetic; these do, both landing on the unclamped
// value 1.0000000000 at sampleCount 4294836225 -- large enough that
// ulp(count)/2 covers the jitter's rounding window. Both stay below 1
// with clampBelowOne.
void checkClampBelowOne() {
  RtFloat const s1 = gman::sample1D(12345u, 191, 0, 50543188u, 4294836225u, 0u);
  check(s1 < 1.0f, "sample1D(12345, 191, 0, 50543188, 4294836225, 0) stays below 1");
  gman::Sample2D const s2 = gman::sample2D(12345u, 13, 0, 41770753u, 4294836225u, 0u);
  check(s2.u1 < 1.0f, "sample2D(12345, 13, 0, 41770753, 4294836225, 0).u1 stays below 1");
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

  return checkSummary("gman's sampler holds its statistical and schedule contracts");
}
