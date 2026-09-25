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
 * GMANBBox's two-point constructor and its getMin()/getMax() accessors
 * -- the min/max prerequisite a BVH needs.
 */

#include "check.h"
#include "gmanbbox.h"
#include "gmanpoint.h"
#include "ri.h"

namespace {

// ---- check 1: the two-point constructor round trips the points given ----
void testTwoPointConstructorRoundTrips() {
  GMANPoint const min(-1.0, -2.0, -3.0);
  GMANPoint const max(4.0, 5.0, 6.0);
  GMANBBox const box(min, max);

  GMANPoint const gotMin = box.getMin();
  GMANPoint const gotMax = box.getMax();
  check(gotMin.getX() == min.getX() && gotMin.getY() == min.getY() && gotMin.getZ() == min.getZ(),
        "two-point ctor: getMin() == the point given as min");
  check(gotMax.getX() == max.getX() && gotMax.getY() == max.getY() && gotMax.getZ() == max.getZ(),
        "two-point ctor: getMax() == the point given as max");
}

// ---- check 2: a default-constructed box spans -RI_INFINITY/RI_INFINITY
// on every axis ----
void testDefaultBoxIsUnbounded() {
  GMANBBox const box;
  GMANPoint const gotMin = box.getMin();
  GMANPoint const gotMax = box.getMax();

  check(gotMin.getX() == -RI_INFINITY && gotMin.getY() == -RI_INFINITY && gotMin.getZ() == -RI_INFINITY,
        "default ctor: getMin() == -RI_INFINITY on every axis");
  check(gotMax.getX() == RI_INFINITY && gotMax.getY() == RI_INFINITY && gotMax.getZ() == RI_INFINITY,
        "default ctor: getMax() == RI_INFINITY on every axis");
}

} // namespace

int main() {
  testTwoPointConstructorRoundTrips();
  testDefaultBoxIsUnbounded();

  return checkSummary("GMANBBox: the two-point constructor and its getMin()/getMax() accessors");
}
