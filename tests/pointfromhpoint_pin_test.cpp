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
 * Pins the wanted behavior: constructing a Point from an HPoint reads the
 * HPoint and leaves it unchanged. Point(HPoint<T>&) calls wToOne() on its
 * argument, so the HPoint comes back with x, y and z divided by w and w equal
 * to 1. WILL_FAIL until the constructor takes the HPoint by const reference
 * or divides a copy.
 */

#include <string>

#include "check.h"
#include "geometrycheck.h"
#include "gmanhpoint.h"
#include "gmanpoint.h"
#include "gmanreal.h"
#include "ri.h"

namespace {

using namespace geometrycheck;

template <typename T> void testPointFromHPointLeavesItsArgument() {
  auto h = gman::HPoint<T>(2, 4, 6, 2);
  static_cast<void>(gman::Point<T>(h));
  expectExact(member<T>("Point", "Point(HPoint&) leaves its argument unchanged"), h, {2, 4, 6, 2});
}

} // namespace

int main() {
  testPointFromHPointLeavesItsArgument<RtFloat>();
  testPointFromHPointLeavesItsArgument<gman::PartnerReal>();
  return checkSummary("pointfromhpoint: all checks passed");
}
