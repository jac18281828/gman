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
 * too-small buffer.
 *
 * Revert check: reverting the throw in GMANBitmap::set makes the negative
 * and overflow cases below go red.
 */

#include "check.h"
#include "gmanbitmap.h"
#include "gmancolor.h"
#include "gmanerror.h"

namespace {

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

} // namespace

int main() {
  testNegativeWidthThrows();
  testOverflowingSizeThrows();
  testZeroByZeroConstructsWithoutThrowing();

  return checkSummary("bitmap holds");
}
