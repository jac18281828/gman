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
 * Runs the gman binary out of process: fork and exec, no shell and no
 * PATH search. The child's stdin is /dev/null; its stdout and stderr are
 * captured per GMANRunOptions::capture through one pipe (the uncaptured
 * stream goes to /dev/null, so combined output keeps the order the child
 * wrote), or run in workingDirectory when that is set.
 *
 * exitStatus is WEXITSTATUS after a normal exit, else -1. A failed chdir
 * or exec in the child exits 127; a failed pipe or fork returns the
 * default result. crashed is true when a signal the runner did not send
 * ended the child.
 *
 * With timeoutSeconds > 0, one deadline measured from the fork bounds
 * both reading the pipe and waiting for exit; past it the runner sends
 * SIGKILL, reaps the child and returns timedOut true, crashed false,
 * exitStatus -1. timeoutSeconds 0 waits without a bound. Every path reaps
 * the child and closes every descriptor the runner opened.
 */

#pragma once

#include <cerrno>
#include <chrono>
#include <cstddef>
#include <string>
#include <vector>

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

struct GMANRunOptions {
  enum class Capture { combined, stdoutOnly, stderrOnly };
  Capture capture = Capture::combined;
  std::string workingDirectory; // empty: the caller's cwd
  int timeoutSeconds = 0;       // 0: no limit
};

struct GMANRunResult {
  int exitStatus = -1;
  bool crashed = false;
  bool timedOut = false;
  std::string output;
};

namespace gmanrungmandetail {

// Redirects the child's stdin to /dev/null and its stdout/stderr per
// capture, dropping the uncaptured stream on /dev/null. Async-signal-safe:
// runs between fork and exec.
inline void redirectChildStreams(int pipeWriteFd, GMANRunOptions::Capture capture) {
  int const nullIn = open("/dev/null", O_RDONLY);
  dup2(nullIn, STDIN_FILENO);
  close(nullIn);

  switch (capture) {
  case GMANRunOptions::Capture::combined:
    dup2(pipeWriteFd, STDOUT_FILENO);
    dup2(pipeWriteFd, STDERR_FILENO);
    break;
  case GMANRunOptions::Capture::stdoutOnly: {
    dup2(pipeWriteFd, STDOUT_FILENO);
    int const nullOut = open("/dev/null", O_WRONLY);
    dup2(nullOut, STDERR_FILENO);
    close(nullOut);
    break;
  }
  case GMANRunOptions::Capture::stderrOnly: {
    dup2(pipeWriteFd, STDERR_FILENO);
    int const nullOut = open("/dev/null", O_WRONLY);
    dup2(nullOut, STDOUT_FILENO);
    close(nullOut);
    break;
  }
  }
  close(pipeWriteFd);
}

// child never returns: it either execs gman or calls _exit(127).
[[noreturn]] inline void execChild(int pipeReadFd, int pipeWriteFd, std::string const& workingDirectory,
                                   GMANRunOptions::Capture capture, char* const* argv) {
  close(pipeReadFd);
  redirectChildStreams(pipeWriteFd, capture);

  if (!workingDirectory.empty() && chdir(workingDirectory.c_str()) != 0) {
    _exit(127);
  }
  execv(argv[0], argv);
  _exit(127);
}

using GMANClock = std::chrono::steady_clock;

// Reads the pipe until EOF or the deadline. Returns false on timeout.
inline bool readUntilEofOrDeadline(int pipeReadFd, GMANClock::time_point deadline, bool bounded, std::string& output) {
  char buf[4096];
  for (;;) {
    if (bounded) {
      auto const remaining = deadline - GMANClock::now();
      if (remaining <= GMANClock::duration::zero()) {
        return false;
      }
      auto const remainingMs = std::chrono::duration_cast<std::chrono::milliseconds>(remaining).count();
      pollfd pfd{pipeReadFd, POLLIN, 0};
      int const rc = poll(&pfd, 1, static_cast<int>(remainingMs) + 1);
      if (rc == 0) {
        return false;
      }
      if (rc < 0) {
        if (errno == EINTR) {
          continue;
        }
        return true;
      }
    }
    ssize_t const n = read(pipeReadFd, buf, sizeof(buf));
    if (n > 0) {
      output.append(buf, static_cast<std::size_t>(n));
      continue;
    }
    if (n == 0) {
      return true;
    }
    if (errno == EINTR) {
      continue;
    }
    return true;
  }
}

// Reaps the child, bounding the wait by the deadline when bounded.
// Returns false on timeout, leaving the child unreaped for the caller to
// kill and reap.
inline bool waitUntilExitOrDeadline(pid_t pid, GMANClock::time_point deadline, bool bounded, int& status) {
  if (!bounded) {
    waitpid(pid, &status, 0);
    return true;
  }
  for (;;) {
    pid_t const r = waitpid(pid, &status, WNOHANG);
    if (r == pid) {
      return true;
    }
    if (GMANClock::now() >= deadline) {
      return false;
    }
    usleep(1000);
  }
}

} // namespace gmanrungmandetail

inline GMANRunResult runGman(std::string const& gman, std::vector<std::string> const& args,
                             GMANRunOptions const& options = {}) {
  using namespace gmanrungmandetail;

  GMANRunResult result;

  std::vector<char*> argv;
  argv.push_back(const_cast<char*>(gman.c_str()));
  for (std::string const& arg : args) {
    argv.push_back(const_cast<char*>(arg.c_str()));
  }
  argv.push_back(nullptr);

  int pipeFds[2];
  if (pipe(pipeFds) != 0) {
    return result;
  }

  pid_t const pid = fork();
  if (pid < 0) {
    close(pipeFds[0]);
    close(pipeFds[1]);
    return result;
  }

  auto const deadline = GMANClock::now() + std::chrono::seconds(options.timeoutSeconds);
  bool const bounded = options.timeoutSeconds > 0;

  if (pid == 0) {
    execChild(pipeFds[0], pipeFds[1], options.workingDirectory, options.capture, argv.data());
  }
  close(pipeFds[1]);

  bool const readComplete = readUntilEofOrDeadline(pipeFds[0], deadline, bounded, result.output);
  close(pipeFds[0]);

  if (!readComplete) {
    kill(pid, SIGKILL);
    int status = 0;
    waitpid(pid, &status, 0);
    result.timedOut = true;
    return result;
  }

  int status = 0;
  if (!waitUntilExitOrDeadline(pid, deadline, bounded, status)) {
    kill(pid, SIGKILL);
    waitpid(pid, &status, 0);
    result.timedOut = true;
    return result;
  }

  if (WIFEXITED(status)) {
    result.exitStatus = WEXITSTATUS(status);
  } else if (WIFSIGNALED(status)) {
    result.crashed = true;
  }
  return result;
}
