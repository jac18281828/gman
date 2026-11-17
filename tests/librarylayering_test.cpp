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
 * Each library includes headers of its own library and the libraries below
 * it, never one above. Every scanned file belongs to the row of the longest
 * directory prefix that matches whole path components in kRows; the row
 * names the libraries the file may include and the export macro it may
 * name. Checking direct includes suffices: when every direct edge points
 * downward, every transitive one does.
 *
 * A quoted include resolves by header basename, since production headers
 * carry no directory component; kGeneratedHeaders names the headers the
 * build writes and no tracked file supplies. A header outside every
 * include/ directory is private to its library, and kPrivateHeaderExceptions
 * names the one include that crosses that line. tests/ is not scanned.
 */

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iostream>
#include <map>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#include "check.h"
#include "sourcescan.h"

namespace {

namespace fs = std::filesystem;

struct LibraryRow {
  std::string prefix;
  std::string library;
  std::vector<std::string> mayInclude;
  std::string exportMacro;
};

const std::vector<LibraryRow> kRows = {
    {"include", "core", {"core"}, "GMAN_EXPORT"},
    {"libgman", "core", {"core"}, "GMAN_EXPORT"},
    {"libgmanray", "ray", {"ray", "core"}, "GMANRAY_EXPORT"},
    {"libgmanraster", "raster", {"raster", "core"}, "GMANRASTER_EXPORT"},
    {"renderers/zbuffer", "renderers/zbuffer", {"renderers/zbuffer", "raster", "core"}, "GMAN_EXPORT"},
    {"renderers/reyes", "renderers/reyes", {"renderers/reyes", "raster", "core"}, "GMAN_EXPORT"},
    {"renderers/raytracer", "renderers/raytracer", {"renderers/raytracer", "ray", "core"}, "GMAN_EXPORT"},
    {"renderers/pathtracer", "renderers/pathtracer", {"renderers/pathtracer", "ray", "core"}, "GMAN_EXPORT"},
    {"renderers/radiosity", "renderers/radiosity", {"renderers/radiosity", "ray", "core"}, "GMAN_EXPORT"},
    {"shaders", "shaders", {"shaders", "core"}, "GMAN_EXPORT"},
    {"libgmanrib", "libgmanrib", {"libgmanrib", "core"}, "GMAN_EXPORT"},
    {"gman", "gman", {"gman", "core"}, "GMAN_EXPORT"},
    {"gmansl", "gmansl", {"gmansl", "core"}, "GMAN_EXPORT"},
};

struct GeneratedHeader {
  std::string name;
  std::string library;
};

const std::vector<GeneratedHeader> kGeneratedHeaders = {
    {"gman/export.h", "core"},
    {"gman/rayexport.h", "ray"},
    {"gman/rasterexport.h", "raster"},
    {"gmanslgrammar.h", "gmansl"},
};

struct PrivateHeaderException {
  std::string includer;
  std::string header;
};

const std::vector<PrivateHeaderException> kPrivateHeaderExceptions = {
    {"gman/gman.cpp", "libgman/gmanfiledrivers.h"},
};

const std::vector<std::string> kExportMacros = {"GMAN_EXPORT", "GMANRAY_EXPORT", "GMANRASTER_EXPORT"};

const std::string kPublicDirectory = "include";
const std::size_t kMinimumFiles = 200;

bool hasPrefix(std::string const& path, std::string const& prefix) {
  return path.size() > prefix.size() && path.compare(0, prefix.size(), prefix) == 0 && path[prefix.size()] == '/';
}

LibraryRow const* rowOf(std::string const& relative) {
  LibraryRow const* best = nullptr;
  for (LibraryRow const& row : kRows) {
    if (hasPrefix(relative, row.prefix) && (best == nullptr || row.prefix.size() > best->prefix.size())) {
      best = &row;
    }
  }
  return best;
}

bool isHeader(fs::path const& path) {
  const std::string ext = path.extension().string();
  return ext == ".h" || ext == ".hpp";
}

bool isPrivateHeader(std::string const& relative) {
  const fs::path path(relative);
  return std::none_of(path.begin(), path.end(), [](fs::path const& part) { return part == kPublicDirectory; });
}

std::vector<std::string> quotedIncludes(std::string const& text) {
  static const std::regex kInclude(R"re(^[ \t]*#[ \t]*include[ \t]*"([^"]+)")re");
  std::vector<std::string> names;
  std::istringstream lines(text);
  std::string line;
  while (std::getline(lines, line)) {
    std::smatch match;
    if (std::regex_search(line, match, kInclude)) {
      names.push_back(match[1].str());
    }
  }
  return names;
}

bool isIdentifierChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_'; }

bool namesWord(std::string const& text, std::string const& word) {
  std::string::size_type pos = 0;
  while ((pos = text.find(word, pos)) != std::string::npos) {
    const bool startsWord = pos == 0 || !isIdentifierChar(text[pos - 1]);
    const std::string::size_type end = pos + word.size();
    const bool endsWord = end == text.size() || !isIdentifierChar(text[end]);
    if (startsWord && endsWord) {
      return true;
    }
    pos = end;
  }
  return false;
}

bool isException(std::string const& includer, std::string const& header) {
  return std::any_of(kPrivateHeaderExceptions.begin(), kPrivateHeaderExceptions.end(),
                     [&](PrivateHeaderException const& e) { return e.includer == includer && e.header == header; });
}

struct ResolvedHeader {
  std::string path;
  std::string library;
  bool isPrivate = false;
};

} // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    std::cerr << "usage: " << argv[0] << " <source-root> <source-dir>...\n";
    return 2;
  }

