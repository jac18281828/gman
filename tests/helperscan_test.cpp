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

#include <cctype>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "check.h"
#include "sourcescan.h"

namespace {

namespace fs = std::filesystem;

constexpr std::string_view kSpawnCalls[] = {"system", "popen",  "fork",  "vfork",  "execl",
                                            "execlp", "execle", "execv", "execvp", "execve"};

bool isIdentifierChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_'; }

// True when text calls name as a function: name immediately followed by
// '(', not itself preceded by an identifier character -- so "execve("
// matches "execve" but not the "execv" prefix inside it, and "vfork("
// matches "vfork" but not the "fork" suffix inside it.
bool hasCallToken(std::string const& text, std::string_view name) {
  std::size_t pos = 0;
  while ((pos = text.find(name, pos)) != std::string::npos) {
    std::size_t const end = pos + name.size();
    bool const boundedLeft = pos == 0 || !isIdentifierChar(text[pos - 1]);
    if (boundedLeft && end < text.size() && text[end] == '(') {
      return true;
    }
    ++pos;
  }
  return false;
}

// True when text calls a posix_spawn* function: the literal prefix,
// then any run of lowercase letters or underscores, then '('.
bool hasPosixSpawnCall(std::string const& text) {
  std::string_view const prefix = "posix_spawn";
  std::size_t pos = 0;
  while ((pos = text.find(prefix, pos)) != std::string::npos) {
    bool const boundedLeft = pos == 0 || !isIdentifierChar(text[pos - 1]);
    std::size_t end = pos + prefix.size();
    while (end < text.size() && (std::islower(static_cast<unsigned char>(text[end])) != 0 || text[end] == '_')) {
      ++end;
    }
    if (boundedLeft && end < text.size() && text[end] == '(') {
      return true;
    }
    ++pos;
  }
  return false;
}

bool hasProcessSpawnToken(std::string const& text) {
  if (text.find("WIFEXITED") != std::string::npos) {
    return true;
  }
  for (std::string_view const name : kSpawnCalls) {
    if (hasCallToken(text, name)) {
      return true;
    }
  }
  return hasPosixSpawnCall(text);
}

} // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "usage: " << argv[0] << " <tests-dir>\n";
    return 2;
  }

  const std::string testsDir = argv[1];
  const std::vector<std::string> dirs = {testsDir};
  const std::vector<fs::path> files = collectSourceFiles(dirs);
  check(files.size() >= 100,
        "scanned at least 100 source files under tests/ (got " + std::to_string(files.size()) + ")");

  for (fs::path const& file : files) {
    const std::string relative = fs::relative(file, testsDir).generic_string();
    const std::string text = readFile(file);

    if (relative != "rungman.h" && relative != "helperscan_test.cpp") {
      check(!hasProcessSpawnToken(text),
            file.generic_string() + ": hand-rolled process spawn; call tests/rungman.h's runGman instead");
    }

    if (relative != "maketransform.h" && relative != "helperscan_test.cpp") {
      check(text.find("GMANOneMatrix") == std::string::npos,
            file.generic_string() + ": GMANOneMatrix; call tests/maketransform.h's makeTransform instead");
    }
  }

  return checkSummary("every test spawns gman through tests/rungman.h and builds a transform through "
                      "tests/maketransform.h");
}
