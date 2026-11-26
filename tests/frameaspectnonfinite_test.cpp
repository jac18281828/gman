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
 * RiFrameAspectRatio ignores a NaN or infinite ratio and keeps the frame it
 * had. GMANOptions::getRasterInfo converts every size and crop bound defined
 * for any finite ratio, pixel aspect ratio and crop window: a value past
 * RtInt's range saturates at its end. GMANSampleBuffer refuses a sample
 * extent that overflows int instead of multiplying it. A debug build's UBSan
 * traps each out-of-range conversion the guards prevent.
 */

#include <climits>
#include <limits>
#include <string>

#include "check.h"
#include "gmanerror.h"
#include "gmanrendermanimpl.h"
#include "gmansamplebuffer.h"
#include "ri.h"

namespace {

constexpr RtFloat kNaN = std::numeric_limits<RtFloat>::quiet_NaN();
constexpr RtFloat kInfinity = std::numeric_limits<RtFloat>::infinity();
constexpr RtInt kLargestFloatBelowIntRange = 2147483520;

class OptionReadingRenderMan : public GMANRenderManImpl {
public:
  GMANOptions& options() { return getOptions(); }
};

void testNonFiniteRatioIsIgnored() {
  OptionReadingRenderMan fresh;
  RtFloat const defaultRatio = fresh.options().getFrameAspectRatio();
  for (RtFloat const bad : {kNaN, kInfinity, -kInfinity}) {
    OptionReadingRenderMan impl;
    impl.RiFrameAspectRatio(bad);
    check(impl.options().getFrameAspectRatio() == defaultRatio,
          "FrameAspectRatio " + std::to_string(bad) + " keeps the frame's own ratio");
    GMANOptions::RasterInfo const raster = impl.options().getRasterInfo();
    GMANOptions::RasterInfo const freshRaster = fresh.options().getRasterInfo();
    check(raster.xres == freshRaster.xres && raster.yres == freshRaster.yres,
          "FrameAspectRatio " + std::to_string(bad) + " keeps the raster size");
  }
}

void testFiniteRatioIsStored() {
  OptionReadingRenderMan impl;
  impl.RiFrameAspectRatio(2.0f);
  check(impl.options().getFrameAspectRatio() == 2.0f, "FrameAspectRatio 2 is stored");
}

void testRasterSizeSaturates() {
  OptionReadingRenderMan wide;
  wide.RiFrameAspectRatio(1e30f);
  GMANOptions::RasterInfo const wideRaster = wide.options().getRasterInfo();
  check(wideRaster.xres == kLargestFloatBelowIntRange && wideRaster.yres == 480,
        "a frame aspect ratio of 1e30 saturates xres and keeps yres");

  OptionReadingRenderMan negative;
  negative.RiFrameAspectRatio(-1e30f);
  check(negative.options().getRasterInfo().yres <= 0, "a frame aspect ratio of -1e30 leaves no rows");

  OptionReadingRenderMan narrowPixels;
  narrowPixels.RiFormat(640, 480, 1e-30f);
  narrowPixels.RiFrameAspectRatio(2.0f);
  check(narrowPixels.options().getRasterInfo().xres == kLargestFloatBelowIntRange,
        "a pixel aspect ratio of 1e-30 saturates xres");

  OptionReadingRenderMan widest;
  widest.RiFormat(INT_MAX, 1, 1.0f);
  GMANOptions::RasterInfo const raster = widest.options().getRasterInfo();
  check(raster.rxmin == 0 && raster.rxmax == kLargestFloatBelowIntRange,
        "a crop bound a float rounds past RtInt's range saturates");
}

void testNanPixelAspectReadsAsEmptyFrame() {
  OptionReadingRenderMan impl;
  impl.options().setFormat(640, 480, kNaN);
  GMANOptions::RasterInfo const raster = impl.options().getRasterInfo();
  check(raster.yres == 0, "a NaN pixel aspect ratio reads as a frame of 0 rows");
}

void testSampleExtentOverflowIsRefused() {
  GMANColor const background(0.0f, 0.0f, 0.0f);
  bool refused = false;
  try {
    GMANSampleBuffer buffer(1 << 28, 1, 16, 1, background);
  } catch (GMANError const& error) {
    refused = error.getCode() == RIE_LIMIT;
  }
  check(refused, "a sample extent past int's range is refused with RIE_LIMIT");
}

} // namespace

int main() {
  testNonFiniteRatioIsIgnored();
  testFiniteRatioIsStored();
  testRasterSizeSaturates();
  testNanPixelAspectReadsAsEmptyFrame();
  testSampleExtentOverflowIsRefused();
  return checkSummary("frameaspectnonfinite: all checks passed");
}
