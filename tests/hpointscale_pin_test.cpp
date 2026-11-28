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
 * Pins the wanted behavior: HPoint::operator*(T) yields x, y, z and w scaled,
 * so (2, 4, 6, 2) * 4 is (8, 16, 24, 8). The operator copies its operand and
 * applies operator*=, which scales a temporary, so the product keeps the
 * operand's x, y and z divided by its w. WILL_FAIL until operator*= updates
 * the point's own components.
 */

#include "check.h"
#include "geometrycheck.h"
#include "gmanhpoint.h"
#include "gmanreal.h"
#include "ri.h"

namespace {

using namespace geometrycheck;

template <typename T> void testScaleYieldsTheScaledComponents() {
  const auto h = gman::HPoint<T>(2, 4, 6, 2);
  const auto scaled = h * T(4);
  expectExact(member<T>("HPoint", "operator*(T) scales every component"), scaled, {8, 16, 24, 8});
}

} // namespace

int main() {
  testScaleYieldsTheScaledComponents<RtFloat>();
  testScaleYieldsTheScaledComponents<gman::PartnerReal>();
  return checkSummary("hpointscale: all checks passed");
}
