/* SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Copyright (c) 2026 John Cairns <john@2ad.com>
 */

/*
 * Step 4: the RIB tokenizer on malformed input it has not already been
 * exercised against -- an unterminated string, an unbalanced bracketed
 * array, and a file truncated mid-keyword with no trailing newline (the
 * exact end-of-input shape the rib-frontend-fixes prompt's tokenizer
 * defect lived in; SPEC.md Section 8). tests/ribdialect_test.cpp already
 * covers two malformed shapes (a non-string array element, a non-string
 * Display argument) that are type errors, not tokenizer-level ones.
 *
 * The requirement is narrower than "parses correctly": a clean, bounded
 * exit -- crash or hang either would defeat every other test's own
 * process-spawning assumption. Each fixture runs under a hard wall-clock
 * timeout, enforced by this test itself (fork/exec/waitpid, not ctest's
 * own TIMEOUT property, which bounds the whole binary, not one fixture).
 */

#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <string>

#include "check.h"

namespace {

struct RunResult {
  bool timedOut = false;
  bool crashed = false;  // terminated by a signal (SIGSEGV, SIGABRT, ...)
  int exitStatus = -1;
};

// Polls rather than using SIGALRM: simpler to reason about across the two
// platforms these tests run on, and 50ms resolution is more than tight
// enough against a multi-second timeout.
RunResult runWithTimeout(const std::string &gman, const std::string &rib,
                         int timeoutSeconds) {
  RunResult result;

  pid_t pid = fork();
  if (pid < 0) {
    return result;
  }
  if (pid == 0) {
    std::freopen("/dev/null", "w", stdout);
    std::freopen("/dev/null", "w", stderr);
    execl(gman.c_str(), gman.c_str(), rib.c_str(), (char *)nullptr);
    _exit(127);
  }

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

}  // namespace

int main(int argc, char *argv[]) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s <gman-binary> <tests/rib/malformed dir>\n",
                 argv[0]);
    return 2;
  }
  const std::string gman = argv[1];
  const std::string dir = argv[2];

  const char *fixtures[] = {
      "unterminated_string.rib",
      "unbalanced_bracket.rib",
      "truncated.rib",
  };

  for (const char *fixture : fixtures) {
    const std::string rib = dir + "/" + fixture;
    RunResult r = runWithTimeout(gman, rib, 10);
    check(!r.timedOut, std::string(fixture) + ": does not hang (10s bound)");
    check(!r.crashed,
          std::string(fixture) + ": does not crash (no signal termination)");
  }

  return checkSummary("RIB malformed-input handling holds");
}
