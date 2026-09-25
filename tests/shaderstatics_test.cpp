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
 * A shader holds no state below file scope: no `static` at brace depth
 * above zero (a function-local or class-member static) and no `mutable`
 * anywhere. Scans every .cpp/.h under the directory named on the command
 * line, comments and string/char literals ignored first so a "static" or
 * "mutable" inside either never counts. A `namespace` or `extern "C"`
 * linkage block does not itself add depth: a file-scope static sitting
 * inside one, such as each plugin's own loadableInfo, still passes.
 */

#include <cctype>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "check.h"
#include "sourcescan.h"

namespace {

// Comments become nothing; a string or char literal's quotes and content
// become one space, so neither can hide a "static"/"mutable" token nor
// contribute a stray brace to the depth count below. Newlines inside a
// block comment are kept so line numbers in a failure message stay useful.
std::string stripCommentsAndLiterals(std::string const& text) {
  std::string out;
  out.reserve(text.size());
  std::size_t i = 0;
  std::size_t const n = text.size();
  while (i < n) {
    char const c = text[i];
    if (c == '/' && i + 1 < n && text[i + 1] == '/') {
      while (i < n && text[i] != '\n') {
        ++i;
      }
      continue;
    }
    if (c == '/' && i + 1 < n && text[i + 1] == '*') {
      i += 2;
      while (i + 1 < n && !(text[i] == '*' && text[i + 1] == '/')) {
        if (text[i] == '\n') {
          out.push_back('\n');
        }
        ++i;
      }
      i = (i + 1 < n) ? i + 2 : n;
      continue;
    }
    if (c == '"' || c == '\'') {
      char const quote = c;
      out.push_back(' ');
      ++i;
      while (i < n && text[i] != quote) {
        if (text[i] == '\\' && i + 1 < n) {
          ++i;
        }
        ++i;
      }
      if (i < n) {
        ++i;
      }
      continue;
    }
    out.push_back(c);
    ++i;
  }
  return out;
}

std::string trimmed(std::string const& s) {
  std::size_t const begin = s.find_first_not_of(" \t\r\n");
  if (begin == std::string::npos) {
    return std::string();
  }
  std::size_t const end = s.find_last_not_of(" \t\r\n");
  return s.substr(begin, end - begin + 1);
}

bool isIdentifier(std::string const& s) {
  if (s.empty() || (!std::isalpha((unsigned char)s[0]) && s[0] != '_')) {
    return false;
  }
  for (char c : s) {
    if (!std::isalnum((unsigned char)c) && c != '_') {
      return false;
    }
  }
  return true;
}

struct Violation {
  int line;
  std::string kind;
};

// True when the text since the last `;`/`{`/`}` opens a namespace or an
// `extern "C"` linkage block -- a scope that does not count toward depth.
bool opensExemptScope(std::string const& sinceBoundary) {
  std::string const t = trimmed(sinceBoundary);
  if (t == "extern") {
    return true;
  }
  if (t.rfind("namespace", 0) == 0) {
    std::string const rest = trimmed(t.substr(std::string("namespace").size()));
    return rest.empty() || isIdentifier(rest);
  }
  return false;
}

std::vector<Violation> findViolations(std::string const& source) {
  std::string const cleaned = stripCommentsAndLiterals(source);
  std::vector<Violation> violations;

  int depth = 0;
  std::vector<bool> exemptStack;
  std::string sinceBoundary;
  std::string word;
  int line = 1;

  auto flushWord = [&]() {
    if (word == "mutable") {
      violations.push_back({line, word});
    } else if (word == "static" && depth > 0) {
      violations.push_back({line, word});
    }
    word.clear();
  };

  for (char const c : cleaned) {
    if (c == '\n') {
      ++line;
    }
    if (std::isalnum((unsigned char)c) || c == '_') {
      word.push_back(c);
      sinceBoundary.push_back(c);
      continue;
    }
    flushWord();

    if (c == '{') {
      bool const exempt = opensExemptScope(sinceBoundary);
      exemptStack.push_back(exempt);
      if (!exempt) {
        ++depth;
      }
      sinceBoundary.clear();
    } else if (c == '}') {
      if (!exemptStack.empty()) {
        bool const wasExempt = exemptStack.back();
        exemptStack.pop_back();
        if (!wasExempt && depth > 0) {
          --depth;
        }
      }
      sinceBoundary.clear();
    } else if (c == ';') {
      sinceBoundary.clear();
    } else {
      sinceBoundary.push_back(c);
    }
  }
  flushWord();
  return violations;
}

} // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "usage: " << argv[0] << " <shaders-dir>\n";
    return 2;
  }

  std::vector<std::string> dirs;
  for (int i = 1; i < argc; ++i) {
    dirs.push_back(argv[i]);
  }

  std::vector<std::filesystem::path> const files = collectSourceFiles(dirs);
  check(files.size() >= 8, "scanned at least eight files");

  for (std::filesystem::path const& file : files) {
    std::string const generic = file.generic_string();
    std::vector<Violation> const violations = findViolations(readFile(file));
    if (violations.empty()) {
      check(true, generic + ": no static above file scope and no mutable");
      continue;
    }
    for (Violation const& violation : violations) {
      std::string const what = (violation.kind == "static") ? "static above file scope" : "mutable";
      check(false, generic + ":" + std::to_string(violation.line) + ": a " + what);
    }
  }

  return checkSummary("shaders hold no static above file scope and no mutable");
}
