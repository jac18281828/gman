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
 * GMANOptions::setPixelSamples stores a count rounded to the nearest integer
 * and held in [1, 16], a NaN as 1, so every renderer's conversion to int
 * reads a finite count whichever way the count arrived: through RiPixelSamples
 * or straight into the setter.
 */

#include <limits>
#include <string>

#include "check.h"
#include "gmanoptions.h"
#include "gmanrendermanimpl.h"
#include "ri.h"

namespace {

constexpr RtFloat kNaN = std::numeric_limits<RtFloat>::quiet_NaN();
constexpr RtFloat kInfinity = std::numeric_limits<RtFloat>::infinity();

class OptionReadingRenderMan : public GMANRenderManImpl {
public:
  GMANOptions& options() { return getOptions(); }
};

void checkStored(GMANOptions const& options, RtFloat xsamples, RtFloat ysamples, std::string const& what) {
  GMANOptions::PixelSamplesStruct const& stored = options.getPixelSamples();
  check(stored.xsamples == xsamples && stored.ysamples == ysamples,
        what + ": stored " + std::to_string(stored.xsamples) + " by " + std::to_string(stored.ysamples) + ", want " +
            std::to_string(xsamples) + " by " + std::to_string(ysamples));
}

void testSetterStoresAFiniteCount() {
  GMANOptions options;
  options.setPixelSamples(kNaN, kNaN);
  checkStored(options, 1.0f, 1.0f, "setPixelSamples(NaN, NaN)");
  options.setPixelSamples(kInfinity, -kInfinity);
  checkStored(options, 16.0f, 1.0f, "setPixelSamples(+inf, -inf)");
  options.setPixelSamples(1e30f, -1e30f);
  checkStored(options, 16.0f, 1.0f, "setPixelSamples(1e30, -1e30)");
  options.setPixelSamples(kNaN, 3.0f);
  checkStored(options, 1.0f, 3.0f, "setPixelSamples(NaN, 3)");
}

void testSetterRoundsAndClamps() {
  GMANOptions options;
  options.setPixelSamples(2.4f, 2.5f);
  checkStored(options, 2.0f, 3.0f, "setPixelSamples(2.4, 2.5)");
  options.setPixelSamples(0.2f, 100.0f);
  checkStored(options, 1.0f, 16.0f, "setPixelSamples(0.2, 100)");
  options.setPixelSamples(4.0f, 8.0f);
  checkStored(options, 4.0f, 8.0f, "setPixelSamples(4, 8)");
}

void testRiPixelSamplesStillStoresTheSameCounts() {
  OptionReadingRenderMan impl;
  impl.RiPixelSamples(kNaN, 3.0f);
  checkStored(impl.options(), 1.0f, 3.0f, "RiPixelSamples(NaN, 3)");
  impl.RiPixelSamples(kInfinity, 2.6f);
  checkStored(impl.options(), 16.0f, 3.0f, "RiPixelSamples(+inf, 2.6)");
  impl.RiPixelSamples(0.0f, -kInfinity);
  checkStored(impl.options(), 1.0f, 1.0f, "RiPixelSamples(0, -inf)");
}

} // namespace

int main() {
  testSetterStoresAFiniteCount();
  testSetterRoundsAndClamps();
  testRiPixelSamplesStillStoresTheSameCounts();
  return checkSummary("pixelsamplesnonfinite: all checks passed");
}
