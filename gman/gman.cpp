/* SPDX-License-Identifier: LGPL-2.1-or-later */

/*
 * This is part of GMAN, a RenderMan-compatible renderer.
 *
 * Copyright (c) 2001, 2000, 1999, John Cairns
 *
 * Author: John Cairns <john@2ad.com>
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
#include <cstdlib>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <CLI/CLI.hpp>

#include "gmanerror.h"
#include "gmanfiledrivers.h"
#include "gmanlog.h"
#include "gmanrendermanimpl.h"
#include "gmanribparse.h"
#include "ri.h"

namespace {

std::string const valuelessFlagDefault = "{}";
std::string_view const emptyInlineRenderer = "--renderer=";

// A path of four or more characters whose fourth-from-last character is
// '.' has those four characters replaced with ".log"; every other path
// has ".log" appended.
std::string logFileNameFor(std::string_view ribPath) {
  if (ribPath.size() >= 4 && ribPath[ribPath.size() - 4] == '.') {
    return std::string(ribPath.substr(0, ribPath.size() - 4)) + ".log";
  }
  return std::string(ribPath) + ".log";
}

// The whole of --version's contract: two lines to stdout, parsing no
// files.
int printVersion() {
  std::cout << "gman " << GMAN_PROJECT_VERSION << "\n";
  std::cout << "drivers:";
  for (std::string const& name : gman::fileDrivers()) {
    std::cout << ' ' << name;
  }
  std::cout << "\n";
  return EXIT_SUCCESS;
}

// True when --version appears anywhere in argv[1..argc), ahead of the
// banner and every other flag: version reporting parses no files and
// touches nothing else main sets up.
bool hasVersionFlag(int argc, char* argv[]) {
  for (int i = 1; i < argc; ++i) {
    if (std::string_view(argv[i]) == "--version") {
      return true;
    }
  }
  return false;
}

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

enum class Outcome { run, help, unrecognized, missingRenderer, emptyRenderer };

struct CommandLine {
  Outcome outcome = Outcome::run;
  std::string unrecognized;
  std::optional<GMANLogLevel> level;
  bool writeLog = false;
  std::optional<std::string> renderer;
  int firstFile = 0;
};

bool givesFlagAValue(CLI::ArgumentMismatch const& error) {
  return std::string_view(error.what()).ends_with("disallowed flag override");
}

CommandLine unrecognizedFlag(std::string token) {
  CommandLine commandLine;
  commandLine.outcome = Outcome::unrecognized;
  commandLine.unrecognized = std::move(token);
  return commandLine;
}

CommandLine parseCommandLine(int argc, char* argv[]) {
  CLI::App app;
  DeclaredOptions const declared = declareOptions(app);

  std::vector<std::string> args(std::make_reverse_iterator(argv + argc), std::make_reverse_iterator(argv + 1));
  bool renderNameMissing = false;
  bool flagGivenValue = false;
  try {
    app.parse(args);
  } catch (CLI::ArgumentMismatch const& error) {
    (givesFlagAValue(error) ? flagGivenValue : renderNameMissing) = true;
  }
  int const flagsEnd = argc - static_cast<int>(args.size());

  std::vector<std::string> const leftover = app.remaining();
  if (!leftover.empty() && leftover.front().starts_with('-')) {
    return unrecognizedFlag(leftover.front());
  }
  if (flagGivenValue) {
    return unrecognizedFlag(argv[flagsEnd - 1]);
  }

  CommandLine commandLine;
  commandLine.firstFile = flagsEnd;
  if (std::any_of(argv + 1, argv + flagsEnd, [](char const* arg) { return arg == emptyInlineRenderer; })) {
    commandLine.outcome = Outcome::emptyRenderer;
    return commandLine;
  }
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
  if (renderNameMissing) {
    commandLine.outcome = Outcome::missingRenderer;
  }
  return commandLine;
}

bool takesValue(CLI::Option const& option) { return option.get_items_expected_max() > 0; }

std::string optionNames(CLI::Option const& option) {
  std::string names;
  for (std::string const& name : option.get_snames()) {
    names += (names.empty() ? "-" : ", -") + name;
  }
  for (std::string const& name : option.get_lnames()) {
    names += (names.empty() ? "--" : ", --") + name;
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
  return "-[" + flags + "]" + valued + " [--version] files ...";
}

// Thrown by handleReport once a report has already printed, so main's
// catch sites can end the file without printing it again.
class ReportedStop : public GMANError {
public:
  ReportedStop(RtInt code, RtInt severity, char const* message) : GMANError(code, severity, message) {}
};

// gman's own error handler: prints every report exactly as RiErrorPrint
// does, then stops the file being parsed at RIE_ERROR or above by
// throwing ReportedStop. The library's own default handler stays
// RiErrorPrint, for an embedder to replace with its own policy.
RtVoid handleReport(RtInt code, RtInt severity, char const* message) {
  RiErrorPrint(code, severity, message);
  if (severity >= RIE_ERROR) {
    throw ReportedStop(code, severity, message);
  }
}

} // namespace

/* function prototypes */

