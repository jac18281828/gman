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
 * RiPixelFilter ignores a request whose width is NaN or infinite and keeps
 * the filter it had; a finite width is stored. GMANSampleBuffer::resolve
 * converts its sample-index bounds defined for every width, a finite one
 * wider than the frame included, which a debug build's UBSan traps when the
 * conversion leaves int's range.
 */

#include <cmath>
#include <limits>
#include <string>

#include "check.h"
#include "gmancolor.h"
#include "gmandefaults.h"
#include "gmanframebuffer.h"
#include "gmanrendermanimpl.h"
#include "gmansamplebuffer.h"
#include "ri.h"

namespace {

constexpr RtFloat kNaN = std::numeric_limits<RtFloat>::quiet_NaN();
constexpr RtFloat kInfinity = std::numeric_limits<RtFloat>::infinity();
constexpr RtFloat kWiderThanAnyFrame = 1e30f;

class OptionReadingRenderMan : public GMANRenderManImpl {
public:
  GMANOptions& options() { return getOptions(); }
};

void testNonFiniteWidthIsIgnored() {
  for (RtFloat const bad : {kNaN, kInfinity, -kInfinity}) {
    std::string const what = std::to_string(bad);
    {
      OptionReadingRenderMan impl;
      impl.RiPixelFilter(RiBoxFilter, bad, 3.0f);
      GMANOptions::PixelFilterStruct const& filter = impl.options().getPixelFilter();
      check(filter.filterfunc == DefaultFilterFunc && filter.xwidth == DefaultFilterXWidth &&
                filter.ywidth == DefaultFilterYWidth,
            "PixelFilter with xwidth " + what + " keeps the default filter");
    }
    {
      OptionReadingRenderMan impl;
      impl.RiPixelFilter(RiBoxFilter, 3.0f, bad);
      GMANOptions::PixelFilterStruct const& filter = impl.options().getPixelFilter();
      check(filter.filterfunc == DefaultFilterFunc && filter.xwidth == DefaultFilterXWidth &&
                filter.ywidth == DefaultFilterYWidth,
            "PixelFilter with ywidth " + what + " keeps the default filter");
    }
  }
}

void testFiniteWidthIsStored() {
  OptionReadingRenderMan impl;
  impl.RiPixelFilter(RiBoxFilter, 3.0f, 4.0f);
  GMANOptions::PixelFilterStruct const& filter = impl.options().getPixelFilter();
  check(filter.filterfunc == RiBoxFilter && filter.xwidth == 3.0f && filter.ywidth == 4.0f,
        "PixelFilter with finite widths stores the filter and both widths");
}

GMANColor const kGrey(0.5f, 0.5f, 0.5f);

void checkUniformResolve(RtFloat xwidth, RtFloat ywidth, std::string const& what) {
  GMANColor const background(0.0f, 0.0f, 0.0f);
  GMANSampleBuffer buffer(4, 4, 2, 2, background);
  for (int sy = 0; sy < 8; ++sy) {
    for (int sx = 0; sx < 8; ++sx) {
      buffer.zTestAndSet(sx, sy, 1.0f, kGrey, GMANAlpha(1.0));
    }
  }
  GMANFrameBuffer frame(4, 4, background);
  buffer.resolve(&frame, RiBoxFilter, xwidth, ywidth);
  GMANColor const corner = frame.getPixel(0, 0);
  GMANColor const centre = frame.getPixel(2, 2);
  check(std::fabs(corner.getRed() - 0.5f) < 1e-5f && std::fabs(centre.getRed() - 0.5f) < 1e-5f,
        what + ": every sample inside the support averages to grey");
}

void testResolveWiderThanFrame() {
  checkUniformResolve(kWiderThanAnyFrame, kWiderThanAnyFrame, "a box filter 1e30 wide");
  checkUniformResolve(kWiderThanAnyFrame, 2.0f, "a box filter 1e30 wide in x only");
  checkUniformResolve(2.0f, kWiderThanAnyFrame, "a box filter 1e30 wide in y only");
  checkUniformResolve(std::numeric_limits<RtFloat>::max(), std::numeric_limits<RtFloat>::max(),
                      "a box filter as wide as a float allows");
}

void testResolveNonFiniteWidthStaysDefined() {
  GMANColor const background(0.0f, 0.0f, 0.0f);
  for (RtFloat const bad : {kNaN, kInfinity}) {
    GMANSampleBuffer buffer(4, 4, 2, 2, background);
    GMANFrameBuffer frame(4, 4, background);
    buffer.resolve(&frame, RiBoxFilter, bad, bad);
    GMANColor const pixel = frame.getPixel(1, 1);
    check(std::isfinite(pixel.getRed()) && std::isfinite(pixel.getGreen()) && std::isfinite(pixel.getBlue()),
          "resolve at width " + std::to_string(bad) + " writes a finite pixel");
  }
}

} // namespace

int main() {
  testNonFiniteWidthIsIgnored();
  testFiniteWidthIsStored();
  testResolveWiderThanFrame();
  testResolveNonFiniteWidthStaysDefined();
  return checkSummary("pixelfilternonfinite: all checks passed");
}
