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
 * A RIB-supplied parameter array longer than its declared class already
 * truncated safely -- copy_float/copy_integer/copy_string clamp to the
 * declared length -- but did so without a word to the scene author, so a
 * miscounted array vanished its excess values with no trace. gman is now
 * expected to warn once, naming the parameter and both lengths, and still
 * truncate exactly as before.
 *
 * The second fixture re-runs the existing under-supply warning verbatim,
 * proving the new over-supply branch leaves that message untouched.
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

  {
    const std::string rib = dir + "/sphere_over_n.rib";
    RunResult r = runCapturingOutput(gman, rib, 10);
    check(!r.timedOut, "sphere_over_n.rib: does not hang (10s bound)");
    check(!r.crashed, "sphere_over_n.rib: does not crash");
    check(r.exitStatus == 0, "sphere_over_n.rib: exits cleanly (degrade, don't abort)");
    check(r.output.find("\"N\"") != std::string::npos && r.output.find("declared length 12") != std::string::npos &&
              r.output.find("supplied length 18") != std::string::npos,
          "sphere_over_n.rib: warns naming the over-long parameter and both lengths");
  }

  {
    const std::string rib = dir + "/sphere_short_n.rib";
    RunResult r = runCapturingOutput(gman, rib, 10);
    check(r.output.find("Parameter \"N\": declared length 12, supplied length 3; "
                        "clamping and zero-filling the remainder.") != std::string::npos,
          "sphere_short_n.rib: the existing under-supply warning is unchanged");
  }

  return checkSummary("GMANParameterList warns on an over-long RIB parameter array");
}
