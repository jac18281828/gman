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
 * The counts-aware RiPolygonV rejects a negative or overflowing nverts,
 * with or without a "P" parameter, warning once and adding the empty stub
 * rather than throwing, corrupting memory or dropping the request
 * silently.
 */

#include <climits>
#include <cstdio>
#include <exception>
#include <fstream>
#include <sstream>
#include <string>

#include "check.h"
#include "gmanlog.h"
#include "gmanrendermanimpl.h"
#include "ri.h"

namespace {

// Ends a minimal, otherwise-untouched world so RiEnd leaves no renderer or
// pending parameter buffers behind.
void closeWorld(GMANRenderManImpl& renderMan) {
  renderMan.RiWorldEnd();
  renderMan.RiEnd();
}

void openWorld(GMANRenderManImpl& renderMan, char* displayName) {
  renderMan.RiBegin("gmanzbuffer");
  renderMan.RiFormat(4, 4, 1.0f);
  renderMan.RiDisplayV(displayName, RI_FILE, RI_RGB, 0, nullptr, nullptr);
  renderMan.RiProjectionV(RI_ORTHOGRAPHIC, 0, nullptr, nullptr);
  renderMan.RiWorldBegin();
}

// Runs fn with warnings captured to a fresh file at path, as
// tests/polygonmesh_test.cpp's own captureLog does, and returns the file's
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

// A negative or overflowing nverts, sent through the counts-aware
// RiPolygonV with or without a "P" parameter. Catches std::exception around
// the call so an unfixed throw fails the check below rather than crashing
// the test binary. Returns whether the call returned rather than throwing.
bool callPolygon(GMANRenderManImpl& renderMan, RtInt nverts, bool withP) {
  RtFloat p[3] = {0, 0, 0};
  RtToken tokens[1] = {RI_P};
  RtPointer parms[1] = {p};
  try {
    if (withP) {
      renderMan.RiPolygonV(nverts, 1, tokens, parms, nullptr);
    } else {
      renderMan.RiPolygonV(nverts, 0, nullptr, nullptr, nullptr);
    }
    return true;
  } catch (std::exception const&) {
    return false;
  }
}

void testNegativeNvertsWithP(std::string const& logPath) {
  GMANRenderManImpl renderMan;
  char displayName[] = "polygonnverts_negative_p.tif";
  openWorld(renderMan, displayName);

  bool returned = false;
  std::string const log = captureLog(logPath, [&] { returned = callPolygon(renderMan, -1, /*withP=*/true); });
  check(returned, "negative nverts, with \"P\": RiPolygonV returns rather than throwing");
  check(log.find("Polygon: nverts[0] = -1 is negative; ignoring.") != std::string::npos,
        "negative nverts, with \"P\": the log holds the negative-nverts message");

  closeWorld(renderMan);
}

void testNegativeNvertsWithoutP(std::string const& logPath) {
  GMANRenderManImpl renderMan;
  char displayName[] = "polygonnverts_negative_nop.tif";
  openWorld(renderMan, displayName);

  bool returned = false;
  std::string const log = captureLog(logPath, [&] { returned = callPolygon(renderMan, -1, /*withP=*/false); });
  check(returned, "negative nverts, no parameter: RiPolygonV returns rather than throwing");
  check(log.find("Polygon: nverts[0] = -1 is negative; ignoring.") != std::string::npos,
        "negative nverts, no parameter: the log holds the negative-nverts message");

  closeWorld(renderMan);
}

// nverts one above INT_MAX / 3, with no parameter.
void testOverflowingNvertsWithoutP(std::string const& logPath) {
  GMANRenderManImpl renderMan;
  char displayName[] = "polygonnverts_overflow_nop.tif";
  openWorld(renderMan, displayName);

  RtInt const nverts = INT_MAX / 3 + 1;
  bool returned = false;
  std::string const log = captureLog(logPath, [&] { returned = callPolygon(renderMan, nverts, /*withP=*/false); });
  check(returned, "overflowing nverts, no parameter: RiPolygonV returns rather than throwing");
  check(log.find("Polygon: nverts sums to " + std::to_string(nverts) + ", times 3 overflows RtInt; ignoring.") !=
            std::string::npos,
        "overflowing nverts, no parameter: the log holds the overflow message");

  closeWorld(renderMan);
}

} // namespace

int main() {
  std::string const logPath = "polygonnverts.log";

  testNegativeNvertsWithP(logPath);
  testNegativeNvertsWithoutP(logPath);
  testOverflowingNvertsWithoutP(logPath);

  return checkSummary("RiPolygonV rejects a negative or overflowing nverts");
}
