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
 * GMANBlobby's constructor each of its four counts. With supplied lengths, the
 * constructor stops at an array one value short of its declaration with
 * RIE_CONSISTENCY, accepts one of exactly the declared length, and keeps the
 * first declared-length values of a longer one with a warning; with none it
 * trusts the caller's arrays at their declared length.
 */

#include <climits>
#include <cstdio>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>

#include "check.h"
#include "gmandictionary.h"
#include "gmanerror.h"
#include "gmanlog.h"
#include "gmanparameterlist.h"
#include "gmanprimitives.h"

namespace {

// Runs fn with warnings captured to a fresh file at path, as
// tests/polygonnverts_test.cpp's own captureLog does, and returns the file's
// contents.
template <typename Fn> std::string captureLog(std::string const& path, Fn&& fn) {
  std::remove(path.c_str());
  setLogFile(path.c_str());
  setScreenOutput(false);

  fn();

  setLogFile("/dev/null");
  setScreenOutput(true);

  std::ifstream in(path, std::ios::binary);
  std::ostringstream contents;
  contents << in.rdbuf();
  return contents.str();
}

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

// P at vertex=2 declares six floats.
void checkSuppliedLengths() {
  GMANDictionary dictionary;
  const auto pointId = dictionary.getTokenId(std::string(RI_P));
  RtFloat points[7] = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f};
  RtToken tokens[1] = {RI_P};
  RtPointer parms[1] = {points};
  const RtInt oneShort[1] = {5};
  const RtInt exact[1] = {6};
  const RtInt longer[1] = {7};

  expectError([&] { GMANParameterList list(dictionary, 1, tokens, parms, 2, 1, 1, 1, oneShort); }, RIE_CONSISTENCY,
              "a supplied length one value short");

  const GMANParameterList exactList(dictionary, 1, tokens, parms, 2, 1, 1, 1, exact);
  check(static_cast<RtFloat*>(exactList.getPointer(pointId)) != nullptr,
        "a supplied length equal to the declared one constructs");
  check(exactList.floats("P").size() == 6, "an exact entry holds the declared length");

  std::string log;
  std::optional<GMANParameterList> longList;
  log = captureLog("parametercountbounds.log",
                   [&] { longList.emplace(dictionary, 1, tokens, parms, 2, 1, 1, 1, longer); });
  check(longList.has_value(), "a longer supplied length constructs");
  check(longList->floats("P").size() == 6, "a longer entry holds the declared length");
  check(longList->floats("P")[5] == 6.0f, "a longer entry keeps the first declared-length values");
  check(log.find("truncating the excess") != std::string::npos, "a longer supplied length warns of the truncation");
}

void checkNullCountsTrustTheCaller() {
  GMANDictionary dictionary;
  const auto pointId = dictionary.getTokenId(std::string(RI_P));
  RtFloat points[6] = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f};
  RtToken tokens[1] = {RI_P};
  RtPointer parms[1] = {points};

  const GMANParameterList list(dictionary, 1, tokens, parms, 2, 1, 1, 1, nullptr);
  check(list.getPointer(pointId) != nullptr, "an array of the declared length constructs with no counts");
  check(list.floats("P").size() == 6 && list.floats("P")[5] == 6.0f,
        "the caller's array is read at its declared length");
}

} // namespace

int main() {
  checkAllocSizeBounds();
  checkAllocSizeNegativeCounts();
  checkConstructorNegativeCounts();
  checkBlobbyNegativeCounts();
  checkSuppliedLengths();
  checkNullCountsTrustTheCaller();

  return checkSummary("parameter counts are bounded");
}
