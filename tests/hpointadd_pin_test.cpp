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
 * Pins the wanted behavior: HPoint::operator+ yields the sum of x, y, z and w,
 * so (2, 4, 6, 2) + (10, 20, 40, 3) is (12, 24, 46, 5). The operator copies
 * its left operand and applies operator+=, which adds to a temporary, so the
 * sum keeps the left operand's x, y and z divided by its w. WILL_FAIL until
 * operator+= updates the point's own components.
 */

#include "check.h"
#include "geometrycheck.h"
#include "gmanhpoint.h"
#include "gmanreal.h"
#include "ri.h"

namespace {

using namespace geometrycheck;

template <typename T> void testAddYieldsTheComponentSum() {
  const auto a = gman::HPoint<T>(2, 4, 6, 2);
  const auto b = gman::HPoint<T>(10, 20, 40, 3);
  const auto sum = a + b;
  expectExact(member<T>("HPoint", "operator+(HPoint) adds componentwise"), sum, {12, 24, 46, 5});
}

} // namespace

int main() {
  testAddYieldsTheComponentSum<RtFloat>();
  testAddYieldsTheComponentSum<gman::PartnerReal>();
  return checkSummary("hpointadd: all checks passed");
}
