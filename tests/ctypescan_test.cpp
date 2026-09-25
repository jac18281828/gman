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
 * isalnum, isalpha, isblank, iscntrl, isdigit, isgraph, islower, isprint,
 * ispunct, isspace, isupper and isxdigit are defined only for EOF and for
 * values representable as unsigned char; passing a plain (signed, on x86
 * and ARM) char is undefined behaviour for any byte above 127. This scans
 * every source file under libgman/ and include/ for a call whose argument
 * is not static_cast<unsigned char>(...). Unlike tolower/toupper, these
 * return an int consumed as a boolean, never narrowed back into a char, so
 * no outer cast applies here.
 *
 * Neither ASan nor UBSan instruments a libc domain precondition, so this
 * source scan is the only thing that fails under plain ctest when a cast is
 * reverted. See tests/tolowerscan_test.cpp for the sibling scan covering
 * tolower and toupper.
 */

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "check.h"
#include "sourcescan.h"

namespace {

namespace fs = std::filesystem;

const std::string kInnerCast = "static_cast<unsigned char>";

const std::vector<std::string>& classifierNames() {
  static const std::vector<std::string> kNames = {"isalnum", "isalpha", "isblank", "iscntrl", "isdigit", "isgraph",
                                                  "islower", "isprint", "ispunct", "isspace", "isupper", "isxdigit"};
  return kNames;
}

// Every classifier call (qualified or not) whose argument is not prefixed
// with the static_cast<unsigned char> that avoids UB on a negative char.
std::vector<std::string> callsMissingUnsignedCharCast(const std::string& text) {
  std::vector<std::string> found;
  for (const std::string& name : classifierNames()) {
    const std::string callOpen = name + "(";
    std::string::size_type pos = 0;
    while ((pos = text.find(callOpen, pos)) != std::string::npos) {
      const std::string::size_type argStart = pos + callOpen.size();
      const bool hasInnerCast = text.compare(argStart, kInnerCast.size(), kInnerCast) == 0;
      if (!hasInnerCast) {
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
    std::cerr << "usage: " << argv[0] << " <dir>...\n";
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
    const std::vector<std::string> calls = callsMissingUnsignedCharCast(text);
    check(calls.empty(), file.generic_string() + ": a <cctype> classifier must take static_cast<unsigned char>(...)");
  }

  return checkSummary("no ctype classifier in libgman/ or include/ takes an unqualified char");
}
