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
 * tolower and toupper are defined only for EOF and for values representable
 * as unsigned char; passing a plain (signed, on x86 and ARM) char is
 * undefined behaviour for any byte above 127. This scans every source file
 * under libgman/ for a call that skips the static_cast<unsigned char> cast
 * the idiom requires -- gman::InlineParse::lc was the one outlier; three
 * other call sites already spell it correctly.
 *
 * Neither ASan nor UBSan instruments a libc domain precondition, so this
 * source scan is the only thing that fails under plain ctest when the cast
 * is reverted.
 */

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "check.h"
#include "sourcescan.h"

namespace {

namespace fs = std::filesystem;

const std::string kGuardedPrefix = "static_cast<unsigned char>";

// Every "tolower(" or "toupper(" call (qualified or not) whose argument
// does not open with the sanctioned unsigned-char cast.
std::vector<std::string> unqualifiedCalls(const std::string& text) {
  std::vector<std::string> found;
  for (const std::string& name : {std::string("tolower"), std::string("toupper")}) {
    const std::string callOpen = name + "(";
    std::string::size_type pos = 0;
    while ((pos = text.find(callOpen, pos)) != std::string::npos) {
      const std::string::size_type argStart = pos + callOpen.size();
      if (text.compare(argStart, kGuardedPrefix.size(), kGuardedPrefix) != 0) {
        found.push_back(name);
      }
      pos = argStart;
    }
  }
  return found;
}

} // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "usage: " << argv[0] << " <libgman-dir>\n";
    return 2;
  }

  std::vector<std::string> dirs;
  for (int i = 1; i < argc; ++i) {
    dirs.push_back(argv[i]);
  }

  const std::vector<fs::path> files = collectSourceFiles(dirs);
  check(files.size() > 10, "scanned a plausible number of source files (>10)");

  for (const fs::path& file : files) {
    const std::string text = readFile(file);
    const std::vector<std::string> calls = unqualifiedCalls(text);
    check(calls.empty(),
          file.generic_string() + ": tolower/toupper takes static_cast<unsigned char>(...), not a plain char");
  }

  return checkSummary("no tolower/toupper in libgman/ takes an unqualified char");
}
