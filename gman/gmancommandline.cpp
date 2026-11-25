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

#include <algorithm>
#include <cstddef>
#include <format>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <CLI/CLI.hpp>

#include "gmancommandline.h"
#include "gmanlog.h"

namespace gman {

namespace {

std::string const valuelessFlagDefault = "{}";
std::string_view const emptyInlineRenderer = "--renderer=";
std::string_view const versionFlag = "--version";

using Outcome = CommandLine::Outcome;

struct DeclaredOptions {
  CLI::Option* help;
  CLI::Option* debug;
  CLI::Option* info;
  CLI::Option* warning;
  CLI::Option* error;
  CLI::Option* quiet;
  CLI::Option* logFile;
  CLI::Option* renderer;
};

CLI::Option* declareValuelessFlag(CLI::App& app, std::string const& names, std::string const& description) {
  return app.add_flag(names + valuelessFlagDefault)->description(description)->disable_flag_override();
}

CLI::Option* declareRenderer(CLI::App& app, std::string const& names, std::string const& description) {
  return app.add_option(names)
      ->description(description)
      ->type_name("renderer")
      ->multi_option_policy(CLI::MultiOptionPolicy::TakeLast);
}

DeclaredOptions declareOptions(CLI::App& app) {
  app.set_help_flag();
  app.prefix_command();
  return DeclaredOptions{
      declareValuelessFlag(app, "-h,--help", "print this help message."),
      declareValuelessFlag(app, "-d,--debug", "set logging to: debug output."),
      declareValuelessFlag(app, "-i,--info", "set logging to: information output."),
      declareValuelessFlag(app, "-w,--warning", "set logging to: warning output."),
      declareValuelessFlag(app, "-e,--error", "set logging to: error output."),
      declareValuelessFlag(app, "-q,--quiet", "set logging to: quiet, only report disasters."),
      declareValuelessFlag(app, "-l,--log-file", "enable a log based on the filename of the rib."),
      declareRenderer(app, "-r,--renderer", "choose the renderer plugin (default: gmanzbuffer)."),
  };
}

std::optional<GMANLogLevel> levelOf(DeclaredOptions const& declared, CLI::Option const* option) {
  if (option == declared.debug) {
    return LOGLVL_DEBUG;
  }
  if (option == declared.info) {
    return LOGLVL_INFO;
  }
  if (option == declared.warning) {
    return LOGLVL_WARNING;
  }
  if (option == declared.error) {
    return LOGLVL_ERROR;
  }
  if (option == declared.quiet) {
    return LOGLVL_DISASTER;
  }
  return std::nullopt;
}

CommandLine withOutcome(Outcome outcome) {
  CommandLine commandLine;
  commandLine.outcome = outcome;
  return commandLine;
}

CommandLine unrecognizedFlag(std::string token) {
  CommandLine commandLine = withOutcome(Outcome::unrecognized);
  commandLine.unrecognized = std::move(token);
  return commandLine;
}

bool givesFlagAValue(CLI::ArgumentMismatch const& error) {
  return std::string_view(error.what()).ends_with("disallowed flag override");
}

bool mentionsVersion(std::vector<std::string> const& arguments) {
  return std::any_of(arguments.begin(), arguments.end(),
                     [](std::string const& argument) { return argument == versionFlag; });
}

CommandLine readParseOrder(CLI::App const& app, DeclaredOptions const& declared) {
  CommandLine commandLine;
  std::size_t renderers = 0;
  for (CLI::Option const* occurrence : app.parse_order()) {
    if (occurrence == declared.help) {
      commandLine.outcome = Outcome::help;
      return commandLine;
    }
    if (occurrence == declared.logFile) {
      commandLine.writeLog = true;
    } else if (occurrence == declared.renderer) {
      std::string const& name = declared.renderer->results()[renderers++];
      if (name.empty()) {
        commandLine.outcome = Outcome::emptyRenderer;
        return commandLine;
      }
      commandLine.renderer = name;
    } else {
      commandLine.level = levelOf(declared, occurrence);
    }
  }
  return commandLine;
}

CommandLine parseFlags(std::vector<std::string> const& arguments) {
  CLI::App app;
  DeclaredOptions const declared = declareOptions(app);

  std::vector<std::string> unparsed(std::make_reverse_iterator(arguments.end()),
                                    std::make_reverse_iterator(arguments.begin()));
  bool renderNameMissing = false;
  bool flagGivenValue = false;
  try {
    app.parse(unparsed);
  } catch (CLI::ArgumentMismatch const& error) {
    (givesFlagAValue(error) ? flagGivenValue : renderNameMissing) = true;
  }
  auto const firstFile = arguments.end() - static_cast<std::ptrdiff_t>(unparsed.size());

  std::vector<std::string> const leftover = app.remaining();
  if (!leftover.empty() && leftover.front().starts_with('-')) {
    return unrecognizedFlag(leftover.front());
  }
  if (flagGivenValue) {
    return unrecognizedFlag(*(firstFile - 1));
  }

  if (std::any_of(arguments.begin(), firstFile,
                  [](std::string const& argument) { return argument == emptyInlineRenderer; })) {
    return withOutcome(Outcome::emptyRenderer);
  }
  CommandLine commandLine = readParseOrder(app, declared);
  if (commandLine.outcome != Outcome::run) {
    return commandLine;
  }
  if (renderNameMissing) {
    commandLine.outcome = Outcome::missingRenderer;
    return commandLine;
  }
  commandLine.files.assign(firstFile, arguments.end());
  return commandLine;
}

std::vector<std::string> dashedNames(CLI::Option const& option) {
  std::vector<std::string> names;
  for (std::string const& name : option.get_snames()) {
    names.push_back("-" + name);
  }
  for (std::string const& name : option.get_lnames()) {
    names.push_back("--" + name);
  }
  return names;
}

bool takesValue(CLI::Option const& option) { return option.get_items_expected_max() > 0; }

std::string optionNames(CLI::Option const& option) {
  std::string names;
  for (std::string const& name : dashedNames(option)) {
    names += (names.empty() ? "" : ", ") + name;
  }
  if (takesValue(option)) {
    names += " " + option.get_type_name();
  }
  return names;
}

std::string synopsis(CLI::App const& app) {
  std::string flags;
  std::string valued;
  for (CLI::Option const* option : app.get_options()) {
    std::string const& letter = option->get_snames().front();
    if (takesValue(*option)) {
      valued += " [-" + letter + " " + option->get_type_name() + "]";
    } else {
      flags += letter;
    }
  }
  return std::format("-[{}]{} [{}] files ...", flags, valued, versionFlag);
}

} // namespace

CommandLine parseCommandLine(std::vector<std::string> const& arguments) {
  if (mentionsVersion(arguments)) {
    return withOutcome(Outcome::version);
  }
  if (arguments.empty()) {
    return withOutcome(Outcome::noArguments);
  }
  return parseFlags(arguments);
}

std::string usageText(std::string_view programName) {
  CLI::App app;
  declareOptions(app);

  std::string text = std::format("{}: {}\n\tParse RIB input files.\n\n", programName, synopsis(app));
  for (CLI::Option const* option : app.get_options()) {
    text += std::format("\t{} - {}\n", optionNames(*option), option->get_description());
  }
  text += std::format("\t{} - print the version and compiled drivers, then exit.\n", versionFlag);
  return text;
}

std::string errorText(CommandLine const& commandLine, std::string_view programName) {
  switch (commandLine.outcome) {
  case Outcome::unrecognized:
    return std::format("{}: unrecognized flag {}\n{}", programName, commandLine.unrecognized, usageText(programName));
  case Outcome::missingRenderer:
    return std::format("{}: -r requires a renderer name\n", programName);
  case Outcome::emptyRenderer:
    return std::format("{}: -r requires a non-empty renderer name\n", programName);
  case Outcome::run:
  case Outcome::version:
  case Outcome::noArguments:
  case Outcome::help:
    break;
  }
  return {};
}

std::vector<std::vector<std::string>> optionSpellings() {
  CLI::App app;
  declareOptions(app);

  std::vector<std::vector<std::string>> spellings;
  for (CLI::Option const* option : app.get_options()) {
    spellings.push_back(dashedNames(*option));
  }
  spellings.push_back({std::string(versionFlag)});
  return spellings;
}

} // namespace gman
