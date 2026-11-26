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
 * RiExposure ignores a request whose gain or gamma is NaN or infinite and
 * keeps the exposure it had, so no infinite gain reaches the output's gamma
 * correction; a finite request is stored.
 */

#include <limits>
#include <string>

#include "check.h"
#include "gmandefaults.h"
#include "gmanrendermanimpl.h"
#include "ri.h"

namespace {

constexpr RtFloat kNaN = std::numeric_limits<RtFloat>::quiet_NaN();
constexpr RtFloat kInfinity = std::numeric_limits<RtFloat>::infinity();

class OptionReadingRenderMan : public GMANRenderManImpl {
public:
  GMANOptions& options() { return getOptions(); }
};

void testNonFiniteExposureIsIgnored() {
  for (RtFloat const bad : {kNaN, kInfinity, -kInfinity}) {
    OptionReadingRenderMan gainImpl;
    gainImpl.RiExposure(bad, 2.2f);
    check(gainImpl.options().getExposure().gain == DefaultGain &&
              gainImpl.options().getExposure().gamma == DefaultGamma,
          "Exposure with a gain of " + std::to_string(bad) + " keeps the exposure in force");
    OptionReadingRenderMan gammaImpl;
    gammaImpl.RiExposure(2.0f, bad);
    check(gammaImpl.options().getExposure().gain == DefaultGain &&
              gammaImpl.options().getExposure().gamma == DefaultGamma,
          "Exposure with a gamma of " + std::to_string(bad) + " keeps the exposure in force");
  }
}

void testFiniteExposureIsStored() {
  OptionReadingRenderMan impl;
  impl.RiExposure(2.0f, 2.2f);
  check(impl.options().getExposure().gain == 2.0f && impl.options().getExposure().gamma == 2.2f,
        "Exposure 2 2.2 is stored");
}

} // namespace

int main() {
  testNonFiniteExposureIsIgnored();
  testFiniteExposureIsStored();
  return checkSummary("exposurenonfinite: all checks passed");
}
