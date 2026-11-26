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
 * -inf as 0, never the transparent a NaN clamped to 0 reads as, and clamps a
 * finite coverage as before. A sample's colour stores a NaN or infinite
 * channel as 0 and a finite channel as it is, and a filter weight that is NaN
 * or infinite counts as 0.
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

std::string describe(GMANColor const& color) {
  return "(" + std::to_string(color.getRed()) + ", " + std::to_string(color.getGreen()) + ", " +
         std::to_string(color.getBlue()) + ")";
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

void testNonFiniteColourIsStoredAsZero() {
  GMANAlpha const opaque(1.0f, 1.0f, 1.0f);
  GMANColor const mixed = resolveOne(GMANColor(kNaN, 0.25f, kInfinity), opaque).color;
  check(mixed.getRed() == 0.0f && mixed.getGreen() == 0.25f && mixed.getBlue() == 0.0f,
        "a sample colour (NaN, 0.25, inf) resolves to (0, 0.25, 0), got " + describe(mixed));
  GMANColor const negative = resolveOne(GMANColor(-kInfinity, kNaN, 1.5f), opaque).color;
  check(negative.getRed() == 0.0f && negative.getGreen() == 0.0f && negative.getBlue() == 1.5f,
        "a sample colour (-inf, NaN, 1.5) resolves to (0, 0, 1.5), got " + describe(negative));
  GMANColor const finite = resolveOne(GMANColor(-0.5f, 2.0f, 0.75f), opaque).color;
  check(finite.getRed() == -0.5f && finite.getGreen() == 2.0f && finite.getBlue() == 0.75f,
        "a finite sample colour, out of [0, 1] included, resolves unchanged, got " + describe(finite));
}

void testNaNDepthNeverWins() {
  GMANSampleBuffer buffer(1, 1, 1, 1, kBackground);
  check(!buffer.zTestAndSet(0, 0, kNaN, kGrey, GMANAlpha(1.0f, 1.0f, 1.0f)), "a NaN depth is never closer");
  check(buffer.zTestAndSet(0, 0, 2.0f, kGrey, GMANAlpha(1.0f, 1.0f, 1.0f)), "a finite depth passes after a NaN one");
}

RtFloat nanOnTheLeft(RtFloat x, RtFloat, RtFloat, RtFloat) { return x < 0.0f ? kNaN : 1.0f; }

RtFloat infinityOnTheLeft(RtFloat x, RtFloat, RtFloat, RtFloat) { return x < 0.0f ? kInfinity : 1.0f; }

GMANColor resolveTwoSamples(RtFilterFunc filter) {
  GMANSampleBuffer buffer(1, 1, 2, 1, kBackground);
  GMANAlpha const opaque(1.0f, 1.0f, 1.0f);
  buffer.zTestAndSet(0, 0, 1.0f, GMANColor(1.0f, 0.0f, 0.0f), opaque);
  buffer.zTestAndSet(1, 0, 1.0f, GMANColor(0.0f, 1.0f, 0.0f), opaque);
  GMANFrameBuffer frame(1, 1, kBackground);
  buffer.resolve(&frame, filter, 2.0f, 2.0f);
  return frame.getPixel(0, 0);
}

void testNonFiniteFilterWeightCountsAsZero() {
  GMANColor const nan = resolveTwoSamples(nanOnTheLeft);
  check(nan.getRed() == 0.0f && nan.getGreen() == 1.0f && nan.getBlue() == 0.0f,
        "a NaN filter weight drops its sample, got " + describe(nan));
  GMANColor const infinite = resolveTwoSamples(infinityOnTheLeft);
  check(infinite.getRed() == 0.0f && infinite.getGreen() == 1.0f && infinite.getBlue() == 0.0f,
        "an infinite filter weight drops its sample, got " + describe(infinite));
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
  testNonFiniteColourIsStoredAsZero();
  testNaNDepthNeverWins();
  testNonFiniteFilterWeightCountsAsZero();
  testFiniteCoverageIsClampedAsBefore();
  return checkSummary("samplebuffernonfinite: all checks passed");
}
