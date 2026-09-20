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
 * A RIB integer literal is bounded once, in GMANRIBTokenize::parseNum,
 * directly against RtInt's own range, where the token is built -- strtol's
 * own saturation at LONG_MIN/LONG_MAX already falls outside that range, so
 * the one check catches a literal that overflows `long` itself and one
 * that merely exceeds RtInt. GMANRIBParse::nextInt and
 * GMANRIBParse::TokenVector::toRtIntVector then take the token's value
 * directly; neither re-checks it. gman::InlineParse::get_size narrows a
 * Declare array size the same way, on its own path.
 *
 * sides_overflow.rib and generalpolygon_nverts_overflow.rib cover the
 * "fits `long`, not RtInt" former failure range (4294967297) in an integer
 * slot; clipping_literal_overflow.rib covers the "overflows `long` itself"
 * former failure range (99999999999999999999); both land here as of this
 * commit rather than after a second, destination-side check.
 * clipping_float_slot_rtint_overflow.rib covers the newer rule that an
 * out-of-range integer literal is an error even when its destination is a
 * float, not just when it is an RtInt.
 *
 * tests/ribmalformed_test.cpp's own harness only asserts a fixture neither
 * hangs nor crashes; checks 1, 2, 3 and 5's fixtures already satisfy that
 * bar before the fix, silently and with exit 0, so it would prove nothing
 * there. This file borrows tests/paramclamp_test.cpp's runCapturingOutput
 * instead, asserting both the exit status and the exact error text.
 */

#include <cstdio>
#include <cstdlib>
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

// See tests/paramclamp_test.cpp's own runCapturingOutput: polls rather than
// SIGALRM, captures the child's combined stdout/stderr for inspection.
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

  // Checks 1 through 5: each fixture's overflowing value must be refused
  // with RIE_RANGE, and the run must exit with failure rather than
  // rendering a truncated result or, for check 4, crashing. 1 and 2 are
  // the "fits long, not RtInt" former failure range; 3 is the "overflows
  // long itself" former failure range; 4 is get_size's own Declare-array
  // bound, unrelated to parseNum; 5 is the newer rule that an
  // out-of-range integer literal is an error even in a float slot.
  const char* rangeFixtures[] = {
      "sides_overflow.rib",
      "generalpolygon_nverts_overflow.rib",
      "clipping_literal_overflow.rib",
      "declare_array_size_overflow.rib",
      "clipping_float_slot_rtint_overflow.rib",
  };

  for (const char* fixture : rangeFixtures) {
    const std::string rib = dir + "/" + fixture;
    RunResult r = runCapturingOutput(gman, rib, 10);
    check(!r.timedOut, std::string(fixture) + ": does not hang (10s bound)");
    check(!r.crashed, std::string(fixture) + ": does not crash (no signal termination)");
    check(r.exitStatus == EXIT_FAILURE, std::string(fixture) + ": exits with failure");
    check(r.output.find("RIE_RANGE") != std::string::npos, std::string(fixture) + ": reports RIE_RANGE");
  }

  return checkSummary("RIB integer bounds hold");
}
