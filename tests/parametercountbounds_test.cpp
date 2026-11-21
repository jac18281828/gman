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
 * The counts that size a parameter list, in process. GMANDictionary::allocSize
 * rejects a negative count and an element count RtInt cannot hold with
 * RIE_RANGE, computes without signed overflow, and returns exactly INT_MAX
 * when that is the answer. GMANParameterList's constructor rejects a negative
 * parameter count and a negative supplied length with RIE_RANGE, and
 * GMANBlobby's constructor each of its four counts.
 */

#include <climits>
#include <string>

#include "check.h"
#include "gmandictionary.h"
#include "gmanerror.h"
#include "gmanparameterlist.h"
#include "gmanprimitives.h"

namespace {

template <typename Body> void expectError(Body&& body, RtInt code, std::string const& what) {
  try {
    body();
  } catch (GMANError const& error) {
    check(error.getCode() == code, what + ": throws the expected code");
    check(error.getSeverity() == RIE_ERROR, what + ": throws at RIE_ERROR");
    return;
  }
  check(false, what + ": throws");
}

void checkAllocSizeBounds() {
  GMANDictionary dictionary;
  const auto floatId = dictionary.addToken("wide", GMANTokenEntry::VERTEX, GMANTokenEntry::FLOAT);
  const auto matrixId = dictionary.addToken("transforms", GMANTokenEntry::VERTEX, GMANTokenEntry::MATRIX);
  const auto pointId = dictionary.getTokenId(std::string(RI_P));

  check(dictionary.allocSize(floatId, INT_MAX, 1, 1) == INT_MAX, "a vertex float at INT_MAX vertices is INT_MAX");
  expectError([&] { dictionary.allocSize(pointId, INT_MAX, 1, 1); }, RIE_RANGE, "a point at INT_MAX vertices");
  expectError([&] { dictionary.allocSize(matrixId, INT_MAX, 1, 1); }, RIE_RANGE, "a matrix at INT_MAX vertices");
  check(dictionary.allocSize(pointId, 1000, 1, 1) == 3000, "an in-range count is unchanged");
}

void checkAllocSizeNegativeCounts() {
  GMANDictionary dictionary;
  const auto kaId = dictionary.getTokenId(std::string(RI_KA));

  expectError([&] { dictionary.allocSize(kaId, -1, 1, 1, 1); }, RIE_RANGE, "a negative vertex count");
  expectError([&] { dictionary.allocSize(kaId, 1, -1, 1, 1); }, RIE_RANGE, "a negative varying count");
  expectError([&] { dictionary.allocSize(kaId, 1, 1, -1, 1); }, RIE_RANGE, "a negative uniform count");
  expectError([&] { dictionary.allocSize(kaId, 1, 1, 1, -1); }, RIE_RANGE, "a negative facevarying count");
}

void checkConstructorNegativeCounts() {
  GMANDictionary dictionary;
  RtFloat ka = 1.0f;
  RtFloat point[3] = {0.0f, 0.0f, 0.0f};
  RtToken kaToken[1] = {RI_KA};
  RtPointer kaParm[1] = {&ka};
  RtToken pointToken[1] = {RI_P};
  RtPointer pointParm[1] = {point};

  expectError([&] { GMANParameterList list(dictionary, -1, kaToken, kaParm); }, RIE_RANGE,
              "a negative parameter count");
  expectError([&] { GMANParameterList list(dictionary, 1, pointToken, pointParm, -1); }, RIE_RANGE,
              "a negative vertex count");
  const RtInt negativeSupplied[1] = {-1};
  expectError([&] { GMANParameterList list(dictionary, 1, kaToken, kaParm, 1, 1, 1, 1, negativeSupplied); }, RIE_RANGE,
              "a negative supplied length");
}

void checkBlobbyNegativeCounts() {
  RtInt code[1] = {0};
  RtFloat floats[1] = {0.0f};
  RtString strings[1] = {const_cast<RtString>("")};
  const GMANParameterList none;

  expectError([&] { GMANBlobby(-1, 0, code, 0, floats, 0, strings, none); }, RIE_RANGE, "a negative leaf count");
  expectError([&] { GMANBlobby(0, -1, code, 0, floats, 0, strings, none); }, RIE_RANGE, "a negative code count");
  expectError([&] { GMANBlobby(0, 0, code, -1, floats, 0, strings, none); }, RIE_RANGE, "a negative float count");
  expectError([&] { GMANBlobby(0, 0, code, 0, floats, -1, strings, none); }, RIE_RANGE, "a negative string count");
}

} // namespace

int main() {
  checkAllocSizeBounds();
  checkAllocSizeNegativeCounts();
  checkConstructorNegativeCounts();
  checkBlobbyNegativeCounts();

  return checkSummary("parameter counts are bounded");
}