  const fs::path root = argv[1];
  std::vector<std::string> dirs;
  for (int i = 2; i < argc; ++i) {
    dirs.push_back(argv[i]);
  }

  const std::vector<fs::path> files = collectSourceFiles(dirs);
  check(files.size() > kMinimumFiles,
        "scanned a plausible number of source files (>" + std::to_string(kMinimumFiles) + ")");

  std::vector<std::string> relatives;
  std::map<std::string, std::vector<std::string>> headersByName;
  for (fs::path const& file : files) {
    const std::string relative = file.lexically_relative(root).generic_string();
    relatives.push_back(relative);
    if (isHeader(file)) {
      headersByName[file.filename().string()].push_back(relative);
    }
  }

  for (auto const& [name, paths] : headersByName) {
    std::string listed;
    for (std::string const& path : paths) {
      listed += " " + path;
    }
    check(paths.size() == 1, "header basename " + name + " is unique:" + listed);
  }

  auto resolve = [&](std::string const& quoted) -> ResolvedHeader {
    for (GeneratedHeader const& generated : kGeneratedHeaders) {
      if (generated.name == quoted) {
        return {quoted, generated.library, false};
      }
    }
    const auto found = headersByName.find(quoted);
    if (found == headersByName.end() || found->second.size() != 1) {
      return {};
    }
    const std::string& path = found->second.front();
    LibraryRow const* row = rowOf(path);
    return {path, row == nullptr ? std::string() : row->library, isPrivateHeader(path)};
  };

  for (std::size_t i = 0; i < files.size(); ++i) {
    const std::string& relative = relatives[i];
    LibraryRow const* row = rowOf(relative);
    if (row == nullptr) {
      check(false, relative + ": no library row covers this directory; place it in kRows");
      continue;
    }

    const std::string text = readFile(files[i]);
    bool clean = true;

    for (std::string const& quoted : quotedIncludes(text)) {
      const ResolvedHeader header = resolve(quoted);
      if (header.path.empty() || header.library.empty()) {
        check(false, relative + ": includes \"" + quoted + "\", which resolves to no production header");
        clean = false;
        continue;
      }
      const bool allowed =
          std::find(row->mayInclude.begin(), row->mayInclude.end(), header.library) != row->mayInclude.end();
      if (!allowed) {
        check(false, relative + ": includes \"" + quoted + "\" of library " + header.library + ", which " +
                         row->library + " may not include");
        clean = false;
      }
      if (header.isPrivate && header.library != row->library && !isException(relative, header.path)) {
        check(false, relative + ": includes \"" + quoted + "\", a private header of " + header.library);
        clean = false;
      }
    }

    for (std::string const& macro : kExportMacros) {
      if (macro != row->exportMacro && namesWord(text, macro)) {
        check(false, relative + ": names " + macro + ", but " + row->library + " names " + row->exportMacro);
        clean = false;
      }
    }

    if (clean) {
      check(true, relative + ": includes and names only what " + row->library + " may");
    }
  }

  return checkSummary("every include points down a layer");
}
