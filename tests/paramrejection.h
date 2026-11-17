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
 * and the token as written, and a completed scene. checkAcceptedParameter
 * expects the same fixture shape to pass without a word.
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

inline void checkAcceptedParameter(std::string const& gman, std::string const& dir, std::string const& fixture) {
  GMANRunResult const r = runFixture(gman, dir + "/" + fixture);
  check(!r.timedOut, fixture + ": does not hang (" + std::to_string(runBoundSeconds) + "s bound)");
  check(!r.crashed, fixture + ": does not crash");
  check(r.exitStatus == EXIT_SUCCESS, fixture + ": exits cleanly");
  check(r.output.find(rejectionMarker) == std::string::npos, fixture + ": logs no rejection");
  checkDegradedRender(fixture, 0);
}

} // namespace paramrejection
