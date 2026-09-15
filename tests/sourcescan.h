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
 * The recursive source-file scan threadcontainment_test.cpp needs: read
 * a file whole, and walk a set of directories for files with a source
 * extension.
 *
 * Bison/flex emit their grammar and lexer sources as .yy/.ll in this tree
 * (gmansl/gmanslgrammar.yy, gmansltokenize.ll); .y/.l are scanned too in
 * case that ever changes, but nothing in the tree uses them today.
 */

#ifndef GMAN_TESTS_SOURCESCAN_H
#define GMAN_TESTS_SOURCESCAN_H

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

inline std::string readFile(const std::filesystem::path &path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream contents;
  contents << in.rdbuf();
  return contents.str();
}

inline bool pathEndsWith(const std::filesystem::path &path,
                         const std::string &suffix) {
  const std::string generic = path.generic_string();
  return generic.size() >= suffix.size() &&
         generic.compare(generic.size() - suffix.size(), suffix.size(),
                         suffix) == 0;
}

inline bool hasSourceExtension(const std::filesystem::path &path) {
  static const std::vector<std::string> kExtensions = {
      ".h", ".hpp", ".c", ".cpp", ".y", ".l", ".yy", ".ll"};
  const std::string ext = path.extension().string();
  return std::find(kExtensions.begin(), kExtensions.end(), ext) !=
         kExtensions.end();
}

inline std::vector<std::filesystem::path> collectSourceFiles(
    const std::vector<std::string> &dirs) {
  namespace fs = std::filesystem;
  std::vector<fs::path> files;
  for (const auto &dir : dirs) {
    if (!fs::exists(dir)) {
      continue;
    }
    for (const auto &entry : fs::recursive_directory_iterator(dir)) {
      if (entry.is_regular_file() && hasSourceExtension(entry.path())) {
        files.push_back(entry.path());
      }
    }
  }
  return files;
}

#endif
