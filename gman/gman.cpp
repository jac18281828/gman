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
#include <format>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
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

std::string versionText() {
  std::string text = std::format("gman {}\ndrivers:", GMAN_PROJECT_VERSION);
  for (std::string const& name : gman::fileDrivers()) {
    text += std::format(" {}", name);
  }
  return text;
}

gman::BitDepth bitDepthOf(int bits) {
  if (bits == 16) {
    return gman::BitDepth::sixteen;
  }
  return gman::BitDepth::eight;
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
  std::vector<std::string> const arguments(argv, argv + argc);
  auto const parsed = gman::parseCommandLine(arguments, versionText);
  if (auto const* const leave = std::get_if<gman::CommandLineExit>(&parsed)) {
    std::cout << leave->out;
    std::cerr << leave->err;
    return leave->status;
  }
  auto const& commandLine = std::get<gman::CommandLine>(parsed);

  int rc = EXIT_SUCCESS;
  try {
    // artificial log object for log settings
    static GMANRenderManImpl renderMan;

    renderMan.RiErrorHandler(handleReport);
    if (commandLine.bitDepth) {
      renderMan.setBitDepth(bitDepthOf(*commandLine.bitDepth));
    }

    GMANLog logObj;

    // info on by default
    logObj.setLogLevel(LOGLVL_INFO);
    // announce the banner
    info("gman {} -- LGPL-2.1-or-later, see COPYING\n", GMAN_PROJECT_VERSION);

    logObj.setLogLevel(commandLine.level);

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
