/* SPDX-License-Identifier: LGPL-2.1-or-later */

/* This is part of GMAN, a RenderMan-compatible renderer.
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
 * Scans every source file under tests/ for a hand-rolled process spawn
 * (system, popen, fork, vfork, an exec variant, posix_spawn or
 * WIFEXITED) outside tests/rungman.h, or a direct use of GMANOneMatrix
 * outside tests/maketransform.h: both belong behind their one shared
 * header. Each failure names the offending file and the header to call
 * instead.
 */

#include <filesystem>
#include <iostream>
#include <regex>
#include <string>
#include <vector>

#include "check.h"
#include "sourcescan.h"

namespace {

namespace fs = std::filesystem;

// (^|[^A-Za-z0-9_]) in a line-oriented grep is a word boundary at one
// end; \b serves the same purpose scanning a whole file's text, and also
// catches a call that opens a line with nothing before it.
const std::regex kProcessSpawn(
    R"(\b(system|popen|fork|vfork|execl|execlp|execle|execv|execvp|execve|posix_spawn[a-z_]*)\(|WIFEXITED)");

} // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "usage: " << argv[0] << " <tests-dir>\n";
    return 2;
  }

  const std::vector<std::string> dirs = {argv[1]};
  const std::vector<fs::path> files = collectSourceFiles(dirs);
  check(files.size() >= 100,
        "scanned at least 100 source files under tests/ (got " + std::to_string(files.size()) + ")");

  for (const fs::path& file : files) {
    const std::string name = file.filename().string();
    const std::string text = readFile(file);

    if (name != "rungman.h" && name != "helperscan_test.cpp") {
      check(!std::regex_search(text, kProcessSpawn),
            file.generic_string() + ": hand-rolled process spawn; call tests/rungman.h's runGman instead");
    }

    if (name != "maketransform.h" && name != "helperscan_test.cpp") {
      check(text.find("GMANOneMatrix") == std::string::npos,
            file.generic_string() + ": GMANOneMatrix; call tests/maketransform.h's makeTransform instead");
    }
  }

  return checkSummary("every test spawns gman through tests/rungman.h and builds a transform through "
                      "tests/maketransform.h");
}
