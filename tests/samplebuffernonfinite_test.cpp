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
 * GMANSampleBuffer resolves one sample per pixel through a box filter, so a
 * pixel is the sample as written. Its alpha reads a NaN as 1, +inf as 1 and
 * -inf as 0, never the transparent a NaN clamped to 0 reads as, and passes a
 * finite coverage unchanged.
 */

#include <cmath>
#include <limits>
#include <string>

#include "check.h"
#include "gmancolor.h"
#include "gmanframebuffer.h"
#include "gmansamplebuffer.h"
#include "ri.h"

namespace {

constexpr RtFloat kNaN = std::numeric_limits<RtFloat>::quiet_NaN();
constexpr RtFloat kInfinity = std::numeric_limits<RtFloat>::infinity();

GMANColor const kBackground(0.0f, 0.0f, 0.0f);
GMANColor const kGrey(0.5f, 0.5f, 0.5f);

struct Resolved {
  GMANColor color;
  GMANAlpha alpha;
};

Resolved resolveOne(GMANColor const& color, GMANAlpha const& alpha) {
  GMANSampleBuffer buffer(1, 1, 1, 1, kBackground);
  buffer.zTestAndSet(0, 0, 1.0f, color, alpha);
  GMANFrameBuffer frame(1, 1, kBackground);
  buffer.resolve(&frame, RiBoxFilter, 1.0f, 1.0f);
  return {frame.getPixel(0, 0), frame.getAlpha(0, 0)};
}

bool alphaIs(GMANAlpha const& alpha, RtFloat red, RtFloat green, RtFloat blue) {
  return alpha.getRed() == red && alpha.getGreen() == green && alpha.getBlue() == blue;
}

std::string describe(GMANAlpha const& alpha) {
  return "(" + std::to_string(alpha.getRed()) + ", " + std::to_string(alpha.getGreen()) + ", " +
         std::to_string(alpha.getBlue()) + ")";
}

void testNonFiniteCoverage() {
  GMANAlpha const nan = resolveOne(kGrey, GMANAlpha(kNaN, kNaN, kNaN)).alpha;
  check(alphaIs(nan, 1.0f, 1.0f, 1.0f), "a NaN coverage resolves to 1 in every channel, got " + describe(nan));
  GMANAlpha const positive = resolveOne(kGrey, GMANAlpha(kInfinity, kInfinity, kInfinity)).alpha;
  check(alphaIs(positive, 1.0f, 1.0f, 1.0f), "an infinite coverage resolves to 1, got " + describe(positive));
  GMANAlpha const negative = resolveOne(kGrey, GMANAlpha(-kInfinity, -kInfinity, -kInfinity)).alpha;
  check(alphaIs(negative, 0.0f, 0.0f, 0.0f), "a negative infinite coverage resolves to 0, got " + describe(negative));
  GMANAlpha const mixed = resolveOne(kGrey, GMANAlpha(kNaN, 0.5f, -kInfinity)).alpha;
  check(alphaIs(mixed, 1.0f, 0.5f, 0.0f), "coverage (NaN, 0.5, -inf) resolves to (1, 0.5, 0), got " + describe(mixed));
}

void testFiniteCoverageIsClampedAsBefore() {
  GMANAlpha const inRange = resolveOne(kGrey, GMANAlpha(0.25f, 0.5f, 1.0f)).alpha;
  check(alphaIs(inRange, 0.25f, 0.5f, 1.0f), "a coverage in [0, 1] passes through, got " + describe(inRange));
  GMANAlpha const outOfRange = resolveOne(kGrey, GMANAlpha(2.0f, -1.0f, 0.5f)).alpha;
  check(alphaIs(outOfRange, 1.0f, 0.0f, 0.5f), "a finite coverage clamps to [0, 1], got " + describe(outOfRange));
}

} // namespace

int main() {
  testNonFiniteCoverage();
  testFiniteCoverageIsClampedAsBefore();
  return checkSummary("samplebuffernonfinite: all checks passed");
}
