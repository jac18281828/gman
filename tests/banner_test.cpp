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
 * gman's opening banner: one line, "gman <version> -- LGPL-2.1-or-later,
 * see COPYING", in place of the four-line license recitation GMANLog used
 * to log. Pins that the banner is stdout's first line and that neither the
 * old notice's "redistribute" nor its Pixar attribution survives.
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

// Spawns argv via fork/exec/waitpid, the technique tests/filedrivers_test.cpp
// uses, capturing stdout.
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

// stdout's first line, without its terminating newline.
std::string firstLine(std::string const& text) {
  const auto eol = text.find('\n');
  return text.substr(0, eol);
}

} // namespace

int main(int argc, char* argv[]) {
  if (argc != 4) {
    std::fprintf(stderr, "usage: %s <gman-binary> <sphere.rib> <version>\n", argv[0]);
    return 2;
  }
  const std::string gman = argv[1];
  const std::string rib = argv[2];
  const std::string expected = "gman " + std::string(argv[3]) + " -- LGPL-2.1-or-later, see COPYING";

  const auto run = runCaptured({gman, rib});
  check(run.exitStatus == 0, "gman exits 0");
  check(firstLine(run.stdoutText) == expected, "gman's first stdout line is the banner (\"" + expected + "\")");
  check(run.stdoutText.find("redistribute") == std::string::npos,
        "the retired license recitation's \"redistribute\" is gone");
  check(run.stdoutText.find("Pixar") == std::string::npos,
        "the retired license recitation's Pixar attribution is gone");

  return checkSummary("gman's banner names its version and license, nothing more");
}
