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

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "check.h"
#include "gmancommandline.h"
#include "rungman.h"

namespace {

using Arguments = std::vector<std::string>;
using Capture = GMANRunOptions::Capture;

GMANRunResult run(std::string const& gmanPath, Arguments const& arguments, Capture capture) {
  GMANRunOptions options;
  options.capture = capture;
  return runGman(gmanPath, arguments, options);
}

std::string firstLine(std::string const& text) { return text.substr(0, text.find('\n')); }

bool isOneLine(std::string const& text) { return !text.empty() && text.find('\n') == text.size() - 1; }

} // namespace

int main(int argc, char* argv[]) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s <gman-binary> <sphere.rib>\n", argv[0]);
    return 2;
  }
  std::string const gmanPath = argv[1];
  std::string const rib = "sphere.rib";
  std::filesystem::copy_file(argv[2], rib, std::filesystem::copy_options::overwrite_existing);

  GMANRunResult const noArgumentsOut = run(gmanPath, {}, Capture::stdoutOnly);
  GMANRunResult const noArgumentsErr = run(gmanPath, {}, Capture::stderrOnly);
  check(noArgumentsOut.exitStatus == 0, "no arguments exits 0");
  check(noArgumentsOut.output.empty(), "no arguments writes nothing to stdout");
  check(noArgumentsErr.output == gman::usageText(gmanPath), "no arguments writes the usage text to stderr");

  GMANRunResult const helpOut = run(gmanPath, {"-h"}, Capture::stdoutOnly);
  GMANRunResult const helpErr = run(gmanPath, {"-h"}, Capture::stderrOnly);
  check(helpOut.exitStatus == 0, "-h exits 0");
  check(isOneLine(helpOut.output), "-h writes one line to stdout");
  check(helpErr.output == gman::usageText(gmanPath), "-h writes the usage text to stderr");
  std::string const banner = firstLine(helpOut.output);

  GMANRunResult const versionOut = run(gmanPath, {"--version"}, Capture::stdoutOnly);
  GMANRunResult const versionErr = run(gmanPath, {"--version"}, Capture::stderrOnly);
  check(versionOut.exitStatus == 0, "--version exits 0");
  check(!versionOut.output.empty(), "--version writes to stdout");
  check(firstLine(versionOut.output) != banner, "--version prints no banner");
  check(versionErr.output.empty(), "--version writes nothing to stderr");

  std::string const badFlagMessage = gman::errorText(gman::parseCommandLine({"-x", rib}), gmanPath);
  std::filesystem::remove("sphere.tif");
  GMANRunResult const badFlagOut = run(gmanPath, {"-x", rib}, Capture::stdoutOnly);
  GMANRunResult const badFlagErr = run(gmanPath, {"-x", rib}, Capture::stderrOnly);
  check(badFlagOut.exitStatus == 1 && badFlagErr.exitStatus == 1, "-x exits 1");
  check(badFlagOut.output == helpOut.output, "-x prints the banner to stdout");
  check(!badFlagMessage.empty() && badFlagErr.output == badFlagMessage, "-x writes its error text to stderr");
  check(!std::filesystem::exists("sphere.tif"), "-x renders no image");

  std::filesystem::remove("sphere.tif");
  GMANRunResult const render = run(gmanPath, {rib}, Capture::stdoutOnly);
  check(render.exitStatus == 0, "a rib file renders and exits 0");
  check(firstLine(render.output) == banner, "a rib file prints the banner first");
  check(std::filesystem::exists("sphere.tif"), "a rib file renders sphere.tif");

  std::filesystem::remove("sphere.log");
  GMANRunResult const logged = run(gmanPath, {"-l", rib}, Capture::stdoutOnly);
  check(logged.exitStatus == 0, "-l renders and exits 0");
  check(std::filesystem::exists("sphere.log"), "-l writes sphere.log");

  return checkSummary("gman's command-line streams, exit status and files are as the module describes");
}
