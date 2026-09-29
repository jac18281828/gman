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
 * Step 5: ReadArchive. A parent RIB that reads a child contributing
 * geometry, and a RIB that reads itself -- which must error cleanly rather
 * than hang, so this drives gman under an explicit process-level timeout
 * rather than trusting it to exit on its own.
 */

#include <cstdio>
#include <cstdlib>
#include <string>

#include "check.h"
#include "rungman.h"

namespace {

// Runs gman under -d with a hard wall-clock deadline. A cycle that is not
// caught would otherwise hang this test (and the ctest run) forever. The
// caller inspects the -d token trace to prove the child's geometry
// actually arrived: an exit status alone cannot distinguish "the archive
// was read" from "the archive was ignored", since a ReadArchive that
// silently does nothing still exits 0, having rendered an empty world.
GMANRunResult runWithTimeout(std::string const& gman, std::string const& rib, int timeoutSeconds) {
  GMANRunOptions options;
  options.timeoutSeconds = timeoutSeconds;
  return runGman(gman, {"-d", rib}, options);
}

} // namespace

int main(int argc, char* argv[]) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s <gman-binary> <tests/rib/archive dir>\n", argv[0]);
    return 2;
  }

  const std::string gman = argv[1];
  const std::string archiveDir = argv[2];

  {
    const std::string parent = archiveDir + "/parent.rib";
    GMANRunResult r = runWithTimeout(gman, parent, 20);
    check(!r.timedOut, "parent.rib: does not hang");
    check(!r.timedOut && r.exitStatus == 0, "parent.rib: exits 0");

    // The exit status alone proves nothing here: a ReadArchive that quietly
    // does nothing renders an empty world and still exits 0. parent.rib
    // contains no Sphere of its own -- the only one in the chain is in
    // child.rib -- so the token appearing in the trace is what distinguishes
    // "the archive was read" from "the archive was skipped".
    check(r.output.find("Keyword token: Sphere") != std::string::npos,
          "parent.rib: child.rib's Sphere reaches the parser (archive read, "
          "not silently ignored)");
  }

  {
    const std::string selfInclude = archiveDir + "/selfinclude.rib";
    GMANRunResult r = runWithTimeout(gman, selfInclude, 20);
    check(!r.timedOut, "selfinclude.rib: a self-including RIB does not hang");
    check(!r.timedOut && r.exitStatus != 0, "selfinclude.rib: errors cleanly instead of rendering garbage");
    check(r.output.find("ReadArchive") != std::string::npos || r.output.find("archive") != std::string::npos,
          "selfinclude.rib: the diagnostic names the archive cycle");
  }

  return checkSummary("ReadArchive holds");
}
