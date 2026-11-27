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
 * A RIB request sizes each parameter array from the counts it declares. A
 * count that no RtInt holds stops the file with RIE_RANGE, and an array
 * shorter than its declaration stops it with RIE_CONSISTENCY, by one value or
 * by millions: the file pays for every element it declares. Each fixture
 * runs out of process under a timeout (tests/rungman.h) and must neither
 * hang nor crash, exit with failure, report its code against the parameter
 * "P", and leave no image behind.
 */

#include <cstdio>
#include <cstdlib>
#include <string>

#include "check.h"
#include "imagecoverage.h"
#include "paramrejection.h"
#include "rungman.h"

namespace {

constexpr int runBoundSeconds = 10;

} // namespace

int main(int argc, char* argv[]) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s <gman-binary> <tests/rib/malformed dir>\n", argv[0]);
    return 2;
  }
  const std::string gman = argv[1];
  const std::string dir = argv[2];

  struct Fixture {
    char const* file;
    char const* code;
  };
  Fixture const fixtures[] = {
      {"patchmesh_dimensions_overflow.rib", "RIE_RANGE"},
      {"pointspolygons_declared_vertices.rib", "RIE_CONSISTENCY"},
      {"generalpolygon_declared_nverts.rib", "RIE_CONSISTENCY"},
      {"patchmesh_declared_dimensions.rib", "RIE_CONSISTENCY"},
      {"pointspolygons_short_p_by_one.rib", "RIE_CONSISTENCY"},
  };

  for (Fixture const& fixture : fixtures) {
    const std::string name = fixture.file;
    std::remove(malformedImagePath);
    GMANRunOptions options;
    options.timeoutSeconds = runBoundSeconds;
    GMANRunResult const r = runGman(gman, {dir + "/" + name}, options);
    check(!r.timedOut, name + ": does not hang (" + std::to_string(runBoundSeconds) + "s bound)");
    check(!r.crashed, name + ": does not crash");
    check(r.exitStatus == EXIT_FAILURE, name + ": exits with failure");
    check(r.output.find(fixture.code) != std::string::npos, name + ": reports " + fixture.code);
    check(r.output.find("\"P\"") != std::string::npos, name + ": names the parameter \"P\"");
    paramrejection::checkNoImageWritten(name);
  }

  return checkSummary("RIB-declared counts stop the file");
}
