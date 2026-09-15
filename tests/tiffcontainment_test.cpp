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
 * libtiff lives behind one seam: libgman/gmantiff.cpp owns the only
 * HAVE_LIBTIFF and the only <tiffio.h>. This scans every other source
 * file under the directories named on its command line for a libtiff
 * symbol escaping that seam -- a header naming one would hand it to
 * every caller that includes it, which is exactly what the seam exists
 * to prevent.
 *
 * Bison/flex emit their grammar and lexer sources as .yy/.ll in this tree
 * (gmansl/gmanslgrammar.yy, gmansltokenize.ll); .y/.l are scanned too in
 * case that ever changes, but nothing in the tree uses them today.
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

bool isIdentifierChar(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
}

// True when text names TIFF as a bare identifier -- "TIFF" preceded by a
// non-identifier character (or the start of text) and immediately
// followed by an identifier character. Matches GMANOutputTIFF's own
// TIFFOpen calls and every TIFFTAG_* constant; does not match
// GMANOutputTIFF, GMANTIFFReader or GMANTIFFWriter (preceded by a letter)
// or prose like "a TIFF file" (followed by a space).
bool bareTiffIdentifier(const std::string &text, std::string &match) {
  const std::string needle = "TIFF";
  std::string::size_type pos = 0;
  while ((pos = text.find(needle, pos)) != std::string::npos) {
    const bool precededByWord = pos > 0 && isIdentifierChar(text[pos - 1]);
    const auto after = pos + needle.size();
    const bool followedByWord =
        after < text.size() && isIdentifierChar(text[after]);
    if (!precededByWord && followedByWord) {
      auto end = after;
      while (end < text.size() && isIdentifierChar(text[end])) {
        ++end;
      }
      match = text.substr(pos, end - pos);
      return true;
    }
    ++pos;
  }
  return false;
}

// The first containment-rule match in text, or empty when there is none.
std::string firstMatch(const std::string &text) {
  if (text.find("HAVE_LIBTIFF") != std::string::npos) {
    return "HAVE_LIBTIFF";
  }
  if (text.find("<tiffio.h>") != std::string::npos) {
    return "<tiffio.h>";
  }
  if (text.find("<tiff.h>") != std::string::npos) {
    return "<tiff.h>";
  }
  if (text.find("_TIFFmalloc") != std::string::npos) {
    return "_TIFFmalloc";
  }
  if (text.find("_TIFFfree") != std::string::npos) {
    return "_TIFFfree";
  }
  std::string bare;
  if (bareTiffIdentifier(text, bare)) {
    return bare;
  }
  return "";
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

  bool sawSeamFile = false;

  for (const fs::path &file : files) {
    if (pathEndsWith(file, "libgman/gmantiff.cpp")) {
      sawSeamFile = true;
      continue;  // the one exempt file: the seam itself.
    }

    const std::string text = readFile(file);
    const std::string generic = file.generic_string();
    const std::string match = firstMatch(text);
    check(
        match.empty(),
        generic + ": names a libtiff symbol outside the seam (" + match + ")");
  }

  check(sawSeamFile, "libgman/gmantiff.cpp was scanned");

  return checkSummary("no libtiff symbol escapes libgman/gmantiff.cpp's seam");
}
