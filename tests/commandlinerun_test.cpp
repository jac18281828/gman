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
 * What only the gman binary shows of its command line: the streams each
 * request writes, its exit status and the files it leaves. Every expected
 * text comes from the command-line module the binary is built on.
 */

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>
#include <variant>
#include <vector>

#include "check.h"
#include "gmancommandline.h"
#include "rungman.h"

namespace {

using Arguments = std::vector<std::string>;
using Capture = GMANRunOptions::Capture;

std::string versionText() { return "gman test\ndrivers: tiff"; }

GMANRunResult run(std::string const& gmanPath, Arguments const& arguments, Capture capture) {
  GMANRunOptions options;
  options.capture = capture;
  return runGman(gmanPath, arguments, options);
}

std::string firstLine(std::string const& text) { return text.substr(0, text.find('\n')); }

gman::CommandLineExit expectedExit(std::string const& gmanPath, Arguments const& arguments) {
  Arguments argv{gmanPath};
  argv.insert(argv.end(), arguments.begin(), arguments.end());
  auto const parsed = gman::parseCommandLine(argv, versionText);
  if (auto const* const leave = std::get_if<gman::CommandLineExit>(&parsed)) {
    return *leave;
  }
  return gman::CommandLineExit{-1, {}, {}};
}

void checkMatchesModule(std::string const& gmanPath, Arguments const& arguments, std::string const& name) {
  gman::CommandLineExit const expected = expectedExit(gmanPath, arguments);
  GMANRunResult const out = run(gmanPath, arguments, Capture::stdoutOnly);
  GMANRunResult const err = run(gmanPath, arguments, Capture::stderrOnly);
  check(out.exitStatus == expected.status && out.exitStatus == err.exitStatus, name + " exits as the module says");
  check(out.output == expected.out, name + " writes the module's stdout");
  check(err.output == expected.err, name + " writes the module's stderr");
}

} // namespace

int main(int argc, char* argv[]) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s <gman-binary> <sphere.rib>\n", argv[0]);
    return 2;
  }
  std::string const gmanPath = argv[1];
  std::string const rib = "sphere.rib";
  std::filesystem::copy_file(argv[2], rib, std::filesystem::copy_options::overwrite_existing);

  checkMatchesModule(gmanPath, {}, "no arguments");
  checkMatchesModule(gmanPath, {"-h"}, "-h");

  GMANRunResult const versionOut = run(gmanPath, {"--version"}, Capture::stdoutOnly);
  GMANRunResult const versionErr = run(gmanPath, {"--version"}, Capture::stderrOnly);
  check(versionOut.exitStatus == 0, "--version exits 0");
  check(versionErr.output.empty(), "--version writes nothing to stderr");
  check(std::ranges::count(versionOut.output, '\n') == 2 &&
            versionOut.output.substr(versionOut.output.find('\n') + 1).starts_with("drivers:"),
        "--version writes two lines, the second listing the drivers");

  std::filesystem::remove("sphere.tif");
  checkMatchesModule(gmanPath, {"-x", rib}, "-x");
  check(!std::filesystem::exists("sphere.tif"), "-x renders no image");

  std::filesystem::remove("sphere.tif");
  GMANRunResult const render = run(gmanPath, {rib}, Capture::stdoutOnly);
  check(render.exitStatus == 0, "a rib file renders and exits 0");
  check(std::filesystem::exists("sphere.tif"), "a rib file renders sphere.tif");
  check(std::ranges::count(render.output, '\n') > 1, "a rib file reports its progress after the banner");

  std::filesystem::remove("sphere.log");
  GMANRunResult const logged = run(gmanPath, {"-l", rib}, Capture::stdoutOnly);
  check(logged.exitStatus == 0, "-l renders and exits 0");
  check(std::filesystem::exists("sphere.log"), "-l writes sphere.log");

  GMANRunResult const quiet = run(gmanPath, {rib, "-q"}, Capture::stdoutOnly);
  check(quiet.exitStatus == 0, "a flag after the file renders and exits 0");
  check(quiet.output == firstLine(render.output) + "\n",
        "a flag after the file applies: a quiet run prints the banner alone");

  return checkSummary("gman's command-line streams, exit status and files are as the module describes");
}
