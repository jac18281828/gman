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
 * RiFormat ignores a request whose pixel aspect ratio is NaN or infinite
 * and keeps the format it had, so the raster size never converts a NaN to an
 * integer; a finite ratio is stored.
 */

#include <limits>
#include <string>

#include "check.h"
#include "gmanrendermanimpl.h"
#include "ri.h"

namespace {

constexpr RtFloat kNaN = std::numeric_limits<RtFloat>::quiet_NaN();
constexpr RtFloat kInfinity = std::numeric_limits<RtFloat>::infinity();

class OptionReadingRenderMan : public GMANRenderManImpl {
public:
  GMANOptions& options() { return getOptions(); }
};

void testNonFiniteAspectIsIgnored() {
  OptionReadingRenderMan fresh;
  GMANOptions::RasterInfo const freshRaster = fresh.options().getRasterInfo();
  for (RtFloat const bad : {kNaN, kInfinity, -kInfinity}) {
    OptionReadingRenderMan impl;
    impl.RiFormat(320, 200, bad);
    GMANOptions::FormatStruct const format = impl.options().getFormat();
    GMANOptions::RasterInfo const raster = impl.options().getRasterInfo();
    check(format.xres == fresh.options().getFormat().xres && format.yres == fresh.options().getFormat().yres,
          "Format with a pixel aspect ratio of " + std::to_string(bad) + " keeps the format's own size");
    check(raster.xres == freshRaster.xres && raster.yres == freshRaster.yres,
          "Format with a pixel aspect ratio of " + std::to_string(bad) + " keeps the raster size");
  }
}

void testFiniteAspectIsStored() {
  OptionReadingRenderMan impl;
  impl.RiFormat(320, 200, 2.0f);
  GMANOptions::FormatStruct const format = impl.options().getFormat();
  check(format.xres == 320 && format.yres == 200 && format.pixelAspectRatio == 2.0f, "Format 320 200 2 is stored");
}

} // namespace

int main() {
  testNonFiniteAspectIsIgnored();
  testFiniteAspectIsStored();
  return checkSummary("formatnonfinite: all checks passed");
}
