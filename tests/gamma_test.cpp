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

// The driver ordering -- correct in float, narrow afterwards -- needs no test
// here. Deleting the byte `correct` overload left the drivers nothing to call
// on a GMANColorRGB, so a driver narrowing before correcting stops compiling.
// The compiler enforces the order; a test asserting it would only test the
// compiler. Assertion 4 below pins the semantics that order makes possible.

#include <cmath>

#include "check.h"
#include "gmancolor.h"
#include "gmangamma.h"

namespace {

constexpr RtFloat kTolerance = 1e-5f;

bool near(RtFloat a, RtFloat b) { return std::fabs(a - b) < kTolerance; }

} // namespace

int main() {
  // Identity: gain 1, gamma 1 leaves every channel bit-identical. Exact
  // equality is the claim that no golden image moves.
  {
    GMANColor color(0.1f, 0.5f, 0.9f);
    color = gman::gammaCorrected(color, 1.0f, 1.0f);
    check(color.getRed() == 0.1f && color.getGreen() == 0.5f && color.getBlue() == 0.9f, "identity leaves color");
  }

  // Gamma: gain 1, gamma 2.2 applies pow(v, 1/2.2) per channel.
  {
    GMANColor color(0.1f, 0.5f, 0.9f);
    color = gman::gammaCorrected(color, 1.0f, 2.2f);
    check(near(color.getRed(), std::pow(0.1f, 1.0f / 2.2f)), "gamma corrects red");
    check(near(color.getGreen(), std::pow(0.5f, 1.0f / 2.2f)), "gamma corrects green");
    check(near(color.getBlue(), std::pow(0.9f, 1.0f / 2.2f)), "gamma corrects blue");
  }

  // Gain: gain 2, gamma 1 doubles each channel.
  {
    GMANColor color(0.1f, 0.2f, 0.3f);
    color = gman::gammaCorrected(color, 2.0f, 1.0f);
    check(near(color.getRed(), 0.2f), "gain doubles red");
    check(near(color.getGreen(), 0.4f), "gain doubles green");
    check(near(color.getBlue(), 0.6f), "gain doubles blue");
  }

  // Sub-byte precision: a channel below 1/255 at gamma 2.2 comes back above
  // 1/255. The deleted byte table could never do this -- it indexed a value
  // already narrowed to 0, and GammaTable[0] was 0. This is the case that
  // pins correcting-before-narrowing over correcting-after.
  {
    const RtFloat below = 0.003f;
    check(below < 1.0f / 255.0f, "0.003 is below one byte step");

    GMANColor color(below, below, below);
    color = gman::gammaCorrected(color, 1.0f, 2.2f);
    check(color.getRed() > 1.0f / 255.0f, "sub-byte red lifts above one step");
    check(color.getGreen() > 1.0f / 255.0f, "sub-byte green lifts above one step");
    check(color.getBlue() > 1.0f / 255.0f, "sub-byte blue lifts above one step");
  }

  return checkSummary("gamma correction holds");
}
