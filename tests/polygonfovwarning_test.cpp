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
 * GMANRenderManImpl::RiWorldBegin resolves fov once, before building
 * either projection branch, and warns there when it was absent or
 * explicitly zero. The z-buffer polygon dicer resolves the same inputs
 * per getRSPolygon/getRSGeneralPolygon/getRSPointsPolygon/
 * getRSPointsGeneralPolygons call, silently, through the very same
 * GMANOptions -- were the warning to live in that shared resolver instead
 * of staying in RiWorldBegin, it would fire once per polygon rather than
 * once per RiWorldBegin. tests/rib/polygonfov.rib's two Polygon requests
 * under one RiWorldBegin, no "fov" supplied, are what tells the two
 * apart.
 */

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

#include "check.h"

namespace {

int runGman(const std::string& gman, const std::string& rib, const std::string& outPath) {
  const std::string command = "\"" + gman + "\" \"" + rib + "\" > \"" + outPath + "\" 2>&1";
  int status = std::system(command.c_str());
  return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

int countOccurrences(const std::string& haystack, const std::string& needle) {
  int count = 0;
  std::size_t pos = 0;
  while ((pos = haystack.find(needle, pos)) != std::string::npos) {
    ++count;
    pos += needle.size();
  }
  return count;
}

} // namespace

int main(int argc, char* argv[]) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s <gman-binary> <tests/rib-dir>\n", argv[0]);
    return 2;
  }
  const std::string gman = argv[1];
  const std::string ribDir = argv[2];

  const std::string outPath = "polygonfov.out";
  check(runGman(gman, ribDir + "/polygonfov.rib", outPath) == 0, "polygonfov.rib renders");

  std::ifstream in(outPath);
  std::ostringstream contents;
  contents << in.rdbuf();

  const int occurrences = countOccurrences(contents.str(), "FOV not set");
  check(occurrences == 1, "\"FOV not set\" prints exactly once for two Polygon requests under one "
                          "RiWorldBegin (got " +
                              std::to_string(occurrences) + ")");

  return checkSummary("polygonfovwarning holds");
}