RtVoid usage(char* myname);

int main(int argc, char* argv[]) {

  if (hasVersionFlag(argc, argv)) {
    return printVersion();
  }

  int rc = EXIT_SUCCESS;
  // handle command line arguments
  if (argc < 2) {
    usage(argv[0]);
  } else {
    try {
      // artificial log object for log settings
      static GMANRenderManImpl renderMan;

      renderMan.RiErrorHandler(handleReport);

      GMANLog logObj;

      // info on by default
      logObj.setLogLevel(LOGLVL_INFO);
      // announce the banner
      info("gman {} -- LGPL-2.1-or-later, see COPYING\n", GMAN_PROJECT_VERSION);

      CommandLine const commandLine = parseCommandLine(argc, argv);
      switch (commandLine.outcome) {
      case Outcome::help:
        usage(argv[0]);
        exit(EXIT_SUCCESS);
      case Outcome::unrecognized:
        std::cerr << argv[0] << ": unrecognized flag " << commandLine.unrecognized << std::endl;
        usage(argv[0]);
        return EXIT_FAILURE;
      case Outcome::missingRenderer:
        std::cerr << argv[0] << ": -r requires a renderer name" << std::endl;
        return EXIT_FAILURE;
      case Outcome::emptyRenderer:
        std::cerr << argv[0] << ": -r requires a non-empty renderer name" << std::endl;
        return EXIT_FAILURE;
      case Outcome::run:
        break;
      }
      if (commandLine.level) {
        logObj.setLogLevel(*commandLine.level);
      }

      for (int i = commandLine.firstFile; i < argc; i++) {
        try {
          const char* ribFile = argv[i];
          info("Parsing {}", argv[i]);

          if (commandLine.writeLog) {
            const std::string fileName = logFileNameFor(ribFile);
            logObj.setLogFile(fileName.c_str());
          }

          std::unique_ptr<GMANRIBParse> const parser =
              commandLine.renderer ? std::make_unique<GMANRIBParse>(renderMan, argv[i], commandLine.renderer->c_str())
                                   : std::make_unique<GMANRIBParse>(renderMan, argv[i]);

          // just parse it...
          parser->parse();

        } catch (ReportedStop&) {
          rc = EXIT_FAILURE;
        } catch (GMANError& e) {
          RiErrorPrint(e.getCode(), e.getSeverity(), e.getMessage());
          rc = EXIT_FAILURE;
        }
      }
    } catch (ReportedStop&) {
      rc = EXIT_FAILURE;
    } catch (GMANError& e) {
      RiErrorPrint(e.getCode(), e.getSeverity(), e.getMessage());
      rc = EXIT_FAILURE;
    }
  }
  return rc;
}

RtVoid usage(char* myname) {
  CLI::App app;
  declareOptions(app);

  std::cerr << myname << ": " << synopsis(app) << std::endl;
  std::cerr << "\tParse RIB input files." << std::endl << std::endl;
  for (CLI::Option const* option : app.get_options()) {
    std::cerr << '\t' << optionNames(*option) << " - " << option->get_description() << std::endl;
  }
  std::cerr << "\t--version - print the version and compiled drivers, then exit." << std::endl;
}
