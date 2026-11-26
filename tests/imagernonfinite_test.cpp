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
 * gman::runImager hands the imager a Ci with every channel finite: a channel
 * a sinc filter's negative lobe leaves below 0 becomes NaN under a gamma
 * other than 1, and arrives as 0. The imager here returns the Ci it was
 * given, so the image afterwards is the imager's own input.
 */

#include <cmath>
#include <limits>
#include <string>

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

class EchoImager : public GMANImagerShader {
public:
  ShaderType getType(RtVoid) const override { return IMAGER; }
  GMANColor const& computeCi(GMANImagerEnv& ie) const override { return ie.Ci; }
  GMANColor const& computeOi(GMANImagerEnv& ie) const override { return ie.Oi; }
};

std::string describe(GMANColor const& c) {
  return "(" + std::to_string(c.getRed()) + ", " + std::to_string(c.getGreen()) + ", " + std::to_string(c.getBlue()) +
         ")";
}

bool allFinite(GMANColor const& c) {
  return std::isfinite(c.getRed()) && std::isfinite(c.getGreen()) && std::isfinite(c.getBlue());
}

void testNegativeChannelUnderGammaReachesImagerAsZero() {
  GMANBitmap image(2, 1, GMANColor(0.0f, 0.0f, 0.0f));
  image.setPixel(0, 0, GMANColor(-0.5f, 0.25f, 0.5f));
  image.setPixel(1, 0, GMANColor(kNaN, kInfinity, -kInfinity));
  GMANOptions options;
  options.setExposure(1.0f, 2.2f);

  gman::runImager(image, EchoImager(), options);

  GMANColor const negative = image.getPixel(0, 0);
  check(allFinite(negative) && negative.getRed() == 0.0f && negative.getGreen() > 0.0f && negative.getBlue() > 0.0f,
        "a negative channel under gamma 2.2 reaches the imager as 0, got " + describe(negative));
  GMANColor const nonFinite = image.getPixel(1, 0);
  check(allFinite(nonFinite),
        "a NaN or infinite pixel reaches the imager with every channel finite, got " + describe(nonFinite));
}

void testNegativeChannelWithoutGammaReachesImagerUnchanged() {
  GMANBitmap image(1, 1, GMANColor(0.0f, 0.0f, 0.0f));
  image.setPixel(0, 0, GMANColor(-0.5f, 0.25f, 2.0f));
  GMANOptions options;

  gman::runImager(image, EchoImager(), options);

  GMANColor const pixel = image.getPixel(0, 0);
  check(pixel.getRed() == -0.5f && pixel.getGreen() == 0.25f && pixel.getBlue() == 2.0f,
        "a finite pixel at gain and gamma 1 reaches the imager unchanged, got " + describe(pixel));
}

} // namespace

int main() {
  testNegativeChannelUnderGammaReachesImagerAsZero();
  testNegativeChannelWithoutGammaReachesImagerUnchanged();
  return checkSummary("imagernonfinite: all checks passed");
}
