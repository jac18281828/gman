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
 * gman's command line, parsed in process: what each argument list resolves
 * to, and what each request that ends the run prints and exits with.
 */

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "check.h"
#include "gmancommandline.h"
#include "gmanlog.h"

namespace {

using gman::CommandLine;
using gman::CommandLineExit;
using Arguments = std::vector<std::string>;
using Parsed = std::variant<CommandLine, CommandLineExit>;

struct LevelFlag {
  char const* shortName;
  char const* longName;
  GMANLogLevel level;
};

constexpr std::array<LevelFlag, 5> levelFlags{{
    {"-d", "--debug", LOGLVL_DEBUG},
    {"-i", "--info", LOGLVL_INFO},
    {"-w", "--warning", LOGLVL_WARNING},
    {"-e", "--error", LOGLVL_ERROR},
    {"-q", "--quiet", LOGLVL_DISASTER},
}};

constexpr bool noLog = false;
constexpr bool withLog = true;
std::optional<std::string> const noRenderer;

std::string versionText() { return "gman test\ndrivers: tiff"; }

Arguments withProgramName(Arguments const& arguments) {
  Arguments argv{"gman"};
  argv.insert(argv.end(), arguments.begin(), arguments.end());
  return argv;
}

Parsed parse(Arguments const& arguments) { return gman::parseCommandLine(withProgramName(arguments), versionText); }

std::string describe(Arguments const& arguments) {
  std::string text = "gman";
  for (std::string const& argument : arguments) {
    text += " '" + argument + "'";
  }
  return text;
}

void expectRun(Arguments const& arguments, GMANLogLevel level, bool writeLog,
               std::optional<std::string> const& renderer, Arguments const& files) {
  Parsed const parsed = parse(arguments);
  CommandLine const* const run = std::get_if<CommandLine>(&parsed);
  check(run != nullptr && run->level == level && run->writeLog == writeLog && run->renderer == renderer &&
            run->files == files,
        describe(arguments) + " renders " + describe(files) + " at level " + std::to_string(static_cast<int>(level)) +
            (writeLog ? " with a log" : " without a log") +
            (renderer ? " on renderer '" + *renderer + "'" : " on the default renderer"));
}

void expectBitDepth(Arguments const& arguments, std::optional<int> bitDepth) {
  Parsed const parsed = parse(arguments);
  CommandLine const* const run = std::get_if<CommandLine>(&parsed);
  check(run != nullptr && run->bitDepth == bitDepth && run->files == Arguments{"a.rib"},
        describe(arguments) + " parses to " + (bitDepth ? "bit depth " + std::to_string(*bitDepth) : "no bit depth"));
}

void expectHelp(Arguments const& arguments) {
  Parsed const parsed = parse(arguments);
  CommandLineExit const* const leave = std::get_if<CommandLineExit>(&parsed);
  check(leave != nullptr && leave->status == 0 && leave->err.empty() && !leave->out.empty(),
        describe(arguments) + " prints help to stdout and exits 0");
}

void expectVersion(Arguments const& arguments) {
  Parsed const parsed = parse(arguments);
  CommandLineExit const* const leave = std::get_if<CommandLineExit>(&parsed);
  check(leave != nullptr && leave->status == 0 && leave->err.empty() && leave->out == versionText() + "\n",
        describe(arguments) + " prints the version to stdout and exits 0");
}

void expectUsageError(Arguments const& arguments) {
  Parsed const parsed = parse(arguments);
  CommandLineExit const* const leave = std::get_if<CommandLineExit>(&parsed);
  check(leave != nullptr && leave->status != 0 && leave->out.empty() && !leave->err.empty(),
        describe(arguments) + " reports a usage error on stderr and exits non-zero");
}

void expectUnexpected(Arguments const& arguments, std::string const& token) {
  Parsed const parsed = parse(arguments);
  CommandLineExit const* const leave = std::get_if<CommandLineExit>(&parsed);
  check(leave != nullptr && leave->status != 0 && leave->out.empty() && leave->err.find(token) != std::string::npos,
        describe(arguments) + " names '" + token + "' as the unexpected argument");
}

void checkFiles() {
  expectRun({"sphere.rib"}, LOGLVL_INFO, noLog, noRenderer, {"sphere.rib"});
  expectRun({"a.rib", "b.rib", "c.rib"}, LOGLVL_INFO, noLog, noRenderer, {"a.rib", "b.rib", "c.rib"});
  expectRun({"--", "-x.rib"}, LOGLVL_INFO, noLog, noRenderer, {"-x.rib"});
}

void checkLevels() {
  for (LevelFlag const& flag : levelFlags) {
    for (std::string_view const name : {flag.shortName, flag.longName}) {
      expectRun({std::string(name), "a.rib"}, flag.level, noLog, noRenderer, {"a.rib"});
    }
  }
  expectRun({"-q", "-d", "a.rib"}, LOGLVL_DEBUG, noLog, noRenderer, {"a.rib"});
  expectRun({"-d", "-q", "a.rib"}, LOGLVL_DISASTER, noLog, noRenderer, {"a.rib"});
  expectRun({"--quiet", "--debug", "a.rib"}, LOGLVL_DEBUG, noLog, noRenderer, {"a.rib"});
}

void checkLog() {
  expectRun({"-l", "a.rib"}, LOGLVL_INFO, withLog, noRenderer, {"a.rib"});
  expectRun({"--log-file", "a.rib"}, LOGLVL_INFO, withLog, noRenderer, {"a.rib"});
  expectRun({"-dl", "a.rib"}, LOGLVL_DEBUG, withLog, noRenderer, {"a.rib"});
}

void checkFlagsAfterFiles() {
  expectRun({"a.rib", "-q"}, LOGLVL_DISASTER, noLog, noRenderer, {"a.rib"});
  expectRun({"a.rib", "-q", "b.rib"}, LOGLVL_DISASTER, noLog, noRenderer, {"a.rib", "b.rib"});
}

void checkRenderer() {
  expectRun({"-r", "gmanraytracer", "a.rib"}, LOGLVL_INFO, noLog, "gmanraytracer", {"a.rib"});
  expectRun({"-rgmanraytracer", "a.rib"}, LOGLVL_INFO, noLog, "gmanraytracer", {"a.rib"});
  expectRun({"--renderer", "gmanraytracer", "a.rib"}, LOGLVL_INFO, noLog, "gmanraytracer", {"a.rib"});
  expectRun({"--renderer=gmanraytracer", "a.rib"}, LOGLVL_INFO, noLog, "gmanraytracer", {"a.rib"});
  expectRun({"-r", "first", "-r", "second", "a.rib"}, LOGLVL_INFO, noLog, "second", {"a.rib"});
}

void checkBitDepth() {
  expectBitDepth({"a.rib"}, std::nullopt);
  expectBitDepth({"--bit-depth", "16", "a.rib"}, 16);
  expectBitDepth({"--bit-depth=8", "a.rib"}, 8);
  expectBitDepth({"a.rib", "--bit-depth", "16"}, 16);
  expectBitDepth({"--bit-depth", "8", "--bit-depth", "16", "a.rib"}, 16);
  expectRun({"-q", "--bit-depth", "16", "-r", "gmanraytracer", "a.rib"}, LOGLVL_DISASTER, noLog, "gmanraytracer",
            {"a.rib"});
}

void checkHelp() {
  expectHelp({});
  expectHelp({"-h"});
  expectHelp({"--help"});
  expectHelp({"-h", "-x"});

  Parsed const none = parse({});
  Parsed const help = parse({"-h"});
  CommandLineExit const* const noneExit = std::get_if<CommandLineExit>(&none);
  CommandLineExit const* const helpExit = std::get_if<CommandLineExit>(&help);
  check(noneExit != nullptr && helpExit != nullptr && noneExit->out == helpExit->out,
        "no arguments prints what -h prints");

  for (std::string_view const flag : {"--help", "--version", "--debug", "--info", "--warning", "--error", "--quiet",
                                      "--log-file", "--renderer", "--bit-depth"}) {
    check(helpExit != nullptr && helpExit->out.find(flag) != std::string::npos, "-h lists " + std::string(flag));
  }
}

void checkVersion() {
  expectVersion({"--version"});
  expectVersion({"a.rib", "--version"});
  expectVersion({"-x", "--version"});
}

void checkUsageErrors() {
  expectUnexpected({"-x", "a.rib"}, "-x");
  expectUnexpected({"a.rib", "-x"}, "-x");
  expectUnexpected({"--foo", "a.rib"}, "--foo");
  expectUsageError({"-r"});
  expectUsageError({"-r", "", "a.rib"});
  expectUsageError({"--renderer=", "a.rib"});
  expectUsageError({"-d"});
  expectUsageError({"--bit-depth", "12", "a.rib"});
  expectUsageError({"--bit-depth", "0", "a.rib"});
  expectUsageError({"--bit-depth", "sixteen", "a.rib"});
  expectUsageError({"--bit-depth=", "a.rib"});
  expectUsageError({"--bit-depth="});
  expectUsageError({"a.rib", "--bit-depth"});
}

void checkEmptyArgv() {
  Parsed const alone = parse({});
  Parsed const empty = gman::parseCommandLine({}, versionText);
  CommandLineExit const* const aloneExit = std::get_if<CommandLineExit>(&alone);
  CommandLineExit const* const emptyExit = std::get_if<CommandLineExit>(&empty);
  check(aloneExit != nullptr && emptyExit != nullptr && aloneExit->status == emptyExit->status &&
            aloneExit->out == emptyExit->out && aloneExit->err == emptyExit->err,
        "an empty argv acts as the program name alone");
}

} // namespace

int main() {
  checkFiles();
  checkLevels();
  checkLog();
  checkFlagsAfterFiles();
  checkRenderer();
  checkBitDepth();
  checkHelp();
  checkVersion();
  checkUsageErrors();
  checkEmptyArgv();
  return checkSummary("gman's command line parses in process");
}
