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
#include <cmath>
#include <cstdint>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "check.h"
#include "gmanparallel.h"
#include "gmansampling.h"

namespace {

constexpr std::uint32_t kSeed = 12345u;

struct MeanStderr {
  double mean;
  double stderrOfMean;
};

// The sample mean of values and the standard error of that mean -- the
// sample standard deviation over sqrt(n) -- every statistical check here
// reduces to this one estimate, applied to a raw draw or to a derived
// per-item quantity.
MeanStderr meanStderr(std::vector<double> const& values) {
  double sum = 0.0;
  for (double v : values) {
    sum += v;
  }
  double const mean = sum / static_cast<double>(values.size());

  double sqSum = 0.0;
  for (double v : values) {
    sqSum += (v - mean) * (v - mean);
  }
  double const variance = sqSum / static_cast<double>(values.size() - 1);
  double const stderrOfMean = std::sqrt(variance / static_cast<double>(values.size()));
  return {mean, stderrOfMean};
}

// Pearson correlation of a and b, and the standard error of that estimate
// under independence: the per-item product of standardized a and
// standardized b has expectation 0 and unit variance when a and b are
// independent, so its own mean/stderr (meanStderr again) is the
// correlation and its standard error.
MeanStderr correlationStderr(std::vector<double> const& a, std::vector<double> const& b) {
  MeanStderr const statA = meanStderr(a);
  MeanStderr const statB = meanStderr(b);
  double const stdA = statA.stderrOfMean * std::sqrt(static_cast<double>(a.size()));
  double const stdB = statB.stderrOfMean * std::sqrt(static_cast<double>(b.size()));

  std::vector<double> products(a.size());
  for (std::size_t i = 0; i < a.size(); ++i) {
    products[i] = ((a[i] - statA.mean) / stdA) * ((b[i] - statB.mean) / stdB);
  }
  return meanStderr(products);
}

void checkNear(double value, double expected, double sigma, double floor, std::string const& what) {
  double const tolerance = std::max(5.0 * sigma, floor);
  check(std::fabs(value - expected) <= tolerance, what);
}

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

// 1. unitFloat pins its five named values exactly.
void checkUnitFloatExact() {
  check(gman::unitFloat(0u) == 0.0f, "unitFloat(0) == 0");
  check(gman::unitFloat(0xFFu) == 0.0f, "unitFloat(0xFF) == 0");
  check(gman::unitFloat(0x100u) == 0x1p-24f, "unitFloat(0x100) == 2^-24");
  check(gman::unitFloat(0xFFFFFFFFu) == 1.0f - 0x1p-24f, "unitFloat(0xFFFFFFFF) == 1 - 2^-24");
  check(gman::unitFloat(0x80000000u) == 0.5f, "unitFloat(0x80000000) == 0.5");
}

// 2. sampleHash reproduces across repeated calls, and four pinned
// tuples match values pinned by an earlier run -- the tests build with
// gcc and clang on Linux and macOS, and a pinned value catches a
// sampler that differs between them.
void checkSampleHashReproduces() {
  std::uint32_t const a = gman::sampleHash(7u, 3, 4, 5u, 6u);
  std::uint32_t const b = gman::sampleHash(7u, 3, 4, 5u, 6u);
  check(a == b, "sampleHash repeats its output for the same arguments");

  check(gman::sampleHash(1u, 2, 3, 4u, 5u) == 0x509ba2deu, "sampleHash(1, 2, 3, 4, 5) is pinned");
  check(gman::sampleHash(42u, 100, 200, 7u, 1u) == 0x4820ffc8u, "sampleHash(42, 100, 200, 7, 1) is pinned");
  check(gman::sampleHash(0u, 0, 0, 0u, 0u) == 0x00000000u, "sampleHash(0, 0, 0, 0, 0) is pinned");
  check(gman::sampleHash(0xdeadbeefu, -5, 17, 999u, 3u) == 0x3dc4bad7u,
        "sampleHash(0xdeadbeef, -5, 17, 999, 3) is pinned");
}

// 3. unitFloat(sampleHash(...)) is uniform on [0, 1); sample1D fills
// every stratum at two sample counts, three pixels and two dimensions.
void checkDistributionAndSample1DStrata() {
  constexpr std::uint32_t kCount = 1u << 16;
  std::vector<double> draws(kCount);
  std::vector<int> histogram(16, 0);
  bool inRange = true;

  for (std::uint32_t i = 0; i < kCount; ++i) {
    RtFloat const u = gman::unitFloat(gman::sampleHash(kSeed, 11, 22, i, 3u));
    if (u < 0.0f || u >= 1.0f) {
      inRange = false;
    }
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
    if (std::fabs(fraction - p) > 5.0 * sigma) {
      histOk = false;
    }
  }
  check(histOk, "unitFloat draws: 16-bin histogram within 5sigma of uniform");

  struct Pixel {
    RtInt x, y;
  };
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
        if (!strataFilledOnce(values, n)) {
          strataOk = false;
        }
      }
    }
  }
  check(strataOk, "sample1D fills every stratum once at N=16 and N=13, three pixels, two dimensions");
}

