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
 * A RIB integer narrows twice on its way from text to RtInt: strtol in
 * GMANRIBTokenize::parseNum, then the implicit long-to-RtInt conversion in
 * GMANRIBParse::nextInt and GMANRIBParse::TokenVector::toRtIntVector.
 * gman::InlineParse::get_size narrows a Declare array size the same way.
 * Each fixture here proves one of those narrowings raises RIE_RANGE
 * instead of silently truncating or, for the Declare case, aborting on an
 * uncaught std::bad_alloc.
 *
 * tests/ribmalformed_test.cpp's own harness only asserts a fixture neither
 * hangs nor crashes; checks 1-3's fixtures already satisfy that bar before
 * the fix, silently and with exit 0, so it would prove nothing there. This
 * file borrows tests/paramclamp_test.cpp's runCapturingOutput instead,
 * asserting both the exit status and the exact error text.
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

  // Checks 1, 2, 3 and 4: each fixture's overflowing value must be
  // refused with RIE_RANGE, and the run must exit with failure rather
  // than rendering a truncated result or, for check 4, crashing.
  const char* rangeFixtures[] = {
      "sides_overflow.rib",
      "generalpolygon_nverts_overflow.rib",
      "clipping_literal_overflow.rib",
      "declare_array_size_overflow.rib",
  };

  for (const char* fixture : rangeFixtures) {
    const std::string rib = dir + "/" + fixture;
    RunResult r = runCapturingOutput(gman, rib, 10);
    check(!r.timedOut, std::string(fixture) + ": does not hang (10s bound)");
    check(!r.crashed, std::string(fixture) + ": does not crash (no signal termination)");
    check(r.exitStatus == EXIT_FAILURE, std::string(fixture) + ": exits with failure");
    check(r.output.find("RIE_RANGE") != std::string::npos, std::string(fixture) + ": reports RIE_RANGE");
  }

  // Check 3b: a stale ERANGE left by an overflowing float literal must not
  // poison the legal integer that follows it.
  {
    const std::string rib = dir + "/clipping_stale_erange.rib";
    RunResult r = runCapturingOutput(gman, rib, 10);
    check(!r.timedOut, "clipping_stale_erange.rib: does not hang (10s bound)");
    check(!r.crashed, "clipping_stale_erange.rib: does not crash (no signal termination)");
    check(r.exitStatus == EXIT_SUCCESS, "clipping_stale_erange.rib: exits cleanly");
    check(r.output.find("RIE_RANGE") == std::string::npos,
          "clipping_stale_erange.rib: the legal Sides 4 that follows an overflowing float "
          "is not rejected by a stale errno");
  }

  return checkSummary("RIB integer bounds hold");
}
