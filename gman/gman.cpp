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

#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "gmancommandline.h"
#include "gmanerror.h"
#include "gmanfiledrivers.h"
#include "gmanlog.h"
#include "gmanrendermanimpl.h"
#include "gmanribparse.h"
#include "ri.h"

namespace {

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

std::unique_ptr<GMANRIBParse> makeParser(GMANRenderManImpl& renderMan, std::string const& ribFile,
                                         std::optional<std::string> const& renderer) {
  if (renderer) {
    return std::make_unique<GMANRIBParse>(renderMan, ribFile.c_str(), renderer->c_str());
  }
  return std::make_unique<GMANRIBParse>(renderMan, ribFile.c_str());
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

int main(int argc, char* argv[]) {
  using Outcome = gman::CommandLine::Outcome;

  std::vector<std::string> const arguments(argv + 1, argv + argc);
  gman::CommandLine const commandLine = gman::parseCommandLine(arguments);
  if (commandLine.outcome == Outcome::version) {
    return printVersion();
  }
  if (commandLine.outcome == Outcome::noArguments) {
    std::cerr << gman::usageText(argv[0]);
    return EXIT_SUCCESS;
  }

  int rc = EXIT_SUCCESS;
  try {
    // artificial log object for log settings
    static GMANRenderManImpl renderMan;

    renderMan.RiErrorHandler(handleReport);

    GMANLog logObj;

    // info on by default
    logObj.setLogLevel(LOGLVL_INFO);
    // announce the banner
    info("gman {} -- LGPL-2.1-or-later, see COPYING\n", GMAN_PROJECT_VERSION);

    switch (commandLine.outcome) {
    case Outcome::help:
      std::cerr << gman::usageText(argv[0]);
      std::exit(EXIT_SUCCESS);
    case Outcome::unrecognized:
    case Outcome::missingRenderer:
    case Outcome::emptyRenderer:
      std::cerr << gman::errorText(commandLine, argv[0]);
      return EXIT_FAILURE;
    case Outcome::run:
    case Outcome::version:
    case Outcome::noArguments:
      break;
    }
    if (commandLine.level) {
      logObj.setLogLevel(*commandLine.level);
    }

    for (std::string const& ribFile : commandLine.files) {
      try {
        info("Parsing {}", ribFile);

        if (commandLine.writeLog) {
          const std::string fileName = logFileNameFor(ribFile);
          logObj.setLogFile(fileName.c_str());
        }

        std::unique_ptr<GMANRIBParse> const parser = makeParser(renderMan, ribFile, commandLine.renderer);

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
  return rc;
}
