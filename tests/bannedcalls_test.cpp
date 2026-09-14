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
 * No production source builds a string by hand: this scans every source
 * file under the directories named on its command line for the
 * sprintf/strcpy family, a hand-sized new char[, or PATH_MAX. A bounded
 * call into a hand-sized buffer is still a hand-sized buffer, so snprintf
 * and strncpy are banned along with their unbounded counterparts.
 * libgman/gmanribparse.cpp's duplicateCString is the one sanctioned
 * new char[ site: the RI API takes char*, so a parsed RIB string handed to
 * it needs its own heap copy.
 */

#include <cctype>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "check.h"
#include "sourcescan.h"

namespace {

namespace fs = std::filesystem;

bool pathEndsWith(const fs::path &path, const std::string &suffix) {
  const std::string generic = path.generic_string();
  return generic.size() >= suffix.size() &&
         generic.compare(generic.size() - suffix.size(), suffix.size(),
                         suffix) == 0;
}

bool isIdentChar(unsigned char c) {
  return std::isalnum(c) || c == '_';
}

// True if `identifier` occurs at text[pos] as a whole identifier --
// bounded on both sides by a non-identifier character or the string's
// edge -- immediately followed by optional whitespace and '('.
bool wholeIdentifierCall(const std::string &text, std::string::size_type pos,
                         const std::string &identifier) {
  if (pos > 0 && isIdentChar(static_cast<unsigned char>(text[pos - 1]))) {
    return false;
  }
  const std::string::size_type end = pos + identifier.size();
  if (end < text.size() && isIdentChar(static_cast<unsigned char>(text[end]))) {
    return false;
  }
  std::string::size_type i = end;
  while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) {
    ++i;
  }
  return i < text.size() && text[i] == '(';
}

// The sprintf/strcpy family §1 bans outright, whole-identifier and
// followed by '(': a bounded call into a hand-sized buffer is still a
// hand-sized buffer.
const std::vector<std::string> kBannedCalls = {
    "sprintf", "vsprintf", "snprintf", "vsnprintf", "strcpy",
    "strncpy", "strcat",   "strncat",  "gets",
};

// Every banned call identifier text uses, each reported once.
std::vector<std::string> bannedCallsFound(const std::string &text) {
  std::vector<std::string> found;
  for (const auto &name : kBannedCalls) {
    std::string::size_type pos = 0;
    while ((pos = text.find(name, pos)) != std::string::npos) {
      if (wholeIdentifierCall(text, pos, name)) {
        found.push_back(name);
        break;
      }
      ++pos;
    }
  }
  return found;
}

// Occurrences of `new` <whitespace> `char` <whitespace> `[`, both `new`
// and `char` whole identifiers, whitespace optional around `char` and `[`.
int newCharBracketCount(const std::string &text) {
  int count = 0;
  std::string::size_type pos = 0;
  while ((pos = text.find("new", pos)) != std::string::npos) {
    if (pos > 0 && isIdentChar(static_cast<unsigned char>(text[pos - 1]))) {
      ++pos;
      continue;
    }
    std::string::size_type i = pos + 3;
    if (i >= text.size() || isIdentChar(static_cast<unsigned char>(text[i]))) {
      ++pos;
      continue;
    }
    const std::string::size_type wsStart = i;
    while (i < text.size() &&
           std::isspace(static_cast<unsigned char>(text[i]))) {
      ++i;
    }
    if (i == wsStart || text.compare(i, 4, "char") != 0) {
      ++pos;
      continue;
    }
    const std::string::size_type charEnd = i + 4;
    if (charEnd < text.size() &&
        isIdentChar(static_cast<unsigned char>(text[charEnd]))) {
      ++pos;
      continue;
    }
    std::string::size_type j = charEnd;
    while (j < text.size() &&
           std::isspace(static_cast<unsigned char>(text[j]))) {
      ++j;
    }
    if (j < text.size() && text[j] == '[') {
      ++count;
    }
    pos = charEnd;
  }
  return count;
}

// True if `name` occurs anywhere in text as a whole identifier, no '('
// required.
bool containsIdentifier(const std::string &text, const std::string &name) {
  std::string::size_type pos = 0;
  while ((pos = text.find(name, pos)) != std::string::npos) {
    const bool leftOk =
        pos == 0 || !isIdentChar(static_cast<unsigned char>(text[pos - 1]));
    const std::string::size_type end = pos + name.size();
    const bool rightOk = end >= text.size() ||
                         !isIdentChar(static_cast<unsigned char>(text[end]));
    if (leftOk && rightOk) {
      return true;
    }
    ++pos;
  }
  return false;
}

}  // namespace

int main(int argc, char **argv) {
  if (argc < 2) {
    std::cerr << "usage: " << argv[0] << " <source-dir> [source-dir ...]\n";
    return 2;
  }

  std::vector<std::string> dirs;
  for (int i = 1; i < argc; ++i) {
    dirs.push_back(argv[i]);
  }

  const std::vector<fs::path> files = collectSourceFiles(dirs);
  check(files.size() > 100,
        "scanned a plausible number of source files (>100)");

  bool sawRibparse = false;

  for (const fs::path &file : files) {
    const std::string text = readFile(file);
    const std::string generic = file.generic_string();
    const bool isRibparse = pathEndsWith(file, "libgman/gmanribparse.cpp");

    const std::vector<std::string> calls = bannedCallsFound(text);
    if (calls.empty()) {
      check(true, generic + ": no sprintf/strcpy-family call");
    } else {
      for (const auto &name : calls) {
        check(false, generic + ": banned call " + name + "(");
      }
    }

    const int newCharCount = newCharBracketCount(text);
    if (isRibparse) {
      sawRibparse = true;
      check(newCharCount == 1,
            generic + ": expected exactly one sanctioned new char[, found " +
                std::to_string(newCharCount));
    } else {
      check(newCharCount == 0,
            generic +
                ": new char[ outside libgman/gmanribparse.cpp's one "
                "sanctioned site");
    }

    check(!containsIdentifier(text, "PATH_MAX"), generic + ": PATH_MAX found");
  }

  check(sawRibparse, "libgman/gmanribparse.cpp was scanned");

  return checkSummary(
      "no production source builds a string by hand outside tests/");
}
