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
 * RiOptionV's handling of Option "pathtracer" "integer samples": unset reads
 * 4, a later Option replaces an earlier one, a value below 1 leaves the
 * stored count unchanged but logs a warning, an "integer other" token changes
 * nothing, and Option "render" "string indirect" and Option "radiosity"
 * "float elementsize" leave the samples count unchanged and still read
 * back themselves.
 */

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include "check.h"
#include "gmanlog.h"
#include "gmanrendermanimpl.h"
#include "ri.h"

namespace {

// GMANGraphicState's getOptions() reaches GMANRenderManImpl only
// protected; a subclass reaches it, as radiosityoption_test.cpp's does.
class OptionReadingRenderMan : public GMANRenderManImpl {
public:
  GMANOptions& options() { return getOptions(); }
};

void setSamples(OptionReadingRenderMan& impl, RtInt value) {
  RtToken tokens[1] = {"integer samples"};
  RtPointer parms[1] = {&value};
  impl.RiOptionV("pathtracer", 1, tokens, parms);
}

void setSamplesOther(OptionReadingRenderMan& impl, RtInt value) {
  RtToken tokens[1] = {"integer other"};
  RtPointer parms[1] = {&value};
  impl.RiOptionV("pathtracer", 1, tokens, parms);
}

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

// Unset reads 4.
void testUnsetReadsFour() {
  OptionReadingRenderMan impl;
  check(impl.options().getPathtracerSamples() == 4, "unset: getPathtracerSamples() reads 4");
}

// 64 reads back; a later 16 replaces it.
void testSetAndReplace() {
  OptionReadingRenderMan impl;
  setSamples(impl, 64);
  check(impl.options().getPathtracerSamples() == 64, "set: 64 reads back exactly");

  setSamples(impl, 16);
  check(impl.options().getPathtracerSamples() == 16, "replace: a second Option with 16 replaces 64");
}

// 0 and -3 each leave 16 and each log its own warning naming samples and
// the rejected value itself.
void testInvalidValuesWarnAndLeaveUnchanged() {
  std::string const logPath = "pathtraceroption_invalid.log";
  std::remove(logPath.c_str());
  setLogFile(logPath.c_str());
  setScreenOutput(false);

  OptionReadingRenderMan impl;
  setSamples(impl, 16);
  check(impl.options().getPathtracerSamples() == 16, "invalid setup: 16 reads back exactly");

  setSamples(impl, 0);
  check(impl.options().getPathtracerSamples() == 16, "invalid: 0 leaves the value at 16");

  setSamples(impl, -3);
  check(impl.options().getPathtracerSamples() == 16, "invalid: -3 leaves the value at 16");

  setLogFile("/dev/null");
  setScreenOutput(true);

  std::ifstream in(logPath, std::ios::binary);
  std::ostringstream contents;
  contents << in.rdbuf();
  std::string const log = contents.str();
  std::size_t const zeroHit = log.find("samples");
  check(zeroHit != std::string::npos && log.find("0 is below 1", zeroHit) != std::string::npos,
        "invalid: 0's own warning names samples and the rejected value 0");
  std::size_t const negativeHit = log.find("samples", zeroHit + 1);
  check(negativeHit != std::string::npos && log.find("-3 is below 1", negativeHit) != std::string::npos,
        "invalid: -3's own warning names samples and the rejected value -3");
}

// "pathtracer" carrying "integer other" changes nothing.
void testUnrelatedTokenIgnored() {
  OptionReadingRenderMan impl;
  setSamples(impl, 8);
  setSamplesOther(impl, 99);
  check(impl.options().getPathtracerSamples() == 8,
        "unrelated token: \"pathtracer\" \"integer other\" leaves the value at 8");
}

// Option "render" "string indirect" and Option "radiosity" "float
// elementsize" leave the samples value unchanged and still read back
// themselves.
void testUnrelatedOptionNamesIgnored() {
  OptionReadingRenderMan impl;
  setSamples(impl, 32);

  setIndirectPassName(impl, "radiosity");
  check(impl.options().getPathtracerSamples() == 32, "unrelated name: \"render\" leaves the samples value at 32");
  check(impl.options().getIndirectPass() == "radiosity", "unrelated name: the indirect pass name reads back");

  setElementSize(impl, 0.6f);
  check(impl.options().getPathtracerSamples() == 32, "unrelated name: \"radiosity\" leaves the samples value at 32");
  check(impl.options().getRadiosityElementSize() == 0.6f, "unrelated name: the element size still reads back after");
}

} // namespace

int main() {
  testUnsetReadsFour();
  testSetAndReplace();
  testInvalidValuesWarnAndLeaveUnchanged();
  testUnrelatedTokenIgnored();
  testUnrelatedOptionNamesIgnored();

  return checkSummary("Option \"pathtracer\" \"integer samples\": unset reads 4, a later Option replaces an earlier "
                      "one, a value below 1 leaves it unchanged but logs a warning, an unrelated token changes "
                      "nothing, and \"render\"/\"radiosity\" Options leave it -- and read back themselves -- "
                      "unaffected");
}
