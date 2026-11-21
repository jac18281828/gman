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
 * The run-and-check body the parameter-rejection tests share. Each helper
 * runs gman on one tests/rib/malformed fixture from the test's own working
 * directory under a time bound, then checks the exit status, the console
 * output and the rendered image.
 *
 * checkRejectedParameter expects the parser to drop one parameter: a clean
 * exit, exactly one "parameter ignored" line, that line naming the request
 * and the token as written, and a completed scene. checkStoppedParameter
 * expects the parser to stop the file at one parameter: failure exit, an
 * RIE_CONSISTENCY line naming the request and the token as written, and no
 * image. checkAcceptedParameter expects the same fixture shape to pass
 * without a word.
 */

#pragma once

#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>

#include "check.h"
#include "imagecoverage.h"
#include "rungman.h"

namespace paramrejection {

constexpr int runBoundSeconds = 20;
constexpr char const* rejectionMarker = "parameter ignored";

inline GMANRunResult runFixture(std::string const& gman, std::string const& path) {
  std::remove(malformedImagePath);
  GMANRunOptions options;
  options.timeoutSeconds = runBoundSeconds;
  return runGman(gman, {path}, options);
}

inline std::string markerLines(std::string const& output, int& count) {
  std::string firstLine;
  std::istringstream lines(output);
  std::string line;
  count = 0;
  while (std::getline(lines, line)) {
    if (line.find(rejectionMarker) == std::string::npos) {
      continue;
    }
    if (count == 0) {
      firstLine = line;
    }
    ++count;
  }
  return firstLine;
}

inline void checkRejectedParameter(std::string const& gman, std::string const& dir, std::string const& fixture,
                                   std::string const& request, std::string const& token) {
  GMANRunResult const r = runFixture(gman, dir + "/" + fixture);
  check(!r.timedOut, fixture + ": does not hang (" + std::to_string(runBoundSeconds) + "s bound)");
  check(!r.crashed, fixture + ": does not crash");
  check(r.exitStatus == EXIT_SUCCESS, fixture + ": exits cleanly (drop the parameter, not the request)");

  int count = 0;
  std::string const line = markerLines(r.output, count);
  check(count == 1, fixture + ": logs exactly one rejection (" + std::to_string(count) + " found)");
  check(line.find(request + " \"" + token + "\"") != std::string::npos,
        fixture + ": the rejection names " + request + " and \"" + token + "\"");
  checkDegradedRender(fixture, 0);
}

// True when the working directory holds the image a fixture renders.
inline bool imageWritten() {
  std::FILE* const file = std::fopen(malformedImagePath, "rb");
  if (file == nullptr) {
    return false;
  }
  std::fclose(file);
  return true;
}

inline void checkNoImageWritten(std::string const& fixture) { check(!imageWritten(), fixture + ": writes no image"); }

// Runs fixture and checks that gman stopped the file at the parameter token of
// request: no hang, no crash, exit EXIT_FAILURE, one line carrying
// RIE_CONSISTENCY and `<request> "<token>"`, and no image.
inline void checkStoppedParameter(std::string const& gman, std::string const& dir, std::string const& fixture,
                                  std::string const& request, std::string const& token) {
  GMANRunResult const r = runFixture(gman, dir + "/" + fixture);
  check(!r.timedOut, fixture + ": does not hang (" + std::to_string(runBoundSeconds) + "s bound)");
  check(!r.crashed, fixture + ": does not crash");
  check(r.exitStatus == EXIT_FAILURE, fixture + ": exits with failure (the file stops)");

  bool named = false;
  std::istringstream lines(r.output);
  std::string line;
  while (std::getline(lines, line)) {
    if (line.find("RIE_CONSISTENCY") != std::string::npos &&
        line.find(request + " \"" + token + "\"") != std::string::npos) {
      named = true;
    }
  }
  check(named, fixture + ": reports RIE_CONSISTENCY naming " + request + " and \"" + token + "\"");
  checkNoImageWritten(fixture);
}

inline void checkAcceptedParameter(std::string const& gman, std::string const& dir, std::string const& fixture) {
  GMANRunResult const r = runFixture(gman, dir + "/" + fixture);
  check(!r.timedOut, fixture + ": does not hang (" + std::to_string(runBoundSeconds) + "s bound)");
  check(!r.crashed, fixture + ": does not crash");
  check(r.exitStatus == EXIT_SUCCESS, fixture + ": exits cleanly");
  check(r.output.find(rejectionMarker) == std::string::npos, fixture + ": logs no rejection");
  checkDegradedRender(fixture, 0);
}

} // namespace paramrejection
