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
 * RiOptionV's own handling of Option "radiosity" "float elementsize":
 * unset reads 0, a later Option replaces an earlier one, a non-finite or
 * non-positive value leaves the stored size unchanged, and an Option
 * naming neither "radiosity" nor "elementsize" changes nothing -- Option
 * "render" "string indirect" included.
 */

#include <cmath>
#include <limits>

#include "check.h"
#include "gmanrendermanimpl.h"
#include "ri.h"

namespace {

// GMANGraphicState's getOptions() reaches GMANRenderManImpl only
// protected; a subclass reaches it, as tests/ribstringescape_test.cpp's
// own subclass reaches an overridden Ri call, and re-exposes it here for
// this file's own checks.
class OptionReadingRenderMan : public GMANRenderManImpl {
public:
  GMANOptions& options() { return getOptions(); }
};

void setElementSize(OptionReadingRenderMan& impl, RtFloat value) {
  RtToken tokens[1] = {"float elementsize"};
  RtPointer parms[1] = {&value};
  impl.RiOptionV("radiosity", 1, tokens, parms);
}

void setIndirectPassName(OptionReadingRenderMan& impl, char const* name) {
  char* value = const_cast<char*>(name);
  RtToken tokens[1] = {"string indirect"};
  RtPointer parms[1] = {&value};
  impl.RiOptionV("render", 1, tokens, parms);
}

// ---- check 1: unset reads 0 ----
void testUnsetReadsZero() {
  OptionReadingRenderMan impl;
  check(impl.options().getRadiosityElementSize() == 0.0f, "unset: getRadiosityElementSize() reads 0");
}

// ---- check 2: a value is read back, and a later Option replaces it ----
void testSetAndReplace() {
  OptionReadingRenderMan impl;
  setElementSize(impl, 0.5f);
  check(impl.options().getRadiosityElementSize() == 0.5f, "set: 0.5 reads back exactly");

  setElementSize(impl, 0.25f);
  check(impl.options().getRadiosityElementSize() == 0.25f, "replace: a second Option with 0.25 replaces 0.5");
}

// ---- check 3: 0, a negative value, NaN and infinity each leave the
// stored size unchanged ----
void testInvalidValuesIgnored() {
  OptionReadingRenderMan impl;
  setElementSize(impl, 0.4f);
  check(impl.options().getRadiosityElementSize() == 0.4f, "invalid setup: 0.4 reads back exactly");

  setElementSize(impl, 0.0f);
  check(impl.options().getRadiosityElementSize() == 0.4f, "invalid: 0 leaves the value at 0.4");

  setElementSize(impl, -1.0f);
  check(impl.options().getRadiosityElementSize() == 0.4f, "invalid: -1 leaves the value at 0.4");

  setElementSize(impl, std::numeric_limits<RtFloat>::quiet_NaN());
  check(impl.options().getRadiosityElementSize() == 0.4f, "invalid: NaN leaves the value at 0.4");

  setElementSize(impl, std::numeric_limits<RtFloat>::infinity());
  check(impl.options().getRadiosityElementSize() == 0.4f, "invalid: infinity leaves the value at 0.4");
}

// ---- check 4: an Option naming neither "radiosity" nor "elementsize"
// changes nothing ----
void testUnrelatedOptionsIgnored() {
  OptionReadingRenderMan impl;
  setElementSize(impl, 0.3f);

  // Another name entirely.
  RtFloat other = 9.0f;
  RtToken hiderTokens[1] = {"string type"};
  RtPointer hiderParms[1] = {&other};
  impl.RiOptionV("hider", 1, hiderTokens, hiderParms);
  check(impl.options().getRadiosityElementSize() == 0.3f, "unrelated name: \"hider\" leaves the value at 0.3");

  // "radiosity" carrying another token.
  RtFloat otherSize = 9.0f;
  RtToken otherTokens[1] = {"float othersize"};
  RtPointer otherParms[1] = {&otherSize};
  impl.RiOptionV("radiosity", 1, otherTokens, otherParms);
  check(impl.options().getRadiosityElementSize() == 0.3f,
        "unrelated token: \"radiosity\" \"float othersize\" leaves the value at 0.3");
}

// ---- check 5: an Option "render" "string indirect" set before it still
// reads back ----
void testIndirectPassOptionUnaffected() {
  OptionReadingRenderMan impl;
  setIndirectPassName(impl, "radiosity");
  check(impl.options().getIndirectPass() == "radiosity", "indirect setup: the pass name reads back");

  setElementSize(impl, 0.6f);
  check(impl.options().getIndirectPass() == "radiosity",
        "indirect: Option \"radiosity\" \"float elementsize\" leaves the pass name unchanged");
  check(impl.options().getRadiosityElementSize() == 0.6f, "indirect: the element size itself still reads back after");
}

} // namespace

int main() {
  testUnsetReadsZero();
  testSetAndReplace();
  testInvalidValuesIgnored();
  testUnrelatedOptionsIgnored();
  testIndirectPassOptionUnaffected();

  return checkSummary("Option \"radiosity\" \"float elementsize\": unset reads 0, a later Option replaces an "
                      "earlier one, a non-finite or non-positive value leaves it unchanged, and an unrelated "
                      "Option -- \"render\" \"string indirect\" included -- changes nothing");
}
