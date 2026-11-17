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
 * The polygon helpers gmanpolygon.h declares: gman::boundingBoxExtent,
 * gman::floatArray and gman::polygonTexCoords.
 */

#include <cstddef>
#include <utility>
#include <vector>

#include "check.h"
#include "gmanparameterlist.h"
#include "gmanpoint.h"
#include "gmanpolygon.h"
#include "ri.h"

namespace {

constexpr std::size_t kCount = 3;

bool pairIs(std::pair<RtFloat, RtFloat> const& got, RtFloat s, RtFloat t) { return got.first == s && got.second == t; }

// ---- check 1: the extent is the widest of the three axis spans ----
void testExtentIsWidestAxisSpan() {
  std::vector<GMANPoint> const xWidest = {GMANPoint(-2.0, 0.0, 0.0), GMANPoint(6.0, 1.0, 1.0),
                                          GMANPoint(0.0, 3.0, 2.0)};
  check(gman::boundingBoxExtent(xWidest) == 8.0, "extent: x spanning 8 against y 3 and z 2 reads 8");

  std::vector<GMANPoint> const zWidest = {GMANPoint(0.0, 0.0, -5.0), GMANPoint(1.0, 2.0, 4.0),
                                          GMANPoint(3.0, 1.0, 0.0)};
  check(gman::boundingBoxExtent(zWidest) == 9.0, "extent: z spanning 9 against x 3 and y 2 reads 9");
}

// ---- check 2: a ring of identical points has extent 0 ----
void testExtentOfIdenticalPointsIsZero() {
  std::vector<GMANPoint> const ring(4, GMANPoint(1.0, -2.0, 3.0));
  check(gman::boundingBoxExtent(ring) == 0.0, "extent: a ring of identical points reads 0");
}

// ---- check 3: floatArray is null for an absent token and points at the
// stored values for a present one ----
void testFloatArrayLookup() {
  RtFloat p[] = {1, 2, 3, 4, 5, 6, 7, 8, 9};
  RtToken tokens[] = {RI_P};
  RtPointer parms[] = {(RtPointer)p};
  GMANParameterList const pl(gman::standardDictionary(), 1, tokens, parms, kCount, kCount, 1, 1);

  check(gman::floatArray(pl, RI_ST) == nullptr, "floatArray: a token the list lacks reads nullptr");
  RtFloat const* const stored = gman::floatArray(pl, RI_P);
  check(stored != nullptr, "floatArray: a token the list holds reads non-null");
  bool same = stored != nullptr;
  for (std::size_t i = 0; same && i < 9; i++) {
    same = stored[i] == p[i];
  }
  check(same, "floatArray: the pointer reads the stored values");

  GMANParameterList const empty;
  check(gman::floatArray(empty, RI_P) == nullptr, "floatArray: an empty list reads nullptr");
}

// ---- check 4: without "st", "s" or "t", each point's (s, t) is
// (p[3i], p[3i+1]) ----
void testTexCoordsDefaultToPositionXY() {
  RtFloat p[] = {1, 2, 3, 4, 5, 6, 7, 8, 9};
  GMANParameterList const pl;
  auto const coords = gman::polygonTexCoords(pl, p, kCount);

  check(coords.size() == kCount, "texcoords default: one entry per point");
  check(pairIs(coords[0], 1, 2) && pairIs(coords[1], 4, 5) && pairIs(coords[2], 7, 8),
        "texcoords default: (s, t) == (p[3i], p[3i+1])");
}

// ---- check 5: "st" overrides both components ----
void testStOverridesBoth() {
  RtFloat p[] = {1, 2, 3, 4, 5, 6, 7, 8, 9};
  RtFloat st[] = {10, 11, 12, 13, 14, 15};
  RtToken tokens[] = {RI_ST};
  RtPointer parms[] = {(RtPointer)st};
  GMANParameterList const pl(gman::standardDictionary(), 1, tokens, parms, kCount, kCount, 1, 1);
  auto const coords = gman::polygonTexCoords(pl, p, kCount);

  check(pairIs(coords[0], 10, 11) && pairIs(coords[1], 12, 13) && pairIs(coords[2], 14, 15),
        "texcoords st: both components come from \"st\"");
}

// ---- check 6: "s" and "t" each override their own component, after "st" ----
void testSAndTOverrideAfterSt() {
  RtFloat p[] = {1, 2, 3, 4, 5, 6, 7, 8, 9};
  RtFloat st[] = {10, 11, 12, 13, 14, 15};
  RtFloat s[] = {20, 21, 22};
  RtFloat t[] = {30, 31, 32};
  RtToken tokens[] = {RI_ST, RI_S, RI_T};
  RtPointer parms[] = {(RtPointer)st, (RtPointer)s, (RtPointer)t};
  GMANParameterList const pl(gman::standardDictionary(), 3, tokens, parms, kCount, kCount, 1, 1);
  auto const coords = gman::polygonTexCoords(pl, p, kCount);

  check(pairIs(coords[0], 20, 30) && pairIs(coords[1], 21, 31) && pairIs(coords[2], 22, 32),
        "texcoords s and t: each overrides its own component of \"st\"");
}

// ---- check 7: "s" alone leaves t at its default ----
void testSAloneLeavesTDefault() {
  RtFloat p[] = {1, 2, 3, 4, 5, 6, 7, 8, 9};
  RtFloat s[] = {20, 21, 22};
  RtToken tokens[] = {RI_S};
  RtPointer parms[] = {(RtPointer)s};
  GMANParameterList const pl(gman::standardDictionary(), 1, tokens, parms, kCount, kCount, 1, 1);
  auto const coords = gman::polygonTexCoords(pl, p, kCount);

  check(pairIs(coords[0], 20, 2) && pairIs(coords[1], 21, 5) && pairIs(coords[2], 22, 8),
        "texcoords s alone: s comes from \"s\", t stays p[3i+1]");
}

} // namespace

int main() {
  testExtentIsWidestAxisSpan();
  testExtentOfIdenticalPointsIsZero();
  testFloatArrayLookup();
  testTexCoordsDefaultToPositionXY();
  testStOverridesBoth();
  testSAndTOverrideAfterSt();
  testSAloneLeavesTDefault();

  return checkSummary("the polygon helpers: boundingBoxExtent, floatArray and polygonTexCoords");
}
