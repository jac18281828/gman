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
 * The shading language defines mod(a, b) as a - b * floor(a / b), so a
 * negative multiple of b is congruent to 0. GMANMod answers b. WILL_FAIL until
 * GMANMod follows the definition for a negative dividend.
 */

#include <format>

#include "check.h"
#include "gmanmath.h"

int main() {
  const RtFloat actual = GMANMod(-4, 2);
  check(actual == 0.0f, std::format("GMANMod(-4, 2): expected 0, got {}", actual));
  return checkSummary("GMANMod gives 0 for a negative multiple of the divisor");
}
