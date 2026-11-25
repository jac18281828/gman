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

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include "check.h"
#include "rungman.h"

namespace {

using Args = std::vector<std::string>;

struct Outcome {
  int exitStatus = -1;
  std::string out;
  std::string err;
};

enum class Level { debug, info, warning, error, quiet };

std::string const sphereRib = "sphere.rib";
std::string const sphereImage = "sphere.tif";
std::string const sphereLog = "sphere.log";

std::string gman;
std::string banner;
std::string versionText;

std::string usageText(std::string const& name) {
  return name + ": -[hdiweql] [-r renderer] [--version] files ...\n"
                "\tParse RIB input files.\n"
                "\n"
                "\t-h - print this help message.\n"
                "\t-d - set logging to: debug output.\n"
                "\t-i - set logging to: information output.\n"
                "\t-w - set logging to: warning output.\n"
                "\t-e - set logging to: error output.\n"
                "\t-q - set logging to: quiet, only report disasters.\n"
                "\t-l - enable a log based on the filename of the rib.\n"
                "\t-r renderer - choose the renderer plugin (default: gmanzbuffer).\n"
                "\t--version - print the version and compiled drivers, then exit.\n";
}

std::string unrecognizedPrefix() { return gman + ": unrecognized flag "; }

std::string describe(Args const& args) {
  std::string text = "gman";
  for (std::string const& arg : args) {
    text += " \"" + arg + "\"";
  }
  return text;
}

bool exists(std::string const& path) { return std::filesystem::exists(path); }

void clean() {
  std::filesystem::remove(sphereImage);
  std::filesystem::remove(sphereLog);
}

bool contains(std::string const& text, std::string const& fragment) { return text.find(fragment) != std::string::npos; }

bool startsWith(std::string const& text, std::string const& prefix) { return text.rfind(prefix, 0) == 0; }

bool endsWith(std::string const& text, std::string const& suffix) {
  return text.size() >= suffix.size() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

Outcome run(Args const& args) {
  clean();
  GMANRunOptions options;
  options.timeoutSeconds = 120;
  options.capture = GMANRunOptions::Capture::stdoutOnly;
  GMANRunResult const out = runGman(gman, args, options);
  options.capture = GMANRunOptions::Capture::stderrOnly;
  GMANRunResult const err = runGman(gman, args, options);
  Outcome outcome;
  outcome.exitStatus = out.exitStatus == err.exitStatus ? out.exitStatus : -2;
  outcome.out = out.output;
  outcome.err = err.output;
  return outcome;
}

void expectExact(Args const& args, int exitStatus, std::string const& out, std::string const& err) {
  Outcome const outcome = run(args);
  std::string const label = describe(args);
  check(outcome.exitStatus == exitStatus, label + " exits " + std::to_string(exitStatus));
  check(outcome.out == out, label + " writes the expected stdout");
  check(outcome.err == err, label + " writes the expected stderr");
}

void expectUnrecognized(Args const& args, std::string const& token) {
  expectExact(args, 1, banner, unrecognizedPrefix() + token + "\n" + usageText(gman));
  check(!exists(sphereImage), describe(args) + " parses no file");
}

void expectUnrecognizedAnyToken(Args const& args) {
  Outcome const outcome = run(args);
  std::string const label = describe(args);
  check(outcome.exitStatus == 1, label + " exits 1");
  check(outcome.out == banner, label + " writes only the banner to stdout");
  check(startsWith(outcome.err, unrecognizedPrefix()), label + " names an unrecognized flag");
  check(endsWith(outcome.err, usageText(gman)), label + " ends with the usage text");
  check(!exists(sphereImage), label + " parses no file");
}

void expectVersion(Args const& args, std::string const& drivers) {
  expectExact(args, 0, "gman " + versionText + "\ndrivers: " + drivers + "\n", "");
  check(!exists(sphereImage), describe(args) + " parses no file");
}

void expectRenders(Args const& args) {
  Outcome const outcome = run(args);
  std::string const label = describe(args);
  check(outcome.exitStatus == 0, label + " exits 0");
  check(exists(sphereImage), label + " renders " + sphereImage);
}

void expectNoImage(Args const& args) {
  Outcome const outcome = run(args);
  std::string const label = describe(args);
  check(outcome.exitStatus != 0, label + " exits nonzero");
  check(!exists(sphereImage), label + " writes no image");
  check(!contains(outcome.err, "unrecognized flag"), label + " reports no unrecognized flag");
}

void expectLevel(Args const& args, Level level) {
  Outcome const outcome = run(args);
  std::string const label = describe(args);
  bool const parsing = level == Level::debug || level == Level::info;
  bool const keywords = level == Level::debug;
  check(outcome.exitStatus == 0, label + " exits 0");
  check(startsWith(outcome.out, banner), label + " starts stdout with the banner");
  check(contains(outcome.out, "Parsing sphere.rib\n") == parsing, label + " logs the file at the expected level");
  check(contains(outcome.out, "Keyword token:") == keywords, label + " logs keyword tokens at the expected level");
  check(exists(sphereImage), label + " renders " + sphereImage);
}

void checkUsageAndVersionRows(std::string const& drivers) {
  expectExact({}, 0, "", usageText(gman));
  expectVersion({"--version"}, drivers);
  expectVersion({"-x", "--version"}, drivers);
  expectVersion({"-r", "--version"}, drivers);
  expectVersion({sphereRib, "--version"}, drivers);
  expectVersion({"-h", "--version"}, drivers);
  expectExact({"-h"}, 0, banner, usageText(gman));
  expectExact({"-h", sphereRib}, 0, banner, usageText(gman));
  check(!exists(sphereImage), "-h parses no file after it");
  expectExact({"-h", "-r"}, 0, banner, usageText(gman));
}

void checkUnrecognizedRows() {
  for (std::string const& token : {"-x", "--foo", "-", "--", "-5"}) {
    expectUnrecognized({token, sphereRib}, token);
  }
  expectUnrecognized({"-d", "-x", sphereRib}, "-x");
}

void checkRendererRows() {
  std::string const missing = gman + ": -r requires a renderer name\n";
  std::string const empty = gman + ": -r requires a non-empty renderer name\n";
  expectExact({"-r"}, 1, banner, missing);
  expectExact({"-d", "-r"}, 1, banner, missing);
  expectExact({"-r", ""}, 1, banner, empty);
  expectExact({"-r", "", sphereRib}, 1, banner, empty);
  check(!exists(sphereImage), "-r \"\" parses no file");
  expectExact({"-r", "", "-h"}, 1, banner, empty);
  expectRenders({"-rgmanzbuffer", sphereRib});
  expectRenders({"-r", "gmanzbuffer", sphereRib});
  expectRenders({"-r", "nosuchrenderer", "-r", "gmanzbuffer", sphereRib});
  expectNoImage({"-r", "nosuchrenderer", sphereRib});
  expectNoImage({"-r", "gmanzbuffer", "-r", "nosuchrenderer", sphereRib});
  expectNoImage({"-r", "-x", sphereRib});
}

void checkLevelRows() {
  expectExact({"-q"}, 0, banner, "");
  expectExact({"-d", "-l"}, 0, banner, "");
  expectLevel({sphereRib}, Level::info);
  expectLevel({"-i", sphereRib}, Level::info);
  expectLevel({"-d", sphereRib}, Level::debug);
  expectLevel({"-w", sphereRib}, Level::warning);
  expectLevel({"-e", sphereRib}, Level::error);
  expectLevel({"-q", sphereRib}, Level::quiet);
  expectLevel({"-q", "-d", sphereRib}, Level::debug);
  expectLevel({"-d", "-q", sphereRib}, Level::quiet);
  expectLevel({"-i", "-e", sphereRib}, Level::error);
  expectLevel({"-e", "-i", sphereRib}, Level::info);
  expectLevel({"-d", "-i", "-w", "-e", "-q", sphereRib}, Level::quiet);
}

void checkFileRows() {
  Outcome const flagAfterFile = run({sphereRib, "-q"});
  check(flagAfterFile.exitStatus != 0, "sphere.rib -q exits nonzero");
  check(exists(sphereImage), "sphere.rib -q renders " + sphereImage);
  check(contains(flagAfterFile.out, "Parsing -q"), "sphere.rib -q tries a file named -q");

  Outcome const missingFirst = run({"nosuchfile.rib", sphereRib});
  check(missingFirst.exitStatus != 0, "nosuchfile.rib sphere.rib exits nonzero");
  check(exists(sphereImage), "nosuchfile.rib sphere.rib still renders " + sphereImage);

  Outcome const logged = run({"-l", sphereRib});
  check(logged.exitStatus == 0, "-l sphere.rib exits 0");
  check(exists(sphereLog), "-l sphere.rib writes " + sphereLog);
}

void checkClusterRows() {
  Outcome const debugAndLog = run({"-dl", sphereRib});
  check(debugAndLog.exitStatus == 0, "-dl sphere.rib exits 0");
  check(contains(debugAndLog.out, "Keyword token:"), "-dl applies -d");
  check(exists(sphereLog), "-dl applies -l and writes " + sphereLog);

  Outcome const quietThenDebug = run({"-qd", sphereRib});
  check(contains(quietThenDebug.out, "Keyword token:"), "-qd ends at debug");
  expectLevel({"-dq", sphereRib}, Level::quiet);
}

void checkLeftoverAndPrecedenceRows() {
  expectUnrecognizedAnyToken({"-dx", sphereRib});
  expectUnrecognizedAnyToken({"-hx"});
  expectUnrecognizedAnyToken({"-d=1", sphereRib});
  expectUnrecognizedAnyToken({"-l1", sphereRib});
  expectUnrecognizedAnyToken({"-h", "-x"});
  expectUnrecognizedAnyToken({"-r", "", "-x"});
}

} // namespace

int main(int argc, char* argv[]) {
  if (argc != 5) {
    std::fprintf(stderr, "usage: %s <gman-binary> <sphere.rib> <version> <expected driver list>\n", argv[0]);
    return 2;
  }
  gman = argv[1];
  versionText = argv[3];
  banner = "gman " + versionText + " -- LGPL-2.1-or-later, see COPYING\n";
  std::string const drivers = argv[4];

  std::error_code ec;
  std::filesystem::copy_file(argv[2], sphereRib, std::filesystem::copy_options::overwrite_existing, ec);
  if (ec) {
    std::fprintf(stderr, "cannot copy %s: %s\n", argv[2], ec.message().c_str());
    return 2;
  }

  checkUsageAndVersionRows(drivers);
  checkUnrecognizedRows();
  checkRendererRows();
  checkLevelRows();
  checkFileRows();
  checkClusterRows();
  checkLeftoverAndPrecedenceRows();

  return checkSummary("gman's command line: every flag, error path and precedence rule");
}