// 4. Independence: six pairings correlate within 5sigma of 0 over 2^16
// pixels at one sample index.
void checkIndependence() {
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
  std::vector<double> sample1DValues(kCount), sample2DU1Values(kCount);

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

    sample1DValues[i] = gman::sample1D(kSeed, x, 5, 0u, 16u, 3u);
    sample2DU1Values[i] = gman::sample2D(kSeed, x, 5, 0u, 16u, 3u).u1;
  }

  checkPairCorrelation(hashDimD, hashDimD1, "sampleHash: dimension d vs d+1 are independent");
  checkPairCorrelation(hashPixel, hashPixelXp1, "sampleHash: pixel (x,y) vs (x+1,y) are independent");
  checkPairCorrelation(hashPixelXY, hashPixelYX, "sampleHash: pixel (x,y) vs (y,x) are independent");
  checkPairCorrelation(hashXDim, hashDimXSwapped, "sampleHash: x vs the dimension swapped are independent");
  checkPairCorrelation(hashSeedS, hashSeedS1, "sampleHash: seed s vs s+1 are independent");
  checkPairCorrelation(sample1DValues, sample2DU1Values, "sample1D vs sample2D's u1 are independent");
}

// 5. sample2D fills every m*n cell and every N-stratum of each axis
// once, for N=16 (m=4, n=4) and N=12 (m=3, n=4); N=7 stays in bounds.
void checkSample2DStratification() {
  struct Pixel {
    RtInt x, y;
  };
  std::vector<Pixel> const pixels = {{0, 0}, {17, 5}, {640, 480}};
  std::vector<std::uint32_t> const dims = {0u, 9u};

  auto checkGrid = [](std::uint32_t n, std::uint32_t m, std::uint32_t nn, RtInt x, RtInt y, std::uint32_t dim) {
    std::vector<bool> cellSeen(static_cast<std::size_t>(m) * nn, false);
    std::vector<bool> u1Seen(n, false);
    std::vector<bool> u2Seen(n, false);
    for (std::uint32_t s = 0; s < n; ++s) {
      gman::Sample2D const p = gman::sample2D(kSeed, x, y, s, n, dim);
      if (p.u1 < 0.0f || p.u1 >= 1.0f || p.u2 < 0.0f || p.u2 >= 1.0f) {
        return false;
      }
      auto const cx = static_cast<std::uint32_t>(p.u1 * static_cast<RtFloat>(m));
      auto const cy = static_cast<std::uint32_t>(p.u2 * static_cast<RtFloat>(nn));
      std::size_t const cell = static_cast<std::size_t>(cx) * nn + cy;
      if (cx >= m || cy >= nn || cellSeen[cell]) {
        return false;
      }
      cellSeen[cell] = true;

      auto const u1Stratum = static_cast<std::uint32_t>(p.u1 * static_cast<RtFloat>(n));
      auto const u2Stratum = static_cast<std::uint32_t>(p.u2 * static_cast<RtFloat>(n));
      if (u1Stratum >= n || u1Seen[u1Stratum]) {
        return false;
      }
      u1Seen[u1Stratum] = true;
      if (u2Stratum >= n || u2Seen[u2Stratum]) {
        return false;
      }
      u2Seen[u2Stratum] = true;
    }
    for (bool s : u1Seen) {
      if (!s) {
        return false;
      }
    }
    for (bool s : u2Seen) {
      if (!s) {
        return false;
      }
    }
    return true;
  };

  bool ok16 = true;
  bool ok12 = true;
  for (Pixel const& px : pixels) {
    for (std::uint32_t dim : dims) {
      if (!checkGrid(16u, 4u, 4u, px.x, px.y, dim)) {
        ok16 = false;
      }
      if (!checkGrid(12u, 3u, 4u, px.x, px.y, dim)) {
        ok12 = false;
      }
    }
  }
  check(ok16, "sample2D fills every 4x4 cell and every 16-stratum of each axis at N=16");
  check(ok12, "sample2D fills every 3x4 cell and every 12-stratum of each axis at N=12");

  constexpr std::uint32_t kCount = 1u << 16;
  bool boundsOk = true;
  for (std::uint32_t i = 0; i < kCount; ++i) {
    auto const x = static_cast<RtInt>(i);
    for (std::uint32_t s = 0; s < 7u; ++s) {
      gman::Sample2D const p = gman::sample2D(kSeed, x, 0, s, 7u, 0u);
      if (p.u1 < 0.0f || p.u1 >= 1.0f || p.u2 < 0.0f || p.u2 >= 1.0f) {
        boundsOk = false;
      }
    }
  }
  check(boundsOk, "sample2D at N=7 stays in [0, 1)^2 over 2^16 pixels");
}

