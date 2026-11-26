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
 * `gman -r <token> sphere.rib` stops the file when the token names no
 * loadable renderer: one report naming RIE_SYSTEM and no RIE_NOTSTARTED from
 * the file's requests, no image and exit status EXIT_FAILURE, without a hang
 * or a crash. Four tokens run the same checks, each in a directory of its own:
 * gmannosuchrenderer, which names no file; gman, the name of the core library;
 * matte, a shader module that loads and offers no renderer; and gmanrib, the
 * RIB-writer plugin.
 */

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <sstream>
#include <string>
#include <string_view>

#include "check.h"
#include "rungman.h"

namespace {

constexpr int kTimeoutSeconds = 20;
constexpr std::string_view kTokens[] = {"gmannosuchrenderer", "gman", "matte", "gmanrib"};

bool hasImage(std::filesystem::path const& directory) {
  for (auto const& entry : std::filesystem::directory_iterator(directory)) {
    if (entry.path().extension() == ".tif") {
      return true;
    }
  }
  return false;
}

void checkFailedRenderer(std::string const& gman, std::string const& rib, std::string_view token) {
  const auto label = std::format("-r {}: ", token);
  const auto directory = std::filesystem::current_path() / token;
  std::filesystem::remove_all(directory);
  std::filesystem::create_directories(directory);

  GMANRunOptions options;
  options.timeoutSeconds = kTimeoutSeconds;
  options.workingDirectory = directory.string();
  const auto result = runGman(gman, {"-r", std::string(token), rib}, options);

  check(!result.timedOut, label + "gman does not hang");
  check(!result.crashed, label + "gman does not crash");
  check(result.exitStatus == EXIT_FAILURE, label + "gman exits EXIT_FAILURE");

  int reportLines = 0;
  bool namesSystem = false;
  bool namesNoRenderer = false;
  std::istringstream lines(result.output);
  for (std::string line; std::getline(lines, line);) {
    if (line.find("RIE_") != std::string::npos) {
      ++reportLines;
      namesSystem = namesSystem || line.find("RIE_SYSTEM") != std::string::npos;
    }
    namesNoRenderer = namesNoRenderer || line.find("No renderer loaded.") != std::string::npos;
  }
  check(reportLines == 1, label + "gman prints exactly one line carrying RIE_");
  check(namesSystem, label + "the line names RIE_SYSTEM");
  check(!namesNoRenderer, label + "gman prints no \"No renderer loaded.\" line");
  check(!hasImage(directory), label + "gman writes no image");
}

} // namespace

int main(int argc, char* argv[]) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s <gman-binary> <sphere.rib>\n", argv[0]);
    return 2;
  }
  std::string const gman = argv[1];
  std::string const rib = argv[2];

  for (const auto token : kTokens) {
    checkFailedRenderer(gman, rib, token);
  }

  return checkSummary("gman stops a file whose -r names no loadable renderer with one report");
}
