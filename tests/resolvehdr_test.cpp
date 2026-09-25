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
 * GMANSampleBuffer::resolve writes colour as filtered, unclamped, and
 * alpha clamped to [0, 1]. A single sample above 1 comes through bit-equal
 * (no clamp rounds it down), a step edge under a negative-lobe kernel
 * undershoots below 0 and overshoots above 1 in colour, and alpha -- fed
 * the same step -- still lands in [0, 1] exactly.
 */

#include "check.h"
#include "gmanframebuffer.h"
#include "gmansamplebuffer.h"
#include "ri.h"

namespace {

void checkChannels(GMANColor const& c, RtFloat r, RtFloat g, RtFloat b, std::string const& tag) {
  check(c.getRed() == r, tag + ": red bit-equal");
  check(c.getGreen() == g, tag + ": green bit-equal");
  check(c.getBlue() == b, tag + ": blue bit-equal");
}

// A single sample, resolved through a width-1 box filter, passes through
// unclamped: the pixel reads it bit-equal even though every channel
// exceeds 1.
void testSingleSampleUnclamped() {
  GMANSampleBuffer buffer(1, 1, 1, 1, GMANColor(0.0f));
  buffer.zTestAndSet(0, 0, 1.0f, GMANColor(2.5f, 3.0f, 4.0f), GMANAlpha(1.0f, 1.0f, 1.0f));

  GMANFrameBuffer frame(1, 1, GMANColor(0.0f));
  buffer.resolve(&frame, RiBoxFilter, 1.0f, 1.0f);

  checkChannels(frame.getPixel(0, 0), 2.5f, 3.0f, 4.0f, "single sample");
}

// An 8-pixel-wide, 2x1-sample buffer holds a hard step: 0 in sample
// columns 0-7, 1 in columns 8-15. RiCatmullRomFilter's negative lobe
// undershoots the low side and overshoots the high side at the pixels
// straddling the step's support.
void testStepEdgeOvershoots() {
  GMANSampleBuffer buffer(8, 1, 2, 1, GMANColor(0.0f));
  for (int sx = 0; sx < 16; sx++) {
    const RtFloat v = sx < 8 ? 0.0f : 1.0f;
    buffer.zTestAndSet(sx, 0, 1.0f, GMANColor(v, v, v), GMANAlpha(v, v, v));
  }

  GMANFrameBuffer frame(8, 1, GMANColor(0.0f));
  buffer.resolve(&frame, RiCatmullRomFilter, 4.0f, 4.0f);

  GMANColor const& low = frame.getPixel(2, 0);
  check(low.getRed() < 0.0f && low.getGreen() < 0.0f && low.getBlue() < 0.0f,
        "step edge: pixel 2's colour undershoots below 0 on every channel");

  GMANColor const& high = frame.getPixel(5, 0);
  check(high.getRed() > 1.0f && high.getGreen() > 1.0f && high.getBlue() > 1.0f,
        "step edge: pixel 5's colour overshoots above 1 on every channel");

  GMANAlpha const& lowAlpha = frame.getAlpha(2, 0);
  check(lowAlpha.getRed() == 0.0f && lowAlpha.getGreen() == 0.0f && lowAlpha.getBlue() == 0.0f,
        "step edge: pixel 2's alpha reads exactly 0, clamped");

  GMANAlpha const& highAlpha = frame.getAlpha(5, 0);
  check(highAlpha.getRed() == 1.0f && highAlpha.getGreen() == 1.0f && highAlpha.getBlue() == 1.0f,
        "step edge: pixel 5's alpha reads exactly 1, clamped");
}

} // namespace

int main() {
  testSingleSampleUnclamped();
  testStepEdgeOvershoots();

  return checkSummary("GMANSampleBuffer::resolve writes colour unclamped, alpha clamped to [0, 1]");
}
