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
 * Option "statistics" "integer progress" [1] makes a tracer log one line per
 * whole percent of rows finished, ending at 100%; the same scene without it
 * logs none; and the option leaves the image byte for byte as it was.
 */

#include <cstddef>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <sys/stat.h>

#include "check.h"
#include "rungman.h"

namespace {

constexpr int kRows = 48;
const std::string kLastLine = "100% (48/48 rows)";
const std::string kProgressMark = " rows), ";

std::vector<int> percentsIn(std::string const& log) {
  std::vector<int> percents;
  std::size_t lineStart = 0;
  while (lineStart < log.size()) {
    std::size_t lineEnd = log.find('\n', lineStart);
    if (lineEnd == std::string::npos) {
      lineEnd = log.size();
    }
    const std::string line = log.substr(lineStart, lineEnd - lineStart);
    lineStart = lineEnd + 1;
    const std::size_t mark = line.find("% (");
    if (mark == std::string::npos || line.find(kProgressMark) == std::string::npos) {
      continue;
    }
    std::size_t digits = mark;
    while (digits > 0 && line[digits - 1] >= '0' && line[digits - 1] <= '9') {
      --digits;
    }
    percents.push_back(std::stoi(line.substr(digits, mark - digits)));
  }
  return percents;
}

std::vector<int> wholePercentsOfRows() {
  std::vector<int> percents;
  for (int row = 1; row <= kRows; ++row) {
    percents.push_back(row * 100 / kRows);
  }
  return percents;
}

std::string slurp(std::string const& path) {
  std::ifstream in(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

GMANRunResult renderIn(std::string const& gman, std::string const& renderer, std::string const& rib,
                       std::string const& workdir) {
  mkdir(workdir.c_str(), 0755);
  GMANRunOptions options;
  options.workingDirectory = workdir;
  return runGman(gman, {"-r", renderer, rib}, options);
}

void checkRenderer(std::string const& gman, std::string const& ribDir, std::string const& renderer) {
  const std::string on = renderer + "-progress-on";
  const std::string off = renderer + "-progress-off";
  GMANRunResult const withOption = renderIn(gman, renderer, ribDir + "/progress.rib", on);
  GMANRunResult const without = renderIn(gman, renderer, ribDir + "/progress_off.rib", off);

  check(withOption.exitStatus == 0, renderer + ": renders with the option");
  check(without.exitStatus == 0, renderer + ": renders without the option");

  const std::vector<int> percents = percentsIn(withOption.output);
  check(percents == wholePercentsOfRows(), renderer + ": one line per row, at each row's whole percent");
  check(withOption.output.find(kLastLine) != std::string::npos, renderer + ": the lines end at " + kLastLine);
  check(withOption.output.find(renderer + ": ") != std::string::npos, renderer + ": the lines name the renderer");
  check(percentsIn(without.output).empty(), renderer + ": no option, no progress line");

  const std::string imageOn = slurp(on + "/progress.tif");
  const std::string imageOff = slurp(off + "/progress.tif");
  check(!imageOn.empty() && imageOn == imageOff, renderer + ": the image is byte for byte the same either way");
}

} // namespace

int main(int argc, char* argv[]) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s <gman-binary> <tests/rib-dir> <renderer>...\n", argv[0]);
    return 2;
  }
  const std::string gman = argv[1];
  const std::string ribDir = argv[2];
  for (int i = 3; i < argc; ++i) {
    checkRenderer(gman, ribDir, argv[i]);
  }

  return checkSummary("a tracer logs row progress when asked and renders the same image either way");
}
