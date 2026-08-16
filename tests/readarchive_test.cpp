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
#include <fstream>
#include <sstream>
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
  std::string output;
};

std::string slurp(const std::string &path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

// Runs gman under -d with a hard wall-clock deadline. A cycle that is not
// caught would otherwise hang this test (and the ctest run) forever.
//
// The child's output goes to logPath rather than /dev/null because an exit
// status alone cannot distinguish "the archive was read" from "the archive
// was ignored": a ReadArchive that silently does nothing still exits 0,
// having rendered an empty world. The caller inspects the -d token trace to
// prove the child's geometry actually arrived.
Result runWithTimeout(const std::string &gman, const std::string &rib,
		      int timeoutSeconds, const std::string &logPath) {
  pid_t pid = fork();
  if (pid == 0) {
    // child: gman's own output goes to the log, not this test's stdout. If
    // the redirect fails there is no way to report it from here without
    // producing the noise it was meant to suppress, so fail the child
    // instead -- 126 is distinct from the 127 exec-failure below.
    if (freopen(logPath.c_str(), "w", stdout) == nullptr ||
	dup2(fileno(stdout), fileno(stderr)) < 0) {
      _exit(126);
    }
    execlp(gman.c_str(), gman.c_str(), "-d", rib.c_str(), (char *) nullptr);
    _exit(127); // exec failed
  }

  if (pid < 0) {
    return Result{false, -1, ""};
  }

  const int pollIntervalUs = 50000;
  const int maxPolls = (timeoutSeconds * 1000000) / pollIntervalUs;
  for (int i = 0; i < maxPolls; ++i) {
    int status = 0;
    pid_t reaped = waitpid(pid, &status, WNOHANG);
    if (reaped == pid) {
      const int exitStatus = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
      return Result{false, exitStatus, slurp(logPath)};
    }
    usleep(pollIntervalUs);
  }

  // Timed out: the child is treated as hung. Kill it and reap it so it does
  // not outlive this test.
  kill(pid, SIGKILL);
  int status = 0;
  waitpid(pid, &status, 0);
  return Result{true, -1, slurp(logPath)};
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
    Result r = runWithTimeout(gman, parent, 20, "parent.log");
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
    Result r = runWithTimeout(gman, selfInclude, 20, "selfinclude.log");
    check(!r.timedOut, "selfinclude.rib: a self-including RIB does not hang");
    check(!r.timedOut && r.exitStatus != 0,
	  "selfinclude.rib: errors cleanly instead of rendering garbage");
    check(r.output.find("ReadArchive") != std::string::npos ||
	  r.output.find("archive") != std::string::npos,
	  "selfinclude.rib: the diagnostic names the archive cycle");
  }

  if (failures != 0) {
    std::printf("%d assertion(s) failed\n", failures);
    return 1;
  }

  std::printf("ReadArchive holds\n");
  return 0;
}
