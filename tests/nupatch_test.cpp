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
 * NuPatch, request commit: RiNuPatchV validates every RISpec 3.2 rule and
 * sizes the parameter list, parseNuPatch checks knot lengths and owns its
 * arrays across a throw. GMANNuPatch is not yet a GMANParametric, so a
 * valid NuPatch still renders nothing -- the evaluator lands in the next
 * commit.
 *
 * Two proof mechanisms:
 *
 *  - Malformed requests: tests/paramclamp_test.cpp's own
 *    Fixture{file, expectedWarning} shape.
 *  - Exception safety: a NuPatch issued inside an illegal block, proving
 *    parseNuPatch's knot arrays survive the throw that follows.
 */

#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <string>

#include "check.h"

namespace {

// ---- Malformed requests (commit 1) ----

struct RunResult {
  bool timedOut = false;
  bool crashed = false;
  int exitStatus = -1;
  std::string output;
};

// tests/paramclamp_test.cpp's own shape: fork/exec, waitpid,
// WIFEXITED/WEXITSTATUS, WIFSIGNALED crash detection.
RunResult runCapturingOutput(const std::string &gman, const std::string &rib,
                             int timeoutSeconds) {
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
    execl(gman.c_str(), gman.c_str(), rib.c_str(), (char *) nullptr);
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
      {"nupatch_short_uknot.rib",
       "NuPatch: uknot length 3 does not match nu + uorder = 4; ignoring."},
      {"nupatch_decreasing_knots.rib",
       "NuPatch: uknot[2]=0.25 is less than uknot[1]=0.5, not "
       "non-decreasing; ignoring."},
      {"nupatch_order_exceeds_n.rib",
       "NuPatch: nu=2 uorder=3 violates nu >= uorder >= 1; ignoring."},
      {"nupatch_empty_range.rib",
       "NuPatch: umin=0.5 umax=0.5 violates umin < umax; ignoring."},
      {"nupatch_umin_below_knot.rib",
       "NuPatch: umin=-0.1 is less than uknot[uorder-1]=0; ignoring."},
      {"nupatch_short_pw.rib",
       "Parameter \"Pw\": declared length 16, supplied length 14; "
       "clamping and zero-filling the remainder."},
      {"nupatch_zero_weight.rib",
       "NuPatch: control point 2 has non-positive \"Pw\" weight 0; "
       "ignoring."},
  };

  for (const Fixture &fixture : fixtures) {
    const std::string rib = malformedDir + "/" + fixture.file;
    RunResult r = runCapturingOutput(gman, rib, 10);
    check(!r.timedOut,
          std::string(fixture.file) + ": does not hang (10s bound)");
    check(!r.crashed, std::string(fixture.file) + ": does not crash");
    check(r.exitStatus == 0,
          std::string(fixture.file) + ": exits 0 (degrade, don't abort)");
    check(r.output.find(fixture.expectedWarning) != std::string::npos,
          std::string(fixture.file) + ": warns naming the rule and its "
                                      "values");
  }
}

// ---- Exception safety (commit 1) ----

void testIllegalBlockLeaksNothing(const std::string &gman,
                                  const std::string &malformedDir) {
  const std::string rib = malformedDir + "/nupatch_illegal_block.rib";
  RunResult r = runCapturingOutput(gman, rib, 10);
  check(!r.timedOut, "nupatch_illegal_block.rib: does not hang (10s bound)");
  check(!r.crashed, "nupatch_illegal_block.rib: does not crash");
  check(r.exitStatus == 1,
        "nupatch_illegal_block.rib: exits 1 (GMANHandleError, "
        "RIE_ILLSTATE) -- on Linux, a leak overrides this with LSan's own "
        "23, so this assertion doubles as the leak-freedom proof under "
        "ctest --test-dir build-debug");
}

}  // namespace

int main(int argc, char *argv[]) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s <gman-binary> <tests/rib dir>\n",
                argv[0]);
    return 2;
  }
  const std::string gman = argv[1];
  const std::string ribDir = argv[2];
  const std::string malformedDir = ribDir + "/malformed";

  testMalformedFixtures(gman, malformedDir);
  testIllegalBlockLeaksNothing(gman, malformedDir);

  return checkSummary("nupatch holds");
}
