/* SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Copyright (c) 2026 John Cairns <john@2ad.com>
 */

/*
 * Step 5: ReadArchive. A parent RIB that reads a child contributing
 * geometry, and a RIB that reads itself -- which must error cleanly rather
 * than hang, so this drives gman under an explicit process-level timeout
 * rather than trusting it to exit on its own.
 */

#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const std::string &what) {
  std::printf("%s: %s\n", ok ? "ok" : "FAIL", what.c_str());
  if (!ok) {
    ++failures;
  }
}

struct Result {
  bool timedOut;
  int exitStatus; // valid only if !timedOut
};

// Runs gman with a hard wall-clock deadline. A cycle that is not caught
// would otherwise hang this test (and the ctest run) forever.
Result runWithTimeout(const std::string &gman, const std::string &rib,
		      int timeoutSeconds) {
  pid_t pid = fork();
  if (pid == 0) {
    // child: silence gman's own output, this test only cares about the
    // exit status. If the redirect fails there is no way to report it from
    // here without producing the noise it was meant to suppress, so fail the
    // child instead -- 126 is distinct from the 127 exec-failure below.
    if (freopen("/dev/null", "w", stdout) == nullptr ||
	freopen("/dev/null", "w", stderr) == nullptr) {
      _exit(126);
    }
    execlp(gman.c_str(), gman.c_str(), rib.c_str(), (char *) nullptr);
    _exit(127); // exec failed
  }

  if (pid < 0) {
    return Result{false, -1};
  }

  const int pollIntervalUs = 50000;
  const int maxPolls = (timeoutSeconds * 1000000) / pollIntervalUs;
  for (int i = 0; i < maxPolls; ++i) {
    int status = 0;
    pid_t reaped = waitpid(pid, &status, WNOHANG);
    if (reaped == pid) {
      const int exitStatus = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
      return Result{false, exitStatus};
    }
    usleep(pollIntervalUs);
  }

  // Timed out: the child is treated as hung. Kill it and reap it so it does
  // not outlive this test.
  kill(pid, SIGKILL);
  int status = 0;
  waitpid(pid, &status, 0);
  return Result{true, -1};
}

} // namespace

int main(int argc, char *argv[]) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s <gman-binary> <tests/rib/archive dir>\n",
		 argv[0]);
    return 2;
  }

  const std::string gman = argv[1];
  const std::string archiveDir = argv[2];

  {
    const std::string parent = archiveDir + "/parent.rib";
    Result r = runWithTimeout(gman, parent, 20);
    check(!r.timedOut, "parent.rib: does not hang");
    check(!r.timedOut && r.exitStatus == 0,
	  "parent.rib: ReadArchive's child geometry arrives, exit 0");
  }

  {
    const std::string selfInclude = archiveDir + "/selfinclude.rib";
    Result r = runWithTimeout(gman, selfInclude, 20);
    check(!r.timedOut, "selfinclude.rib: a self-including RIB does not hang");
    check(!r.timedOut && r.exitStatus != 0,
	  "selfinclude.rib: errors cleanly instead of rendering garbage");
  }

  if (failures != 0) {
    std::printf("%d assertion(s) failed\n", failures);
    return 1;
  }

  std::printf("ReadArchive holds\n");
  return 0;
}
