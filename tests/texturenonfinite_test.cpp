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
 * A texture lookup at a non-finite s or t answers NaN in every channel, and
 * at a finite coordinate beyond RtInt's range answers the texel its wrap
 * mode defines there, on the checker tests/checkertexture.h writes (row 0:
 * red, green; row 1: blue, white). A debug build's UBSan traps the
 * out-of-range conversion the guard prevents.
 */

#include <cmath>
#include <limits>
#include <string>

#include "check.h"
#include "checkertexture.h"
#include "gmancolor.h"
#include "gmantexture.h"
#include "ri.h"

namespace {

constexpr RtFloat kNaN = std::numeric_limits<RtFloat>::quiet_NaN();
constexpr RtFloat kInfinity = std::numeric_limits<RtFloat>::infinity();
constexpr RtFloat kBeyondRange = 1.75e9f;
constexpr RtFloat kOverflowsProduct = 3e38f;
constexpr RtFloat kTopRowT = 0.25f;
constexpr RtFloat kLeftColumnS = 0.25f;

const GMANColor kRed(1.0f, 0.0f, 0.0f);
const GMANColor kGreen(0.0f, 1.0f, 0.0f);
const GMANColor kBlue(0.0f, 0.0f, 1.0f);
const GMANColor kBlack(0.0f, 0.0f, 0.0f);

bool colorNear(GMANColor const& got, GMANColor const& want) {
  return std::fabs(got.getRed() - want.getRed()) <= 1e-4f && std::fabs(got.getGreen() - want.getGreen()) <= 1e-4f &&
         std::fabs(got.getBlue() - want.getBlue()) <= 1e-4f;
}

bool allNaN(GMANColor const& c) {
  return std::isnan(c.getRed()) && std::isnan(c.getGreen()) && std::isnan(c.getBlue());
}

std::string describe(GMANColor const& c) {
  return "(" + std::to_string(c.getRed()) + ", " + std::to_string(c.getGreen()) + ", " + std::to_string(c.getBlue()) +
         ")";
}

void checkColor(GMANColor const& got, GMANColor const& want, std::string const& what) {
  check(colorNear(got, want), what + ": got " + describe(got) + ", want " + describe(want));
}

char const* wrapName(gman::TextureWrap wrap) {
  switch (wrap) {
  case gman::TEXTURE_CLAMP:
    return "clamp";
  case gman::TEXTURE_PERIODIC:
    return "periodic";
  default:
    return "black";
  }
}

void testNonFiniteCoordinate(gman::TextureCache& cache, std::string const& name) {
  for (gman::TextureWrap const wrap : {gman::TEXTURE_CLAMP, gman::TEXTURE_PERIODIC, gman::TEXTURE_BLACK}) {
    for (RtFloat const bad : {kNaN, kInfinity, -kInfinity}) {
      std::string const what = std::string(wrapName(wrap)) + " wrap at " + std::to_string(bad);
      check(allNaN(cache.sample(name, bad, kTopRowT, wrap)), what + " in s answers NaN");
      check(allNaN(cache.sample(name, kLeftColumnS, bad, wrap)), what + " in t answers NaN");
    }
    checkColor(cache.sample(name, kLeftColumnS, kTopRowT, wrap), kRed, std::string(wrapName(wrap)) + " wrap in range");
  }
}

void testBeyondRange(gman::TextureCache& cache, std::string const& name) {
  using gman::TEXTURE_BLACK;
  using gman::TEXTURE_CLAMP;
  using gman::TEXTURE_PERIODIC;
  checkColor(cache.sample(name, kBeyondRange, kTopRowT, TEXTURE_CLAMP), kGreen, "clamp past the right edge by 1.75e9");
  checkColor(cache.sample(name, -kBeyondRange, kTopRowT, TEXTURE_CLAMP), kRed, "clamp past the left edge by 1.75e9");
  checkColor(cache.sample(name, kLeftColumnS, kBeyondRange, TEXTURE_CLAMP), kBlue, "clamp past the bottom by 1.75e9");
  checkColor(cache.sample(name, kLeftColumnS, -kBeyondRange, TEXTURE_CLAMP), kRed, "clamp past the top by 1.75e9");
  checkColor(cache.sample(name, kBeyondRange, kTopRowT, TEXTURE_PERIODIC), kRed, "periodic at s of 1.75e9");
  checkColor(cache.sample(name, -kBeyondRange, kTopRowT, TEXTURE_PERIODIC), kRed, "periodic at s of -1.75e9");
  checkColor(cache.sample(name, kLeftColumnS, kBeyondRange, TEXTURE_PERIODIC), kRed, "periodic at t of 1.75e9");
  checkColor(cache.sample(name, kBeyondRange, kTopRowT, TEXTURE_BLACK), kBlack, "black beyond the right edge");
  checkColor(cache.sample(name, kLeftColumnS, -kBeyondRange, TEXTURE_BLACK), kBlack, "black beyond the top");
}

void testProductOverflowsFloat(gman::TextureCache& cache, std::string const& name) {
  using gman::TEXTURE_BLACK;
  using gman::TEXTURE_CLAMP;
  using gman::TEXTURE_PERIODIC;
  checkColor(cache.sample(name, kOverflowsProduct, kTopRowT, TEXTURE_CLAMP), kGreen,
             "clamp where s * width overflows a float");
  checkColor(cache.sample(name, -kOverflowsProduct, kTopRowT, TEXTURE_CLAMP), kRed,
             "clamp where s * width overflows a float, negative");
  checkColor(cache.sample(name, kOverflowsProduct, kTopRowT, TEXTURE_PERIODIC), kRed,
             "periodic where s * width overflows a float");
  checkColor(cache.sample(name, kOverflowsProduct, kTopRowT, TEXTURE_BLACK), kBlack,
             "black where s * width overflows a float");
  RtFloat const widest = std::numeric_limits<RtFloat>::max();
  checkColor(cache.sample(name, widest, kTopRowT, TEXTURE_CLAMP), kGreen, "clamp at the largest finite s");
  checkColor(cache.sample(name, kLeftColumnS, -widest, TEXTURE_CLAMP), kRed, "clamp at the most negative finite t");
}

} // namespace

int main() {
  std::string const name = "texturenonfinite_checker.tif";
  if (!writeCheckerTexture(name)) {
    check(false, "the checker texture is written");
    return checkSummary("texturenonfinite: all checks passed");
  }
  gman::TextureCache cache;
  testNonFiniteCoordinate(cache, name);
  testBeyondRange(cache, name);
  testProductOverflowsFloat(cache, name);
  return checkSummary("texturenonfinite: all checks passed");
}
