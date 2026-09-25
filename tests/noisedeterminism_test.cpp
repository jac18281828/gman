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
 * gman::Noise seeds its own std::mt19937 rather than the process-global C
 * rand(). Two independently constructed instances agree at the same fixed
 * inputs, across both noise and cellnoise, proving each instance's sequence
 * is deterministic and instance-independent. cellnoise's fixed-value
 * constants, recorded from a real build, pin its cross-platform
 * determinism. rand()'s own sequence is left unperturbed by constructing
 * or exercising a Noise.
 */

#include <cmath>
#include <cstdlib>

#include "check.h"
#include "gmannoise.h"
#include "gmanpoint.h"

namespace {

bool near(RtFloat a, RtFloat b, RtFloat tol = 1e-6) { return std::fabs(a - b) <= tol; }

void testTwoInstancesAgree() {
  gman::Noise a;
  gman::Noise b;

  GMANPoint const p1(0.25, 0.5, 0.75);
  GMANPoint const p2(3.0, -1.5, 2.25);
  RtFloat const v(1.75);

  check(near(a.noise(p1), b.noise(p1)), "noise: two instances agree at the same GMANPoint input");
  check(near(a.noise(p2), b.noise(p2)), "noise: two instances agree at a second GMANPoint input");
  check(near(a.noise(v), b.noise(v)), "noise: two instances agree at the same scalar input");
  check(near(a.cellnoise(p1), b.cellnoise(p1)), "cellnoise: two instances agree at the same GMANPoint input");
  check(near(a.cellnoise(p2), b.cellnoise(p2)), "cellnoise: two instances agree at a second GMANPoint input");
  check(near(a.cellnoise(v), b.cellnoise(v)), "cellnoise: two instances agree at the same scalar input");
}

// Recorded from a real build (this file's own header comment): a
// regression here means the seeded draw sequence changed, not that two
// instances disagree with each other.
//
// cellnoise only, not noise: cellnoise looks its cell's value up directly
// from the seeded table, while noise interpolates through a gradient dot
// product whose exact rounding is not guaranteed identical across
// compilers -- testTwoInstancesAgree already covers noise's own
// determinism within one build.
void testFixedValues() {
  gman::Noise n;

  GMANPoint const p1(0.25, 0.5, 0.75);
  GMANPoint const p2(3.0, -1.5, 2.25);
  RtFloat const v(1.75);

  check(near(n.cellnoise(p1), (RtFloat)0.0529898629), "cellnoise(p1) matches its recorded value");
  check(near(n.cellnoise(p2), (RtFloat)0.383108139), "cellnoise(p2) matches its recorded value");
  check(near(n.cellnoise(v), (RtFloat)0.750981987), "cellnoise(v) matches its recorded value");
}

void testRandUnperturbed() {
  constexpr unsigned kSeed = 12345;

  // The expected draw: a fresh seeded sequence's first value, with no
  // Noise construction or use in between.
  std::srand(kSeed);
  int const expected = std::rand();

  // The draw under test: reseed the same way, construct and exercise a
  // Noise, then draw without reseeding. If Noise touches rand()'s own
  // sequence, this draw diverges from expected.
  std::srand(kSeed);
  gman::Noise n;
  GMANPoint const p(1.0, 2.0, 3.0);
  (void)n.noise(p);
  (void)n.cellnoise(p);
  int const actual = std::rand();

  check(expected == actual, "constructing and exercising a Noise leaves rand()'s own sequence unperturbed");
}

} // namespace

int main() {
  testTwoInstancesAgree();
  testFixedValues();
  testRandUnperturbed();
  return checkSummary("noisedeterminism: ok");
}
