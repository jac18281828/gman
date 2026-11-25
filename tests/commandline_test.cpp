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
 * gman's command line, parsed in process: the outcome and the fields each
 * argument list resolves to, and the usage text's listing of every option.
 */

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "check.h"
#include "gmancommandline.h"
#include "gmanlog.h"

namespace {

using gman::CommandLine;
using Outcome = CommandLine::Outcome;
using Arguments = std::vector<std::string>;

std::string_view nameOf(Outcome outcome) {
  switch (outcome) {
  case Outcome::run:
    return "run";
  case Outcome::version:
    return "version";
  case Outcome::noArguments:
    return "noArguments";
  case Outcome::help:
    return "help";
  case Outcome::unrecognized:
    return "unrecognized";
  case Outcome::missingRenderer:
    return "missingRenderer";
  case Outcome::emptyRenderer:
    return "emptyRenderer";
  }
  return "unknown";
}

std::string describe(Arguments const& arguments) {
  if (arguments.empty()) {
    return "no arguments";
  }
  std::string text;
  for (std::string const& argument : arguments) {
    text += (text.empty() ? "'" : " '") + argument + "'";
  }
  return text;
}

void expectOutcome(Arguments const& arguments, Outcome outcome) {
  CommandLine const parsed = gman::parseCommandLine(arguments);
  check(parsed.outcome == outcome, describe(arguments) + " is " + std::string(nameOf(outcome)));
}

void expectNamed(Arguments const& arguments, std::string const& token) {
  CommandLine const parsed = gman::parseCommandLine(arguments);
  check(parsed.outcome == Outcome::unrecognized && parsed.unrecognized == token,
        describe(arguments) + " names '" + token + "' as unrecognized");
}

void expectLevel(Arguments const& arguments, std::optional<GMANLogLevel> level) {
  CommandLine const parsed = gman::parseCommandLine(arguments);
  check(parsed.outcome == Outcome::run && parsed.level == level,
        describe(arguments) +
            (level ? " ends at level " + std::to_string(static_cast<int>(*level)) : " sets no level"));
}

void expectRenderer(Arguments const& arguments, std::optional<std::string> const& renderer) {
  CommandLine const parsed = gman::parseCommandLine(arguments);
  check(parsed.outcome == Outcome::run && parsed.renderer == renderer,
        describe(arguments) + (renderer ? " selects renderer '" + *renderer + "'" : " selects no renderer"));
}

void expectFiles(Arguments const& arguments, Arguments const& files) {
  CommandLine const parsed = gman::parseCommandLine(arguments);
  check(parsed.outcome == Outcome::run && parsed.files == files,
        describe(arguments) + " renders " + std::to_string(files.size()) + " file(s): " + describe(files));
}

void checkVersionAndNoArguments() {
  expectOutcome({}, Outcome::noArguments);
  expectOutcome({"--version"}, Outcome::version);
  expectOutcome({"-x", "--version"}, Outcome::version);
  expectOutcome({"-r", "--version"}, Outcome::version);
  expectOutcome({"sphere.rib", "--version"}, Outcome::version);
  expectOutcome({"-h", "--version"}, Outcome::version);
  expectOutcome({"--renderer", "--version"}, Outcome::version);
}

void checkHelp() {
  expectOutcome({"-h"}, Outcome::help);
  expectOutcome({"--help"}, Outcome::help);
  expectOutcome({"-h", "--help"}, Outcome::help);
  expectOutcome({"-h", "-d", "sphere.rib"}, Outcome::help);
  expectOutcome({"-h", "-r"}, Outcome::help);
  expectOutcome({"-r", "", "-h"}, Outcome::emptyRenderer);
}

void checkUnrecognizedTokens() {
  for (std::string const token : {"-x", "--foo", "-", "--", "-5"}) {
    expectNamed({token}, token);
    expectNamed({token, "sphere.rib"}, token);
  }
}

void checkRendererErrors() {
  expectOutcome({"-r"}, Outcome::missingRenderer);
  expectOutcome({"--renderer"}, Outcome::missingRenderer);
  expectOutcome({"sphere.rib", "-r"}, Outcome::run);
  expectOutcome({"-r", ""}, Outcome::emptyRenderer);
  expectOutcome({"--renderer", ""}, Outcome::emptyRenderer);
  expectOutcome({"--renderer="}, Outcome::emptyRenderer);
  expectOutcome({"--renderer=", "sphere.rib"}, Outcome::emptyRenderer);
  expectOutcome({"-h", "--renderer="}, Outcome::emptyRenderer);
  expectOutcome({"-r", "--renderer=", "sphere.rib"}, Outcome::emptyRenderer);
}

void checkRendererNames() {
  expectRenderer({"-r", "gmanzbuffer"}, "gmanzbuffer");
  expectRenderer({"-rgmanzbuffer"}, "gmanzbuffer");
  expectRenderer({"--renderer", "gmanzbuffer"}, "gmanzbuffer");
  expectRenderer({"--renderer=gmanzbuffer"}, "gmanzbuffer");
  expectRenderer({"-r", "first", "-r", "second"}, "second");
  expectRenderer({"-r", "first", "--renderer", "second"}, "second");
  expectRenderer({"--renderer", "first", "-r", "second"}, "second");
  expectRenderer({"--renderer=first", "-rsecond"}, "second");
  expectRenderer({"-r", "-x"}, "-x");
  expectRenderer({"--renderer=x=y"}, "x=y");
  expectRenderer({"-d", "-l"}, std::nullopt);
  expectRenderer({"sphere.rib"}, std::nullopt);
}

void checkLevels() {
  expectLevel({"-d"}, LOGLVL_DEBUG);
  expectLevel({"-i"}, LOGLVL_INFO);
  expectLevel({"-w"}, LOGLVL_WARNING);
  expectLevel({"-e"}, LOGLVL_ERROR);
  expectLevel({"-q"}, LOGLVL_DISASTER);
  expectLevel({"--debug"}, LOGLVL_DEBUG);
  expectLevel({"--info"}, LOGLVL_INFO);
  expectLevel({"--warning"}, LOGLVL_WARNING);
  expectLevel({"--error"}, LOGLVL_ERROR);
  expectLevel({"--quiet"}, LOGLVL_DISASTER);
  expectLevel({"-q", "-d"}, LOGLVL_DEBUG);
  expectLevel({"-d", "-q"}, LOGLVL_DISASTER);
  expectLevel({"-e", "-i"}, LOGLVL_INFO);
  expectLevel({"-i", "-e"}, LOGLVL_ERROR);
  expectLevel({"-q", "--debug"}, LOGLVL_DEBUG);
  expectLevel({"--quiet", "--debug"}, LOGLVL_DEBUG);
  expectLevel({"--debug", "--quiet"}, LOGLVL_DISASTER);
  expectLevel({"-l"}, std::nullopt);
  expectLevel({"-r", "gmanzbuffer"}, std::nullopt);
  expectLevel({"sphere.rib"}, std::nullopt);
}

void checkWhereFlagsEnd() {
  expectFiles({"sphere.rib"}, {"sphere.rib"});
  expectFiles({"-d", "a.rib", "b.rib"}, {"a.rib", "b.rib"});
  expectFiles({"sphere.rib", "-q"}, {"sphere.rib", "-q"});
  expectFiles({"sphere.rib", "--debug"}, {"sphere.rib", "--debug"});
  expectLevel({"sphere.rib", "-q"}, std::nullopt);
  expectLevel({"sphere.rib", "--debug"}, std::nullopt);
  expectFiles({"-q"}, {});
  expectFiles({"-d", "-l"}, {});
}

void checkLogFile() {
  CommandLine const parsed = gman::parseCommandLine({"-l", "sphere.rib"});
  check(parsed.outcome == Outcome::run && parsed.writeLog, "-l asks for a log");
  CommandLine const longForm = gman::parseCommandLine({"--log-file", "sphere.rib"});
  check(longForm.outcome == Outcome::run && longForm.writeLog, "--log-file asks for a log");
  CommandLine const none = gman::parseCommandLine({"sphere.rib"});
  check(none.outcome == Outcome::run && !none.writeLog, "no flag asks for no log");
}

void checkClusters() {
  CommandLine const cluster = gman::parseCommandLine({"-dl", "sphere.rib"});
  check(cluster.outcome == Outcome::run && cluster.level == LOGLVL_DEBUG && cluster.writeLog,
        "-dl sets debug and the log");
  expectLevel({"-qd"}, LOGLVL_DEBUG);
  expectLevel({"-dq"}, LOGLVL_DISASTER);
  expectOutcome({"-dx"}, Outcome::unrecognized);
  expectOutcome({"-hx"}, Outcome::unrecognized);
  expectOutcome({"-d=1"}, Outcome::unrecognized);
  expectOutcome({"-l1"}, Outcome::unrecognized);
}

void checkUnrecognizedOutranksEarlierEvents() {
  expectNamed({"-h", "-x"}, "-x");
  expectNamed({"-r", "", "-x"}, "-x");
  expectNamed({"-x", "--debug=1", "sphere.rib"}, "-x");
}

void checkLongNameRules() {
  for (std::string const argument : {"--debug=1", "--help=x", "--log-file=x", "--deb", "--Debug"}) {
    expectNamed({argument}, argument);
    expectNamed({argument, "sphere.rib"}, argument);
  }
  expectNamed({"-h", "--debug=1"}, "--debug=1");
}

void checkRenderersGuardStopsAtFirstFile() {
  expectFiles({"sphere.rib", "--renderer="}, {"sphere.rib", "--renderer="});
}

void checkEmptyValuesActAsBareFlags() {
  expectLevel({"--debug="}, LOGLVL_DEBUG);
  expectLevel({"--debug={}"}, LOGLVL_DEBUG);
  expectLevel({"--quiet="}, LOGLVL_DISASTER);
  CommandLine const log = gman::parseCommandLine({"--log-file="});
  check(log.outcome == Outcome::run && log.writeLog, "--log-file= asks for a log");
  expectOutcome({"--help="}, Outcome::help);
  expectOutcome({"--help={}"}, Outcome::help);
}

std::vector<std::string> splitLines(std::string const& text) {
  std::vector<std::string> lines;
  std::string line;
  for (char const c : text) {
    if (c == '\n') {
      lines.push_back(line);
      line.clear();
    } else {
      line += c;
    }
  }
  if (!line.empty()) {
    lines.push_back(line);
  }
  return lines;
}

std::vector<std::string> tokensOf(std::string const& line) {
  std::vector<std::string> tokens;
  std::string token;
  for (char const c : line) {
    if (std::isspace(static_cast<unsigned char>(c)) != 0 || c == ',') {
      if (!token.empty()) {
        tokens.push_back(token);
        token.clear();
      }
    } else {
      token += c;
    }
  }
  if (!token.empty()) {
    tokens.push_back(token);
  }
  return tokens;
}

bool holdsEverySpelling(std::string const& line, std::vector<std::string> const& spellings) {
  std::vector<std::string> const tokens = tokensOf(line);
  return std::ranges::all_of(spellings, [&tokens](std::string const& spelling) {
    return std::ranges::find(tokens, spelling) != tokens.end();
  });
}

std::string joined(std::vector<std::string> const& spellings) {
  std::string text;
  for (std::string const& spelling : spellings) {
    text += (text.empty() ? "" : " ") + spelling;
  }
  return text;
}

void checkEverySpellingParses() {
  std::vector<std::vector<std::string>> const entries = gman::optionSpellings();
  check(entries.size() > 1, "optionSpellings lists more than --version");
  for (std::vector<std::string> const& spellings : entries) {
    for (std::string const& spelling : spellings) {
      CommandLine const alone = gman::parseCommandLine({spelling});
      check(alone.outcome != Outcome::unrecognized, spelling + " given alone is recognized");
      if (alone.outcome == Outcome::missingRenderer) {
        CommandLine const valued = gman::parseCommandLine({spelling, "gmanzbuffer"});
        check(valued.outcome == Outcome::run && valued.renderer == "gmanzbuffer",
              spelling + " takes the value gmanzbuffer");
      }
    }
  }
}

void checkUsageListsEveryOption() {
  std::vector<std::string> const lines = splitLines(gman::usageText("gman"));
  for (std::vector<std::string> const& spellings : gman::optionSpellings()) {
    auto const holders = std::ranges::count_if(
        lines, [&spellings](std::string const& line) { return holdsEverySpelling(line, spellings); });
    check(holders == 1,
          "exactly one usage line holds " + joined(spellings) + " (holds " + std::to_string(holders) + ")");
  }
}

void checkErrorText() {
  CommandLine const bad = gman::parseCommandLine({"-x", "sphere.rib"});
  std::string const text = gman::errorText(bad, "gman");
  std::string const usage = gman::usageText("gman");
  check(text.starts_with("gman: "), "the unrecognized-flag error opens with the program name");
  check(text.find("-x") != std::string::npos, "the unrecognized-flag error names -x");
  check(text.ends_with(usage), "the unrecognized-flag error ends with the usage text");
  check(!usage.empty() && usage.starts_with("gman: "), "the usage text opens with the program name");
  check(usage.ends_with('\n'), "the usage text ends with a newline");

  for (Arguments const& arguments : {Arguments{"-r"}, Arguments{"-r", ""}}) {
    std::string const message = gman::errorText(gman::parseCommandLine(arguments), "gman");
    check(message.starts_with("gman: ") && message.ends_with('\n') && message.find('\n') == message.size() - 1,
          describe(arguments) + " errors on one line naming the program");
  }
  check(gman::errorText(gman::parseCommandLine({"-r"}), "gman") !=
            gman::errorText(gman::parseCommandLine({"-r", ""}), "gman"),
        "a missing renderer and an empty one read differently");

  for (Arguments const& arguments : {Arguments{}, Arguments{"--version"}, Arguments{"-h"}, Arguments{"sphere.rib"}}) {
    check(gman::errorText(gman::parseCommandLine(arguments), "gman").empty(),
          describe(arguments) + " has no error text");
  }
}

} // namespace

int main() {
  checkVersionAndNoArguments();
  checkHelp();
  checkUnrecognizedTokens();
  checkRendererErrors();
  checkRendererNames();
  checkLevels();
  checkWhereFlagsEnd();
  checkLogFile();
  checkClusters();
  checkUnrecognizedOutranksEarlierEvents();
  checkLongNameRules();
  checkRenderersGuardStopsAtFirstFile();
  checkEmptyValuesActAsBareFlags();
  checkEverySpellingParses();
  checkUsageListsEveryOption();
  checkErrorText();
  return checkSummary("gman's command line parses in process");
}
