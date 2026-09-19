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
 * gman::sampleCentre answers the raster coordinate of one sample's
 * centre; a renderer's per-sample loop calls it once per axis. The
 * deleted (T, U, V) overload is pinned separately, at compile time, since
 * a wrong-typed call is a build failure, not a runtime one.
 */

#include <cmath>
#include <cstdint>

#include "check.h"
#include "gmansamplebuffer.h"

namespace {

constexpr RtFloat kTolerance = 1e-6f;

// A requires-expression never calls the function it names, so an
// argument list that resolves to the deleted overload still compiles
// here -- it fails only where the call itself is emitted -- which is
// what makes this concept safe to instantiate on either overload.
template <class T, class U, class V>
concept SampleCentreCallable = requires(T origin, U s, V samplesPerPixel) {
  { gman::sampleCentre(origin, s, samplesPerPixel) } -> std::same_as<RtFloat>;
};

static_assert(SampleCentreCallable<int, int, int>,
              "gman::sampleCentre(int, int, int) resolves to the constexpr overload");
static_assert(!SampleCentreCallable<double, int, int>,
              "a double origin resolves to the deleted overload, not a narrowing conversion");
static_assert(!SampleCentreCallable<int, int64_t, int>,
              "an int64_t sample index resolves to the deleted overload, not a narrowing conversion");
static_assert(!SampleCentreCallable<int, int, double>,
              "a double samplesPerPixel resolves to the deleted overload, not a narrowing conversion");

} // namespace

int main() {
  check(std::fabs(gman::sampleCentre(0, 0, 2) - 0.25f) <= kTolerance,
        "sampleCentre(0, 0, 2): the first of two samples centres at 0.25");
  check(std::fabs(gman::sampleCentre(10, 3, 2) - 11.75f) <= kTolerance,
        "sampleCentre(10, 3, 2): sample 3 (the second pixel's second sample) centres at 11.75");

  return checkSummary("gman::sampleCentre pins one sample's raster centre, int-only");
}
