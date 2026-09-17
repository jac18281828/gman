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
 * gman --version's contract: "gman <version>" then a "drivers:" line
 * listing gmanFileDrivers(), space-separated, in table order. CMake
 * computes the expected list for this build (tests/CMakeLists.txt) and
 * passes it on the command line, so this test never re-derives which
 * options were enabled -- it checks the program's own output against
 * what the build actually compiled.
 */

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include <sys/wait.h>
#include <unistd.h>

#include "check.h"

namespace {

struct CapturedRun {
  int exitStatus = -1;
  std::string stdoutText;
};

// Spawns argv via fork/exec/waitpid, the technique
// tests/ribmalformed_test.cpp uses, capturing stdout. --version's output
// is two short lines, well under a pipe's buffer, so reading it after the
// child exits cannot deadlock.
CapturedRun runCaptured(std::vector<std::string> const& argv) {
  CapturedRun result;
  int outPipe[2];
  if (pipe(outPipe) != 0) {
    return result;
  }

  pid_t pid = fork();
  if (pid < 0) {
    close(outPipe[0]);
    close(outPipe[1]);
    return result;
  }
  if (pid == 0) {
    close(outPipe[0]);
    dup2(outPipe[1], STDOUT_FILENO);
    close(outPipe[1]);
    std::vector<char*> cargv;
    for (auto const& arg : argv) {
      cargv.push_back(const_cast<char*>(arg.c_str()));
    }
    cargv.push_back(nullptr);
    execv(cargv[0], cargv.data());
    _exit(127);
  }

  close(outPipe[1]);
  char buffer[4096];
  ssize_t bytesRead;
  while ((bytesRead = read(outPipe[0], buffer, sizeof buffer)) > 0) {
    result.stdoutText.append(buffer, size_t(bytesRead));
  }
  close(outPipe[0]);

  int status = 0;
  waitpid(pid, &status, 0);
  result.exitStatus = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
  return result;
}

// The line beginning "drivers:" among captured lines, or "" if absent.
std::string driversLine(std::string const& text) {
  std::size_t pos = 0;
  while (pos < text.size()) {
    const auto eol = text.find('\n', pos);
    const auto line = text.substr(pos, eol - pos);
    if (line.rfind("drivers:", 0) == 0) {
      return line;
    }
    if (eol == std::string::npos) {
      break;
    }
    pos = eol + 1;
  }
  return "";
}

} // namespace

int main(int argc, char* argv[]) {
  if (argc != 3) {
    std::fprintf(stderr, "usage: %s <gman-binary> <expected driver list>\n", argv[0]);
    return 2;
  }
  const std::string gman = argv[1];
  const std::string expected = "drivers: " + std::string(argv[2]);

  const auto run = runCaptured({gman, "--version"});
  check(run.exitStatus == 0, "gman --version exits 0");
  check(run.stdoutText.rfind("gman ", 0) == 0, "gman --version's first line names the program");
  check(driversLine(run.stdoutText) == expected,
        "gman --version's drivers: line matches this build (\"" + expected + "\")");

  return checkSummary("gman --version reports this build's file drivers");
}
