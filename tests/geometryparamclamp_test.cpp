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
 * Polygon and the seven quadrics built their GMANParameterList with no
 * suppliedCounts at all, so a short RIB-supplied array read past its own
 * allocation (ASan heap-buffer-overflow in copy_float) instead of
 * clamping the way tests/paramclamp_test.cpp's six protected requests
 * already do. gman is now expected to warn once, naming the parameter and
 * both lengths, clamp and zero-fill instead, for every one of the eight.
 *
 * Each fixture is run out-of-process (fork/exec, like
 * tests/ribmalformed_test.cpp and tests/paramclamp_test.cpp) so a
 * regression that reintroduces the overflow shows up as a crash under
 * this test's own bound, not just under the debug preset's ASan build.
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include "check.h"

namespace {

struct RunResult {
  bool timedOut = false;
  bool crashed = false; // terminated by a signal (SIGSEGV, SIGABRT, ...)
  int exitStatus = -1;
  std::string output;
};

// Runs gman on rib with a hard wall-clock bound, capturing its combined
// stdout/stderr for the caller to inspect -- warning() (gmanlog.cpp) writes
// to stdout, not stderr. Polls rather than SIGALRM, matching
// ribmalformed_test.cpp's own reasoning.
RunResult runCapturingOutput(const std::string& gman, const std::string& rib, int timeoutSeconds) {
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
    execl(gman.c_str(), gman.c_str(), rib.c_str(), (char*)nullptr);
    _exit(127);
  }
  close(pipeFds[1]);

  char buf[4096];
  ssize_t n;
  while ((n = read(pipeFds[0], buf, sizeof(buf))) > 0) {
    result.output.append(buf, (size_t)n);
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
    RunResult r = runCapturingOutput(gman, rib, 10);
    check(!r.timedOut, std::string(fixture.file) + ": does not hang (10s bound)");
    check(!r.crashed, std::string(fixture.file) + ": does not crash -- a short array stays inside its own "
                                                  "allocation instead of reading past it");
    check(r.exitStatus == 0, std::string(fixture.file) + ": exits cleanly (degrade, don't abort)");
    check(r.output.find(fixture.expectedWarning) != std::string::npos,
          std::string(fixture.file) + ": warns naming the short parameter and both lengths");
  }

  return checkSummary("Polygon and the seven quadrics clamp a short RIB-supplied parameter array");
}
