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
 * An unrecognized command-line flag is a usage error: gman names the flag,
 * prints usage(), and exits nonzero without parsing any file.
 */

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

#include <sys/wait.h>

#include "check.h"

namespace {

int runGman(const std::string& command) {
  int status = std::system(command.c_str());
  return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

// Reads the whole file at path, or "" if it cannot be opened.
std::string readFile(const std::string& path) {
  std::FILE* f = std::fopen(path.c_str(), "r");
  if (!f) {
    return "";
  }
  std::string output;
  char buf[4096];
  size_t n;
  while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) {
    output.append(buf, n);
  }
  std::fclose(f);
  return output;
}

} // namespace

int main(int argc, char* argv[]) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s <gman-binary> <sphere.rib>\n", argv[0]);
    return 2;
  }
  std::string const gman = argv[1];
  std::string const rib = argv[2];
  std::string const stderrPath = "unknownflag_stderr.txt";

  std::filesystem::remove("sphere.tif");
  int const badFlagExit = runGman("\"" + gman + "\" -x \"" + rib + "\" 2>\"" + stderrPath + "\" >/dev/null");
  std::string const stderrOutput = readFile(stderrPath);
  check(badFlagExit != 0, "gman -x exits nonzero");
  check(!std::filesystem::exists("sphere.tif"), "gman -x writes no sphere.tif -- no file was parsed");
  check(stderrOutput.find("-x") != std::string::npos, "stderr names the flag as invoked");
  check(stderrOutput.find("Parse RIB input files.") != std::string::npos,
        "stderr shows a fragment of usage()'s fixed text");

  // Control: the same file with no bad flag still renders.
  std::filesystem::remove("sphere.tif");
  int const controlExit = runGman("\"" + gman + "\" \"" + rib + "\" >/dev/null 2>&1");
  check(controlExit == 0, "gman with no bad flag exits 0");
  check(std::filesystem::exists("sphere.tif"),
        "gman with no bad flag renders sphere.tif -- the fix touches only the unrecognized-flag path");

  return checkSummary("gman rejects an unrecognized command-line flag");
}
