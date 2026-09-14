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
 * gman -l names its log by replacing a path's trailing ".xxx" with ".log",
 * or appending ".log" when the path has no such suffix. Exercises every rib
 * name length the rule distinguishes, run out of a scratch directory rather
 * than the checked-in tree.
 */

#include <sys/wait.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "check.h"

namespace {

int runGmanLog(const std::string &gman, const std::string &ribPath,
               const std::string &workdir) {
  const std::string command = "cd \"" + workdir + "\" && \"" + gman +
                              "\" -l \"" + ribPath + "\" > run.log 2>&1";
  const int status = std::system(command.c_str());
  return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

std::string slurp(const std::string &path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

}  // namespace

int main(int argc, char *argv[]) {
  if (argc < 4) {
    std::fprintf(
        stderr,
        "usage: %s <gman-binary> <tests/rib/sphere.rib> <scratch dir>\n",
        argv[0]);
    return 2;
  }

  const std::string gman = argv[1];
  const std::string sphereRib = argv[2];
  const std::string scratch = argv[3];

  std::error_code ec;
  std::filesystem::create_directories(scratch, ec);
  if (ec) {
    std::fprintf(stderr, "cannot create %s: %s\n", scratch.c_str(),
                 ec.message().c_str());
    return 2;
  }

  const std::string sphereContents = slurp(sphereRib);

  struct Case {
    const char *name;
    const char *logName;
  };
  const Case cases[] = {
      {"ab", "ab.log"},
      {"scene", "scene.log"},
      {"sphere.rib", "sphere.log"},
      {"sphere.ribx", "sphere.ribx.log"},
  };

  for (const Case &c : cases) {
    const std::string ribPath = scratch + "/" + c.name;
    const std::string logPath = scratch + "/" + c.logName;

    std::ofstream out(ribPath, std::ios::binary);
    out << sphereContents;
    out.close();

    std::filesystem::remove(logPath, ec);

    // Relative: the runner already cd's into scratch, and an absolute
    // ribPath would push every name past the 4-character branch this test
    // means to exercise.
    const int exitCode = runGmanLog(gman, c.name, scratch);
    check(exitCode == 0, std::string(c.name) + ": gman -l exits 0");

    check(std::filesystem::exists(logPath),
          std::string(c.name) + ": " + c.logName + " exists");

    const std::string logContents = slurp(logPath);
    check(
        logContents.find("Setting log:") != std::string::npos,
        std::string(c.name) + ": " + c.logName + " contains \"Setting log:\"");
  }

  return checkSummary(
      "gman -l names its log file correctly for every path length");
}
