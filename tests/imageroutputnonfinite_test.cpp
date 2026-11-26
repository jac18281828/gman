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
 * The colour an imager returns replaces the pixel, a NaN or infinite channel
 * read as 0 and a finite channel kept: gman::runImager stores what the imager
 * answers through the same guard as the colour it hands the imager.
 */

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
  check(pixel.getRed() == 0.0f && pixel.getGreen() == 0.0f && pixel.getBlue() == 0.5f,
        "a colour an imager returned (NaN, inf, 0.5) is stored as (0, 0, 0.5)");
  return checkSummary("imageroutputnonfinite: all checks passed");
}
