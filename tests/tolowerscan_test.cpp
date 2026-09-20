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
 * undefined behaviour for any byte above 127. The idiom this scans for is
 * static_cast<char>(std::tolower(static_cast<unsigned char>(c))): the inner
 * cast avoids the UB, the outer cast avoids the implicit int-to-char
 * narrowing tolower's return then triggers. This scans every source file
 * under libgman/ for a call missing either half -- gman::InlineParse::lc was
 * the one outlier; three other call sites already spell it correctly.
 *
 * Neither ASan nor UBSan instruments a libc domain precondition, so this
 * source scan is the only thing that fails under plain ctest when a cast is
 * reverted.
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
const std::string kOuterCastQualified = "static_cast<char>(std::";
const std::string kOuterCastBare = "static_cast<char>(";

// Every "tolower(" or "toupper(" call (qualified or not) missing either the
// inner static_cast<unsigned char> that avoids UB or the outer
// static_cast<char> that avoids narrowing tolower's int return.
std::vector<std::string> callsMissingRequiredCasts(const std::string& text) {
  std::vector<std::string> found;
  for (const std::string& name : {std::string("tolower"), std::string("toupper")}) {
    const std::string callOpen = name + "(";
    std::string::size_type pos = 0;
    while ((pos = text.find(callOpen, pos)) != std::string::npos) {
      const std::string::size_type argStart = pos + callOpen.size();
      const bool hasInnerCast = text.compare(argStart, kInnerCast.size(), kInnerCast) == 0;
      const bool hasOuterCast =
          (pos >= kOuterCastQualified.size() &&
           text.compare(pos - kOuterCastQualified.size(), kOuterCastQualified.size(), kOuterCastQualified) == 0) ||
          (pos >= kOuterCastBare.size() &&
           text.compare(pos - kOuterCastBare.size(), kOuterCastBare.size(), kOuterCastBare) == 0);
      if (!hasInnerCast || !hasOuterCast) {
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
    const std::vector<std::string> calls = callsMissingRequiredCasts(text);
    check(calls.empty(), file.generic_string() + ": tolower/toupper must be static_cast<char>(std::tolower("
                                                 "static_cast<unsigned char>(...)))");
  }

  return checkSummary("no tolower/toupper in libgman/ takes an unqualified char");
}
