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
 * A scene naming a texture no file carries reports the warning once per
 * world block under every renderer, however many workers shade it, and
 * prints no libtiff text; a second world block reports it again.
 */

#include <cstddef>
#include <cstdio>
#include <string>

#include "check.h"
#include "gmanparallel.h"
#include "ri.h"
#include "rungman.h"

namespace {

const RtInt kFixtureRows = 128;

const std::string kWarning = "cannot open, using opaque black";

int countOccurrences(std::string const& haystack, std::string const& needle) {
  int count = 0;
  std::size_t pos = 0;
  while ((pos = haystack.find(needle, pos)) != std::string::npos) {
    ++count;
    pos += needle.size();
  }
  return count;
}

GMANRunResult render(std::string const& gman, std::string const& renderer, std::string const& ribDir,
                     std::string const& stem) {
  return runGman(gman, {"-r", renderer, ribDir + "/" + stem + ".rib"});
}

void checkWorldBlocks(std::string const& gman, std::string const& renderer, std::string const& ribDir,
                      std::string const& stem, int worlds) {
  const std::string tag = renderer + " " + stem;
  GMANRunResult const result = render(gman, renderer, ribDir, stem);

  check(result.exitStatus == 0, tag + ": exits 0");
  check(countOccurrences(result.output, kWarning) == worlds,
        tag + ": reports the missing texture " + std::to_string(worlds) + " time(s), got " +
            std::to_string(countOccurrences(result.output, kWarning)));
  check(result.output.find("TIFFOpen") == std::string::npos, tag + ": prints no libtiff text");
}

} // namespace

int main(int argc, char* argv[]) {
  if (argc < 4) {
    std::fprintf(stderr, "usage: %s <gman-binary> <tests/rib-dir> <renderer>\n", argv[0]);
    return 2;
  }
  const std::string gman = argv[1];
  const std::string ribDir = argv[2];
  const std::string renderer = argv[3];

  check(gman::parallelWorkers(kFixtureRows, 0) > 1, "precondition: more than one worker shades a " +
                                                        std::to_string(kFixtureRows) +
                                                        "-row frame, or the per-worker warning cannot show");

  checkWorldBlocks(gman, renderer, ribDir, "texturewarningonce", 1);
  checkWorldBlocks(gman, renderer, ribDir, "texturewarningonce_frames", 2);

  return checkSummary("a missing texture warns once per world block under every renderer");
}
