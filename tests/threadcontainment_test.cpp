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
 * Threading lives behind one seam: libgman/gmanparallel.cpp owns thread
 * creation, and libgman/gmanlog.cpp owns one private std::mutex because a
 * worker calls warning(). This scans every other source file under the
 * directories named on its command line for a thread primitive escaping
 * that seam -- a header naming one would hand it to every caller that
 * includes it, which is exactly what the seam exists to prevent.
 *
 * Bison/flex emit their grammar and lexer sources as .yy/.ll in this tree
 * (gmansl/gmanslgrammar.yy, gmansltokenize.ll); .y/.l are scanned too in
 * case that ever changes, but nothing in the tree uses them today.
 */

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "check.h"
#include "sourcescan.h"

namespace {

namespace fs = std::filesystem;

bool isHeader(const fs::path &path) {
  const std::string ext = path.extension().string();
  return ext == ".h" || ext == ".hpp";
}

// The threading list §1 confines to gmanparallel.cpp and (for <mutex> and
// std::mutex/std::lock_guard only) gmanlog.cpp.
struct ForbiddenHeader {
  const char *include;  // as it appears between the angle brackets
};

const std::vector<ForbiddenHeader> kForbiddenHeaders = {
    {"thread"},       {"stop_token"},         {"mutex"},
    {"atomic"},       {"condition_variable"}, {"future"},
    {"shared_mutex"}, {"pthread.h"},
};

struct ForbiddenToken {
  const char *token;
};

const std::vector<ForbiddenToken> kForbiddenTokens = {
    {"std::jthread"}, {"std::thread"}, {"std::stop_token"},
    {"std::mutex"},   {"std::atomic"}, {"pthread_"},
};

// Headers this file includes that are on the forbidden list, as the
// literal "<name>" substring #include writes them with.
std::vector<std::string> forbiddenHeadersFound(const std::string &text) {
  std::vector<std::string> found;
  for (const auto &header : kForbiddenHeaders) {
    std::string needle = std::string("<") + header.include + ">";
    if (text.find(needle) != std::string::npos) {
      found.push_back(header.include);
    }
  }
  return found;
}

// Forbidden tokens this file names.
std::vector<std::string> forbiddenTokensFound(const std::string &text) {
  std::vector<std::string> found;
  for (const auto &tok : kForbiddenTokens) {
    if (text.find(tok.token) != std::string::npos) {
      found.push_back(tok.token);
    }
  }
  return found;
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

  bool sawParallelFile = false;
  bool sawLogFile = false;

  for (const fs::path &file : files) {
    const std::string text = readFile(file);
    const std::string generic = file.generic_string();
    const bool isParallelFile = pathEndsWith(file, "libgman/gmanparallel.cpp");
    const bool isLogFile = pathEndsWith(file, "libgman/gmanlog.cpp");

    if (isParallelFile) {
      sawParallelFile = true;
      continue;  // sanctioned: may include and name any of the list.
    }

    const std::vector<std::string> headers = forbiddenHeadersFound(text);
    const std::vector<std::string> tokens = forbiddenTokensFound(text);

    if (isLogFile) {
      sawLogFile = true;
      // May include <mutex> and name std::mutex/std::lock_guard, nothing
      // else from the threading list.
      for (const auto &header : headers) {
        check(header == "mutex",
              generic + ": only <mutex> is sanctioned here, not <" + header +
                  ">");
      }
      for (const auto &token : tokens) {
        check(token == "std::mutex",
              generic + ": only std::mutex is sanctioned here, not " + token);
      }
      continue;
    }

    if (isHeader(file)) {
      check(headers.empty(),
            generic + ": a header includes a threading header (<" +
                (headers.empty() ? "" : headers.front()) + ">)");
      check(tokens.empty(), generic + ": a header names a thread primitive (" +
                                (tokens.empty() ? "" : tokens.front()) + ")");
    } else {
      check(headers.empty(),
            generic + ": includes a threading header outside the seam (<" +
                (headers.empty() ? "" : headers.front()) + ">)");
      check(tokens.empty(),
            generic + ": names a thread primitive outside the seam (" +
                (tokens.empty() ? "" : tokens.front()) + ")");
    }
  }

  check(sawParallelFile, "libgman/gmanparallel.cpp was scanned");
  check(sawLogFile, "libgman/gmanlog.cpp was scanned");

  return checkSummary(
      "no thread primitive escapes gmanparallel.cpp/gmanlog.cpp's seam");
}
