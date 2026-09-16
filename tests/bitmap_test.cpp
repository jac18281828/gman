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
 * Pins Class E of the clang-tidy-typed-fixes prompt: GMANBitmap rejects a
 * size whose pixel count overflows int instead of allocating a wrapped,
 * too-small buffer, and assignment always takes the source's own
 * dimensions.
 *
 * Revert check: reverting the throw in GMANBitmap::set makes the negative
 * and overflow cases below go red; reverting operator='s defaulted copy
 * back to its "&&" guard makes the dimension-mismatch assignment case go
 * red.
 */

#include "check.h"
#include "gmanbitmap.h"
#include "gmancolor.h"
#include "gmanerror.h"

namespace {

bool colorsEqual(const GMANColor &a, const GMANColor &b) {
  return a.getRed() == b.getRed() && a.getGreen() == b.getGreen() &&
         a.getBlue() == b.getBlue();
}

void testNegativeWidthThrows() {
  bool threw = false;
  try {
    GMANBitmap bmp(-1, 4, GMANColor(0.0));
  } catch (GMANError &) {
    threw = true;
  }
  check(threw, "a negative width throws GMANError");
}

void testOverflowingSizeThrows() {
  bool threw = false;
  try {
    // 65536 * 65536 == 2^32, which wraps a 32-bit int to 0.
    GMANBitmap bmp(65536, 65536, GMANColor(0.0));
  } catch (GMANError &) {
    threw = true;
  }
  check(threw, "a pixel count that overflows int throws GMANError");
}

void testZeroByZeroConstructsWithoutThrowing() {
  bool threw = false;
  try {
    GMANBitmap bmp(0, 0, GMANColor(0.0));
  } catch (GMANError &) {
    threw = true;
  }
  check(!threw, "a 0x0 bitmap constructs without throwing");

  bool defaultThrew = false;
  try {
    GMANBitmap bmp;
  } catch (GMANError &) {
    defaultThrew = true;
  }
  check(!defaultThrew, "the default constructor builds 0x0 without throwing");
}

void testAssignmentCopiesBothDimensions() {
  // Source: 4x8, every pixel a distinct color.
  GMANBitmap src(4, 8, GMANColor(0.0));
  for (int y = 0; y < 8; y++) {
    for (int x = 0; x < 4; x++) {
      const GMANColorSample sample = (GMANColorSample) (x + y * 4) / 32.0f;
      src.setPixel(x, y, GMANColor(sample));
    }
  }

  // Target differs in only one dimension -- the case the unmodified "&&"
  // guard leaves at its old size.
  GMANBitmap dst(4, 2, GMANColor(1.0));
  dst = src;

  check(dst.getWidth() == 4 && dst.getHeight() == 8,
        "assignment: target takes the source's width and height");

  bool allMatch = true;
  for (int y = 0; y < 8 && allMatch; y++) {
    for (int x = 0; x < 4 && allMatch; x++) {
      if (!colorsEqual(dst.getPixel(x, y), src.getPixel(x, y))) {
        allMatch = false;
      }
    }
  }
  check(allMatch, "assignment: every pixel equals the source's");
}

} // namespace

int main() {
  testNegativeWidthThrows();
  testOverflowingSizeThrows();
  testZeroByZeroConstructsWithoutThrowing();
  testAssignmentCopiesBothDimensions();

  return checkSummary("bitmap holds");
}
