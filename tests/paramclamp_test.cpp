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
 * F6: a RIB-supplied parameter array shorter than its declared class used
 * to make GMANParameterList::copy_float/copy_integer/copy_string read past
 * the array's own allocation (ASan heap-buffer-overflow). gman is now
 * expected to warn once, naming the parameter and both lengths, clamp and
 * zero-fill instead. Bicubic is covered separately from bilinear because
 * RiPatchV derives a different required count from the same code path (16
 * versus 4); patch_param_reorder.rib additionally proves the count stays
 * aligned with its value even though parseParameterList's std::map emits
 * in key order, not push order.
 *
 * Each fixture is run out-of-process (fork/exec, like
 * tests/ribmalformed_test.cpp) so a regression that reintroduces the
 * overflow shows up as a crash under this test's own bound, not just under
 * the debug preset's ASan build -- though ASan is what turns the overflow
 * into a deterministic abort rather than a read of whatever heap byte
 * happened to follow the allocation.
 */

#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "check.h"

namespace {

struct RunResult {
  bool timedOut = false;
  bool crashed = false;  // terminated by a signal (SIGSEGV, SIGABRT, ...)
  int exitStatus = -1;
  std::string output;
};

// Runs gman on rib with a hard wall-clock bound, capturing its combined
// stdout/stderr for the caller to inspect -- warning() (gmanlog.cpp) writes
// to stdout, not stderr. Polls rather than SIGALRM, matching
// ribmalformed_test.cpp's own reasoning.
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
    execl(gman.c_str(), gman.c_str(), rib.c_str(), (char *)nullptr);
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

// Counts non-overlapping occurrences of needle in haystack.
int countOccurrences(const std::string &haystack, const std::string &needle) {
  int count = 0;
  std::string::size_type pos = 0;
  while ((pos = haystack.find(needle, pos)) != std::string::npos) {
    ++count;
    pos += needle.size();
  }
  return count;
}

}  // namespace

int main(int argc, char *argv[]) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s <gman-binary> <tests/rib/malformed dir>\n",
                 argv[0]);
    return 2;
  }
  const std::string gman = argv[1];
  const std::string dir = argv[2];

  struct Fixture {
    const char *file;
    const char *expectedWarning;
  };
  const Fixture fixtures[] = {
      {"patch_short_p_bilinear.rib",
       "Parameter \"P\": declared length 12, supplied length 6"},
      {"patch_short_p_bicubic.rib",
       "Parameter \"P\": declared length 48, supplied length 6"},
      {"patchmesh_short_p.rib",
       "Parameter \"P\": declared length 18, supplied length 9"},
      {"patch_param_reorder.rib",
       "Parameter \"P\": declared length 12, supplied length 6"},
  };

  for (const Fixture &fixture : fixtures) {
    const std::string rib = dir + "/" + fixture.file;
    RunResult r = runCapturingOutput(gman, rib, 10);
    check(!r.timedOut,
          std::string(fixture.file) + ": does not hang (10s bound)");
    check(!r.crashed,
          std::string(fixture.file) +
              ": does not crash -- a short array stays inside its own "
              "allocation instead of reading past it");
    check(r.exitStatus == 0,
          std::string(fixture.file) + ": exits cleanly (degrade, don't abort)");
    check(r.output.find(fixture.expectedWarning) != std::string::npos,
          std::string(fixture.file) +
              ": warns naming the short parameter and both lengths");
  }

  // patch_param_reorder.rib's other three parameters ("N", "Cs", "st") are
  // each fully supplied; the map reordering parseParameterList's own
  // std::map performs must not spuriously clamp one of them because a
  // counts array built in the wrong order lined a full-length value up
  // with a different key's short count.
  {
    RunResult r =
        runCapturingOutput(gman, dir + "/patch_param_reorder.rib", 10);
    check(countOccurrences(r.output, "Parameter \"") == 1,
          "patch_param_reorder.rib: only the short parameter (\"P\") warns, "
          "not one of the fully-supplied ones a misaligned counts array "
          "would have named instead");
  }

  return checkSummary("RIB-supplied parameter array clamp holds");
}
