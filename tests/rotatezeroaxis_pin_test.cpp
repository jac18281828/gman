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
 * Pins the wanted behaviour: RiRotate about a zero axis, finite RIB input,
 * leaves the current transformation with every entry finite. The request hands
 * GMANMatrix4::rot a zero axis, which divides by the axis's zero length, so
 * every entry becomes NaN and the renderers then reject each point it
 * transforms. A math primitive follows IEEE and answers NaN; the guard belongs
 * to the request. RiTransformPoints is a stub that returns null, so the test
 * reads the current transformation itself. WILL_FAIL until RiRotate refuses a
 * zero axis or answers a finite matrix for one.
 */

#include <cmath>

#include "check.h"
#include "gmanmatrix4.h"
#include "gmanrendermanimpl.h"
#include "ri.h"

namespace {

class TransformReadingRenderMan : public GMANRenderManImpl {
public:
  GMANMatrix4 current() { return getTransform().interpolate(0.0); }
};

bool everyEntryFinite(GMANMatrix4 const& matrix) {
  for (int row = 0; row < 4; ++row) {
    for (int column = 0; column < 4; ++column) {
      if (!std::isfinite(matrix[row][column])) {
        return false;
      }
    }
  }
  return true;
}

} // namespace

int main() {
  TransformReadingRenderMan control;
  control.RiRotate(90.0f, 0.0f, 0.0f, 1.0f);
  check(std::fabs(control.current()[0][1]) > 0.5f, "a rotation about a nonzero axis changes the transformation");

  TransformReadingRenderMan impl;
  impl.RiRotate(90.0f, 0.0f, 0.0f, 0.0f);
  check(everyEntryFinite(impl.current()), "RiRotate about the zero axis leaves every transformation entry finite");
  return checkSummary("rotatezeroaxis: all checks passed");
}
