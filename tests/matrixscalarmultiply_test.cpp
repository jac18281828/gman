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
 * GMANMatrix4::operator*=(RtFloat) must scale every one of the matrix's 16
 * entries, not just column 0. GMANMovingMatrix::interpolate blends two time
 * samples through that same scalar multiply, so a second check pins the
 * blend at a fraction whose two weights differ -- at the exact midpoint the
 * weights are numerically equal and a swapped-weight defect would pass by
 * coincidence.
 */

#include <cmath>
#include <vector>

#include "check.h"
#include "gmanmatrix4.h"
#include "gmantransform.h"

namespace {

bool near(RtFloat a, RtFloat b, RtFloat tol = 1e-4) { return std::fabs(a - b) <= tol; }

void testScalarMultiplyScalesEveryEntry() {
  RtMatrix cells = {{1.0, 2.0, 3.0, 4.0}, {5.0, 6.0, 7.0, 8.0}, {9.0, 10.0, 11.0, 12.0}, {13.0, 14.0, 15.0, 16.0}};
  GMANMatrix4 m(cells);
  const RtFloat scalar = 3.5;

  GMANMatrix4 result = m * scalar;

  bool allScaled = true;
  for (RtInt i = 0; i < 4; i++) {
    for (RtInt j = 0; j < 4; j++) {
      if (!near(result[i][j], cells[i][j] * scalar)) {
        allScaled = false;
      }
    }
  }
  check(allScaled, "operator*(RtFloat) scales all 16 entries, not just column 0");
}

void testMovingMatrixInterpolateBlendsBothSamples() {
  RtMatrix sample0 = {{1.0, 2.0, 3.0, 4.0}, {5.0, 6.0, 7.0, 8.0}, {9.0, 10.0, 11.0, 12.0}, {13.0, 14.0, 15.0, 16.0}};
  RtMatrix sample1 = {{101.0, 102.0, 103.0, 104.0},
                      {105.0, 106.0, 107.0, 108.0},
                      {109.0, 110.0, 111.0, 112.0},
                      {113.0, 114.0, 115.0, 116.0}};

  GMANMovingMatrix mm(std::vector<RtFloat>{0.0, 2.0});
  mm.get(0) = GMANMatrix4(sample0);
  mm.get(1) = GMANMatrix4(sample1);

  GMANMatrix4 blended = mm.interpolate(0.5);

  bool blendMatches = true;
  for (RtInt i = 0; i < 4; i++) {
    for (RtInt j = 0; j < 4; j++) {
      const RtFloat expect = sample1[i][j] * 0.25 + sample0[i][j] * 0.75;
      if (!near(blended[i][j], expect)) {
        blendMatches = false;
      }
    }
  }
  check(blendMatches, "interpolate blends sample1*0.25 + sample0*0.75 at time 0.5");
}

} // namespace

int main() {
  testScalarMultiplyScalesEveryEntry();
  testMovingMatrixInterpolateBlendsBothSamples();

  return checkSummary("matrixscalarmultiply holds");
}
