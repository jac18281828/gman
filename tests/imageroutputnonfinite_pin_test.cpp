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
 * Pins the wanted behaviour: the colour an imager returns replaces the
 * pixel, so a NaN or infinite channel it returns should not reach the image.
 * gman::runImager stores the returned colour as it is, and only the output
 * stage later maps a NaN to 0, so a pixel keeps the NaN here. WILL_FAIL until
 * runImager reads a non-finite channel of the returned colour as 0.
 */

#include <cmath>
#include <limits>

#include "check.h"
#include "gmanbitmap.h"
#include "gmancolor.h"
#include "gmanimagerpass.h"
#include "gmanimagershader.h"
#include "gmanoptions.h"
#include "gmanshaderenvironment.h"
#include "ri.h"

namespace {

constexpr RtFloat kNaN = std::numeric_limits<RtFloat>::quiet_NaN();
constexpr RtFloat kInfinity = std::numeric_limits<RtFloat>::infinity();

GMANColor const kNonFinite(kNaN, kInfinity, 0.5f);

class NonFiniteImager : public GMANImagerShader {
public:
  ShaderType getType(RtVoid) const override { return IMAGER; }
  GMANColor const& computeCi(GMANImagerEnv&) const override { return kNonFinite; }
  GMANColor const& computeOi(GMANImagerEnv& ie) const override { return ie.Oi; }
};

} // namespace

int main() {
  GMANBitmap image(1, 1, GMANColor(0.0f, 0.0f, 0.0f));
  GMANOptions options;
  gman::runImager(image, NonFiniteImager(), options);
  GMANColor const pixel = image.getPixel(0, 0);
  check(std::isfinite(pixel.getRed()) && std::isfinite(pixel.getGreen()) && std::isfinite(pixel.getBlue()),
        "a pixel an imager returned (NaN, inf, 0.5) for has every channel finite");
  return checkSummary("imageroutputnonfinite: all checks passed");
}
