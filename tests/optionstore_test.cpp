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
 * The option store: RiOptionV keeps every Option parameter the typed
 * handlers do not read, and GMANOptions::getNamedOption reads it back by
 * Option name and parameter identifier. Covers the round trip of each
 * storage type, absence, replace and accumulate, ownership of the copied
 * values, the three consumed pairs staying out while every other pair under
 * a handled name goes in, the silent drop of a token the dictionary cannot
 * resolve, and the frame's push and pop.
 */

#include <algorithm>
#include <memory>
#include <string>
#include <variant>
#include <vector>

#include "check.h"
#include "gmanrendermanimpl.h"
#include "ri.h"

namespace {

// GMANGraphicState's getOptions() reaches GMANRenderManImpl only
// protected; a subclass reaches it, as pathtraceroption_test.cpp's does.
class OptionReadingRenderMan : public GMANRenderManImpl {
public:
  GMANOptions& options() { return getOptions(); }
};

template <typename T>
std::vector<T> const* readAs(GMANOptions const& options, std::string const& option, std::string const& parameter) {
  GMANOptions::NamedOptionValue const* const value = options.getNamedOption(option, parameter);
  if (value == nullptr) {
    return nullptr;
  }
  return std::get_if<std::vector<T>>(value);
}

bool readsFloats(GMANOptions const& options, std::string const& option, std::string const& parameter,
                 std::vector<RtFloat> const& expected) {
  auto const* const value = readAs<RtFloat>(options, option, parameter);
  return value != nullptr && *value == expected;
}

bool readsInts(GMANOptions const& options, std::string const& option, std::string const& parameter,
               std::vector<RtInt> const& expected) {
  auto const* const value = readAs<RtInt>(options, option, parameter);
  return value != nullptr && *value == expected;
}

bool readsStrings(GMANOptions const& options, std::string const& option, std::string const& parameter,
                  std::vector<std::string> const& expected) {
  auto const* const value = readAs<std::string>(options, option, parameter);
  return value != nullptr && *value == expected;
}

bool readsNothing(GMANOptions const& options, std::string const& option, std::string const& parameter) {
  return options.getNamedOption(option, parameter) == nullptr;
}

void setFloat(OptionReadingRenderMan& impl, RtToken option, RtToken token, RtFloat value) {
  RtToken tokens[1] = {token};
  RtPointer parms[1] = {&value};
  impl.RiOptionV(option, 1, tokens, parms);
}

void setInt(OptionReadingRenderMan& impl, RtToken option, RtToken token, RtInt value) {
  RtToken tokens[1] = {token};
  RtPointer parms[1] = {&value};
  impl.RiOptionV(option, 1, tokens, parms);
}

void setString(OptionReadingRenderMan& impl, RtToken option, RtToken token, char const* text) {
  char* value = const_cast<char*>(text);
  RtToken tokens[1] = {token};
  RtPointer parms[1] = {&value};
  impl.RiOptionV(option, 1, tokens, parms);
}

// A float, an integer array of two and a string, each declared inline under
// "limits", read back with their own alternative, exact length and values.
void testRoundTripThreeTypes() {
  OptionReadingRenderMan impl;
  RtFloat gain = 1.5f;
  RtInt counts[2] = {7, 9};
  char* label = const_cast<char*>("hello");
  RtToken tokens[3] = {"float gain", "integer[2] counts", "string label"};
  RtPointer parms[3] = {&gain, counts, &label};
  impl.RiOptionV("limits", 3, tokens, parms);

  GMANOptions const& options = impl.options();
  check(readsFloats(options, "limits", "gain", {1.5f}), "round trip: a float reads back as one float");
  check(readsInts(options, "limits", "counts", {7, 9}), "round trip: an integer[2] reads back both ints in order");
  check(readsStrings(options, "limits", "label", {"hello"}), "round trip: a string reads back as one string");
  check(readAs<RtInt>(options, "limits", "gain") == nullptr && readAs<RtFloat>(options, "limits", "counts") == nullptr,
        "round trip: each value holds only its own alternative");
}

// An unset Option name reads null; a set Option with an unset parameter reads
// null.
void testAbsence() {
  OptionReadingRenderMan impl;
  check(readsNothing(impl.options(), "limits", "gain"), "absence: an unset Option name reads null");

  setFloat(impl, "limits", "float gain", 1.5f);
  check(readsNothing(impl.options(), "limits", "other"), "absence: a set Option with an unset parameter reads null");
  check(readsNothing(impl.options(), "elsewhere", "gain"), "absence: a different Option name reads null");
}

// A second RiOptionV for the same Option and parameter replaces the value; a
// different parameter accumulates beside it; another Option keeps its own; a
// later float replaces an earlier integer of the same identifier.
void testReplaceAndAccumulate() {
  OptionReadingRenderMan impl;
  setFloat(impl, "limits", "float gain", 1.5f);
  setFloat(impl, "limits", "float gain", 2.5f);
  check(readsFloats(impl.options(), "limits", "gain", {2.5f}), "replace: a second value replaces the first");

  setInt(impl, "limits", "integer depth", 4);
  check(readsFloats(impl.options(), "limits", "gain", {2.5f}) && readsInts(impl.options(), "limits", "depth", {4}),
        "accumulate: a different parameter leaves the first readable");

  setFloat(impl, "other", "float gain", 9.0f);
  check(readsFloats(impl.options(), "limits", "gain", {2.5f}) && readsFloats(impl.options(), "other", "gain", {9.0f}),
        "accumulate: another Option name keeps its own value");

  setInt(impl, "limits", "integer x", 3);
  setFloat(impl, "limits", "float x", 0.5f);
  check(readsFloats(impl.options(), "limits", "x", {0.5f}) && readAs<RtInt>(impl.options(), "limits", "x") == nullptr,
        "replace: float x after integer x under one Option reads the float");
}

// The caller's arrays are overwritten and its string destroyed after
// RiOptionV returns; the stored values read back unchanged.
void testOwning() {
  OptionReadingRenderMan impl;
  std::vector<RtFloat> floats = {1.0f, 2.0f, 3.0f};
  std::vector<RtInt> ints = {4, 5};
  auto text = std::make_unique<std::string>("gather");
  char* textPointer = text->data();
  RtToken tokens[3] = {"float[3] weights", "integer[2] counts", "string mode"};
  RtPointer parms[3] = {floats.data(), ints.data(), &textPointer};
  impl.RiOptionV("limits", 3, tokens, parms);

  std::fill(floats.begin(), floats.end(), -1.0f);
  std::fill(ints.begin(), ints.end(), -1);
  std::fill(text->begin(), text->end(), 'X');
  text.reset();

  check(readsFloats(impl.options(), "limits", "weights", {1.0f, 2.0f, 3.0f}),
        "owning: the float array survives the caller overwriting it");
  check(readsInts(impl.options(), "limits", "counts", {4, 5}),
        "owning: the int array survives the caller overwriting it");
  check(readsStrings(impl.options(), "limits", "mode", {"gather"}),
        "owning: the string survives the caller destroying it");
}

// The pairs the typed handlers read stay out of the store, whatever type the
// token declares, and still read through their typed accessors.
void testConsumedPairsStayOut() {
  OptionReadingRenderMan impl;
  setInt(impl, "pathtracer", "integer samples", 16);
  setFloat(impl, "radiosity", "float elementsize", 0.25f);
  setString(impl, "render", "string indirect", "radiosity");

  GMANOptions const& options = impl.options();
  check(readsNothing(options, "pathtracer", "samples"), "consumed: \"pathtracer\" \"integer samples\" is not stored");
  check(readsNothing(options, "radiosity", "elementsize"),
        "consumed: \"radiosity\" \"float elementsize\" is not stored");
  check(readsNothing(options, "render", "indirect"), "consumed: \"render\" \"string indirect\" is not stored");
  check(options.getPathtracerSamples() == 16, "consumed: the typed samples accessor reads 16");
  check(options.getRadiosityElementSize() == 0.25f, "consumed: the typed element size accessor reads 0.25");
  check(options.getIndirectPass() == "radiosity", "consumed: the typed indirect pass accessor reads radiosity");

  setFloat(impl, "pathtracer", "float samples", 8.0f);
  check(readsNothing(options, "pathtracer", "samples"),
        "consumed: \"pathtracer\" \"float samples\" is not stored whatever its declared type");
  check(options.getPathtracerSamples() == 16, "consumed: a float samples token leaves the typed count at 16");
}

// Every other pair under a handled name is stored and leaves the typed
// values unchanged.
void testOtherPairsUnderHandledNamesGoIn() {
  OptionReadingRenderMan impl;
  setInt(impl, "pathtracer", "integer samples", 16);
  setFloat(impl, "radiosity", "float elementsize", 0.25f);

  setString(impl, "pathtracer", "string indirect", "gather");
  check(readsStrings(impl.options(), "pathtracer", "indirect", {"gather"}),
        "handled names: \"pathtracer\" \"string indirect\" reads back gather");
  check(impl.options().getPathtracerSamples() == 16, "handled names: the typed samples count stays 16");

  setFloat(impl, "radiosity", "float othersize", 0.75f);
  check(readsFloats(impl.options(), "radiosity", "othersize", {0.75f}),
        "handled names: \"radiosity\" \"float othersize\" reads back its float");
  check(impl.options().getRadiosityElementSize() == 0.25f, "handled names: the typed element size stays 0.25");

  setFloat(impl, "render", "float tolerance", 0.125f);
  check(readsFloats(impl.options(), "render", "tolerance", {0.125f}),
        "handled names: \"render\" \"float tolerance\" reads back its float");
}

// An undeclared bare token beside a declared one drops silently: the
// undeclared reads null, the declared one reads back and the error handler
// sees nothing.
void testUnresolvablePairsDrop() {
  OptionReadingRenderMan impl;
  impl.RiErrorHandler(RiErrorIgnore);
  RiLastError = RIE_NOERROR;

  RtInt bucket[2] = {32, 32};
  RtFloat gain = 1.5f;
  RtToken tokens[2] = {"bucketsize", "float gain"};
  RtPointer parms[2] = {bucket, &gain};
  impl.RiOptionV("limits", 2, tokens, parms);

  check(readsNothing(impl.options(), "limits", "bucketsize"), "unresolvable: the undeclared token reads null");
  check(readsFloats(impl.options(), "limits", "gain", {1.5f}), "unresolvable: the declared token beside it reads back");
  check(RiLastError == RIE_NOERROR, "unresolvable: the call raises no error");

  impl.RiErrorHandler(RiErrorPrint);
}

// Options set outside a frame read inside it; a replacement and a new
// parameter inside the frame read their inner values; after RiFrameEnd the
// outer values read back and the inner-only parameter reads null.
void testFramePushAndPop() {
  OptionReadingRenderMan impl;
  setFloat(impl, "limits", "float gain", 1.5f);
  setInt(impl, "limits", "integer depth", 4);

  impl.RiFrameBegin(1);
  check(readsFloats(impl.options(), "limits", "gain", {1.5f}) && readsInts(impl.options(), "limits", "depth", {4}),
        "frame: the outer values read inside the frame");

  setFloat(impl, "limits", "float gain", 2.5f);
  setString(impl, "limits", "string inner", "frame");
  check(readsFloats(impl.options(), "limits", "gain", {2.5f}),
        "frame: a replacement inside the frame reads its inner value");
  check(readsStrings(impl.options(), "limits", "inner", {"frame"}),
        "frame: a parameter added inside the frame reads back");
  check(readsInts(impl.options(), "limits", "depth", {4}),
        "frame: an untouched outer value still reads inside the frame");
  impl.RiFrameEnd();

  check(readsFloats(impl.options(), "limits", "gain", {1.5f}), "frame: the outer value reads back after RiFrameEnd");
  check(readsInts(impl.options(), "limits", "depth", {4}), "frame: the untouched value reads back after RiFrameEnd");
  check(readsNothing(impl.options(), "limits", "inner"), "frame: the inner-only parameter reads null after RiFrameEnd");
}

} // namespace

int main() {
  testRoundTripThreeTypes();
  testAbsence();
  testReplaceAndAccumulate();
  testOwning();
  testConsumedPairsStayOut();
  testOtherPairsUnderHandledNamesGoIn();
  testUnresolvablePairsDrop();
  testFramePushAndPop();

  return checkSummary("Option store: each storage type round-trips by Option name and parameter identifier, absent "
                      "keys read null, a later value replaces and another parameter accumulates, values are owned, "
                      "the three pairs the typed handlers read stay out while every other pair under a handled "
                      "name goes in, an unresolvable token drops silently, and a frame's Options end with it");
}
