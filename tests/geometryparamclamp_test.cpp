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
 * Guards against Polygon and the seven quadrics building their
 * GMANParameterList with no suppliedCounts: without it, a short
 * RIB-supplied array would read past its own allocation (ASan
 * heap-buffer-overflow in copy_float) instead of clamping the way
 * tests/paramclamp_test.cpp's six protected requests do. gman warns
 * once, naming the parameter and both lengths, and clamps and
 * zero-fills instead, for every one of the eight.
 *
 * Each fixture runs out of process under a timeout (tests/rungman.h), so
 * a regression that reintroduces the overflow shows up as a crash under
 * this test's own bound, not just under the debug preset's ASan build.
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "check.h"
#include "rungman.h"

namespace {

GMANRunResult runCapturingOutput(const std::string& gman, const std::string& rib, int timeoutSeconds) {
  GMANRunOptions options;
  options.timeoutSeconds = timeoutSeconds;
  return runGman(gman, {rib}, options);
}

} // namespace

int main(int argc, char* argv[]) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s <gman-binary> <tests/rib/malformed dir>\n", argv[0]);
    return 2;
  }
  const std::string gman = argv[1];
  const std::string dir = argv[2];

  struct Fixture {
    const char* file;
    const char* expectedWarning;
  };
  const Fixture fixtures[] = {
      {"polygon_short_n.rib", "Parameter \"N\": declared length 12, supplied length 3"},
      {"sphere_short_n.rib", "Parameter \"N\": declared length 12, supplied length 3"},
      {"cone_short_n.rib", "Parameter \"N\": declared length 12, supplied length 3"},
      {"cylinder_short_n.rib", "Parameter \"N\": declared length 12, supplied length 3"},
      {"hyperboloid_short_n.rib", "Parameter \"N\": declared length 12, supplied length 3"},
      {"paraboloid_short_n.rib", "Parameter \"N\": declared length 12, supplied length 3"},
      {"disk_short_n.rib", "Parameter \"N\": declared length 12, supplied length 3"},
      {"torus_short_n.rib", "Parameter \"N\": declared length 12, supplied length 3"},
  };

  for (const Fixture& fixture : fixtures) {
    const std::string rib = dir + "/" + fixture.file;
    GMANRunResult r = runCapturingOutput(gman, rib, 10);
    check(!r.timedOut, std::string(fixture.file) + ": does not hang (10s bound)");
    check(!r.crashed, std::string(fixture.file) + ": does not crash -- a short array stays inside its own "
                                                  "allocation instead of reading past it");
    check(r.exitStatus == 0, std::string(fixture.file) + ": exits cleanly (degrade, don't abort)");
    check(r.output.find(fixture.expectedWarning) != std::string::npos,
          std::string(fixture.file) + ": warns naming the short parameter and both lengths");
  }

  return checkSummary("Polygon and the seven quadrics clamp a short RIB-supplied parameter array");
}
