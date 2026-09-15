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
 * PointsPolygons and PointsGeneralPolygons: every face is the equivalent
 * Polygon/GeneralPolygon, gathered through "verts" out of a shared "P"
 * instead of carrying its own. This first commit covers parsing and
 * validation only -- both requests still render nothing -- through
 * malformed request fixtures in tests/paramclamp_test.cpp's own Fixture
 * shape. The object-manager and twin-RIB render cases join once the
 * meshes themselves render (the commit after this one).
 */

#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdio>
#include <string>

#include "check.h"

namespace {

struct RunResult {
  bool timedOut = false;
  bool crashed = false;
  int exitStatus = -1;
  std::string output;
};

// Runs gman out-of-process with a hard wall-clock bound, capturing its
// combined stdout/stderr -- tests/paramclamp_test.cpp's own shape, plus an
// optional "-d" so the desync check below can read the debug keyword
// trace.
RunResult runCapturingOutput(const std::string &gman, const std::string &rib,
                             int timeoutSeconds, bool debug = false) {
  RunResult result;

  int pipeFds[2];
  if (pipe(pipeFds) != 0) {
    return result;
  }

  pid_t pid = fork();
  if (pid < 0) {
    close(pipeFds[0]);
    close(pipeFds[1]);
    return result;
  }
  if (pid == 0) {
    close(pipeFds[0]);
    dup2(pipeFds[1], STDOUT_FILENO);
    dup2(pipeFds[1], STDERR_FILENO);
    close(pipeFds[1]);
    if (debug) {
      execl(gman.c_str(), gman.c_str(), "-d", rib.c_str(), (char *) nullptr);
    } else {
      execl(gman.c_str(), gman.c_str(), rib.c_str(), (char *) nullptr);
    }
    _exit(127);
  }
  close(pipeFds[1]);

  char buf[4096];
  ssize_t n;
  while ((n = read(pipeFds[0], buf, sizeof(buf))) > 0) {
    result.output.append(buf, (std::size_t) n);
  }
  close(pipeFds[0]);

  const int pollIntervalUs = 50 * 1000;
  const int maxPolls = (timeoutSeconds * 1000000) / pollIntervalUs;
  int status = 0;
  for (int i = 0; i < maxPolls; ++i) {
    pid_t r = waitpid(pid, &status, WNOHANG);
    if (r == pid) {
      if (WIFEXITED(status)) {
        result.exitStatus = WEXITSTATUS(status);
      } else if (WIFSIGNALED(status)) {
        result.crashed = true;
      }
      return result;
    }
    usleep(pollIntervalUs);
  }

  result.timedOut = true;
  kill(pid, SIGKILL);
  waitpid(pid, &status, 0);
  return result;
}

void testMalformedFixtures(const std::string &gman,
                           const std::string &malformedDir) {
  struct Fixture {
    const char *file;
    const char *expectedWarning;
  };
  const Fixture fixtures[] = {
      {"pointspolygons_short_verts.rib",
       "PointsPolygons: verts length 7 does not match nverts sum 8; "
       "ignoring."},
      {"pointsgeneralpolygons_short_nverts.rib",
       "PointsGeneralPolygons: nverts length 1 does not match nloops sum "
       "2; ignoring."},
      {"pointspolygons_negative_index.rib",
       "PointsPolygons: verts[2] = -1 is negative; ignoring."},
      {"pointsgeneralpolygons_zero_loops.rib",
       "PointsGeneralPolygons: nloops[0] = 0 is invalid; ignoring."},
      {"pointspolygons_short_p.rib",
       "Parameter \"P\": declared length 12, supplied length 9"},
  };

  for (const Fixture &fixture : fixtures) {
    const std::string rib = malformedDir + "/" + fixture.file;

    RunResult r = runCapturingOutput(gman, rib, 10);
    check(!r.timedOut,
          std::string(fixture.file) + ": does not hang (10s bound)");
    check(!r.crashed, std::string(fixture.file) + ": does not crash");
    check(r.exitStatus == 0,
          std::string(fixture.file) + ": exits cleanly (degrade, don't "
                                       "abort)");
    check(r.output.find(fixture.expectedWarning) != std::string::npos,
          std::string(fixture.file) +
              ": warns naming the rule and both values");

    RunResult debugRun = runCapturingOutput(gman, rib, 10, /*debug=*/true);
    check(debugRun.output.find("Keyword token: Sphere") != std::string::npos,
          std::string(fixture.file) +
              ": the Sphere after it still parses (no desync)");
  }
}

}  // namespace

int main(int argc, char *argv[]) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s <gman-binary> <tests/rib dir>\n", argv[0]);
    return 2;
  }
  const std::string gman = argv[1];
  const std::string ribDir = argv[2];

  testMalformedFixtures(gman, ribDir + "/malformed");

  return checkSummary("pointspolygons holds");
}
