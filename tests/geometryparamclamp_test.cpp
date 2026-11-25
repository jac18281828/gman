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
 * heap-buffer-overflow in copy_float) instead of stopping the file the way
 * tests/paramclamp_test.cpp's six protected requests do. gman stops the
 * file with RIE_CONSISTENCY, naming the parameter and both lengths, and
 * writes no image, for every one of the eight.
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
#include "imagecoverage.h"
#include "paramrejection.h"
#include "rungman.h"

int main(int argc, char* argv[]) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s <gman-binary> <tests/rib/malformed dir>\n", argv[0]);
    return 2;
  }
  const std::string gman = argv[1];
  const std::string dir = argv[2];

  struct Fixture {
    const char* file;
    char const* expectedError;
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

  for (Fixture const& fixture : fixtures) {
    paramrejection::checkStoppedShortArray(gman, dir, fixture.file, fixture.expectedError);
  }

  return checkSummary("Polygon and the seven quadrics stop the file on a short RIB-supplied parameter array");
}
