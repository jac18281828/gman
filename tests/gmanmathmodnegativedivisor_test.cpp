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
 * The shading language defines mod(a, b) as a - b * floor(a / b) for either
 * sign of b, so the result takes the sign of a negative divisor. GMANMod
 * answers as if b were positive. WILL_FAIL until GMANMod follows the
 * definition for a negative divisor.
 */

#include <format>

#include "check.h"
#include "gmanmath.h"

int main() {
  const RtFloat positiveDividend = GMANMod(5, -3);
  check(positiveDividend == -1.0f, std::format("GMANMod(5, -3): expected -1, got {}", positiveDividend));

  const RtFloat negativeDividend = GMANMod(-5, -3);
  check(negativeDividend == -2.0f, std::format("GMANMod(-5, -3): expected -2, got {}", negativeDividend));
  return checkSummary("GMANMod follows the definition for a negative divisor");
}