// 6. Pattern decorrelation: the (cell, u1-stratum) sequence at N=16
// differs between two pixels and between two dimensions at one pixel;
// so does sample1D's stratum-index sequence.
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

// 7. Schedule independence: a serial fill and six parallelFor fills (1,
// 3, 8 workers, over rows and over 8x8 tiles) land the same buffer,
// bit for bit.
void checkScheduleIndependence() {
  constexpr int kSide = 64;
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

  auto rowOrder = [&](RtInt index) {
    RtInt const y = index / (kSide * static_cast<RtInt>(kSamples));
    RtInt const rem = index % (kSide * static_cast<RtInt>(kSamples));
    RtInt const x = rem / static_cast<RtInt>(kSamples);
    auto const s = static_cast<std::uint32_t>(rem % static_cast<RtInt>(kSamples));
    return std::make_tuple(x, y, s);
  };

  auto tileOrder = [&](RtInt index) {
    constexpr RtInt kTileSide = 8;
    constexpr RtInt kTilesPerSide = kSide / kTileSide;
    constexpr RtInt kPixelsPerTile = kTileSide * kTileSide;

    RtInt const pixelIndex = index / static_cast<RtInt>(kSamples);
    auto const s = static_cast<std::uint32_t>(index % static_cast<RtInt>(kSamples));
    RtInt const tileIndex = pixelIndex / kPixelsPerTile;
    RtInt const withinTile = pixelIndex % kPixelsPerTile;
    RtInt const tileRow = tileIndex / kTilesPerSide;
    RtInt const tileCol = tileIndex % kTilesPerSide;
    RtInt const localRow = withinTile / kTileSide;
    RtInt const localCol = withinTile % kTileSide;
    RtInt const y = tileRow * kTileSide + localRow;
    RtInt const x = tileCol * kTileSide + localCol;
    return std::make_tuple(x, y, s);
  };

  bool allMatch = true;
  auto runAndCompare = [&](auto const& order, RtInt workers) {
    std::vector<gman::Sample2D> buffer(static_cast<std::size_t>(kTotal));
    gman::parallelFor(
        kTotal,
        [&](RtInt index, RtInt) {
          auto const [x, y, s] = order(index);
          buffer[static_cast<std::size_t>(canonicalIndex(x, y, s))] = gman::sample2D(kSeed, x, y, s, kSamples, kDim);
        },
        workers);
    for (std::size_t i = 0; i < buffer.size(); ++i) {
      if (buffer[i].u1 != serial[i].u1 || buffer[i].u2 != serial[i].u2) {
        allMatch = false;
      }
    }
  };

  for (RtInt workers : {1, 3, 8}) {
    runAndCompare(rowOrder, workers);
    runAndCompare(tileOrder, workers);
  }

  check(allMatch, "sample2D: serial and parallelFor fills match bit for bit at 1, 3 and 8 workers, row and tile order");
}

} // namespace

int main() {
  checkUnitFloatExact();
  checkSampleHashReproduces();
  checkDistributionAndSample1DStrata();
  checkIndependence();
  checkSample2DStratification();
  checkPatternDecorrelation();
  checkScheduleIndependence();

  return checkSummary("gman's sampler holds its statistical and schedule contracts");
}
