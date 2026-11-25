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
 * IEEE 754 returns NaN for a NaN operand, and the shading language's sign(x)
 * has no other answer for it. GMANSign answers 0. WILL_FAIL until GMANSign
 * returns NaN for NaN.
 */

#include <cmath>
#include <format>
#include <limits>

#include "check.h"
#include "gmanmath.h"

int main() {
  const RtFloat actual = GMANSign(std::numeric_limits<RtFloat>::quiet_NaN());
  check(std::isnan(actual), std::format("GMANSign(NaN): expected NaN, got {}", actual));
  return checkSummary("GMANSign gives NaN for NaN");
}
