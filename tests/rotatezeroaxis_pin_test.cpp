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
 * Pins the wanted behaviour: Rotate about a zero axis, finite RIB input,
 * builds a matrix with every entry finite. GMANMatrix4::rot normalizes the
 * axis by its length, 0, and every entry becomes NaN; the renderers then
 * reject every point it transforms. WILL_FAIL until rot answers a finite
 * matrix for a zero axis.
 */

#include <cmath>

#include "check.h"
#include "gmanmatrix4.h"
#include "ri.h"

int main() {
  GMANMatrix4 matrix;
  matrix.rot(90.0f, 0.0f, 0.0f, 0.0f);
  bool finite = true;
  for (int row = 0; row < 4; ++row) {
    for (int column = 0; column < 4; ++column) {
      finite = finite && std::isfinite(matrix[row][column]);
    }
  }
  check(finite, "a rotation about the zero axis builds a matrix with every entry finite");
  return checkSummary("rotatezeroaxis: all checks passed");
}
