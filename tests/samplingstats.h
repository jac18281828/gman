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
 * Statistics shared by gman's sampler and warp tests: a mean and its
 * standard error, a 5-sigma-or-floor tolerance check, and a Pearson
 * correlation with its own standard error under independence.
 */

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

#include "check.h"

struct GmanMeanStderr {
  double mean;
  double stderrOfMean;
};

// The sample mean of values and the standard error of that mean -- the
// sample standard deviation over sqrt(n).
inline GmanMeanStderr meanStderr(std::vector<double> const& values) {
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

// Pearson correlation of a and b, and its standard error under
// independence: the per-item product of standardized a and standardized
// b has expectation 0 and unit variance when a and b are independent, so
// its own mean/stderr is the correlation and its standard error.
inline GmanMeanStderr correlationStderr(std::vector<double> const& a, std::vector<double> const& b) {
  GmanMeanStderr const statA = meanStderr(a);
  GmanMeanStderr const statB = meanStderr(b);
  double const stdA = statA.stderrOfMean * std::sqrt(static_cast<double>(a.size()));
  double const stdB = statB.stderrOfMean * std::sqrt(static_cast<double>(b.size()));

  std::vector<double> products(a.size());
  for (std::size_t i = 0; i < a.size(); ++i) {
    products[i] = ((a[i] - statA.mean) / stdA) * ((b[i] - statB.mean) / stdB);
  }
  return meanStderr(products);
}

// Asserts value within 5 sigma of expected, or within floor when the
// variance is zero or near it.
inline void checkNear(double value, double expected, double sigma, double floor, std::string const& what) {
  double const tolerance = std::max(5.0 * sigma, floor);
  check(std::fabs(value - expected) <= tolerance, what);
}
