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

#include <iostream>
#include <memory>
#include <string>
#include <string_view>

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

      GMANLog logObj;

      // info on by default
      logObj.setLogLevel(LOGLVL_INFO);
      // announce the banner
      info("gman {} -- LGPL-2.1-or-later, see COPYING\n", GMAN_PROJECT_VERSION);

      bool writeLog = false;
      // Empty means -r was never given; GMANRIBParse's own default
      // ("gmanzbuffer") applies by omitting the argument entirely below,
      // not by repeating that name here.
      std::string rendererName;
      bool rendererNameGiven = false;

      int arg = 1;

      while ((arg < argc) && (argv[arg][0] == '-')) {
        switch (argv[arg][1]) {
        case 'd':
          logObj.setLogLevel(LOGLVL_DEBUG);
          break;
        case 'e':
          logObj.setLogLevel(LOGLVL_ERROR);
          break;
        case 'h':
          usage(argv[0]);
          exit(EXIT_SUCCESS);
        case 'i':
          logObj.setLogLevel(LOGLVL_INFO);
          break;
        case 'l':
          writeLog = true;
          break;
        case 'q':
          logObj.setLogLevel(LOGLVL_DISASTER);
          break;
        case 'r':
          // -rNAME (glued, the getopt convention) or -r NAME (the next
          // argv); either way, an empty name is a usage error, not a
          // silent fall-through to the default.
          if (argv[arg][2] != '\0') {
            rendererName = argv[arg] + 2;
          } else if (arg + 1 < argc) {
            rendererName = argv[++arg];
          } else {
            std::cerr << argv[0] << ": -r requires a renderer name" << std::endl;
            return EXIT_FAILURE;
          }
          if (rendererName.empty()) {
            std::cerr << argv[0] << ": -r requires a non-empty renderer name" << std::endl;
            return EXIT_FAILURE;
          }
          rendererNameGiven = true;
          break;
        case 'w':
          logObj.setLogLevel(LOGLVL_WARNING);
          break;
        }
        arg++;
      }

      for (int i = arg; i < argc; i++) {
        try {
          const char* ribFile = argv[i];
          info("Parsing {}", argv[i]);

          if (writeLog) {
            const std::string fileName = logFileNameFor(ribFile);
            logObj.setLogFile(fileName.c_str());
          }

          std::unique_ptr<GMANRIBParse> const parser =
              rendererNameGiven ? std::make_unique<GMANRIBParse>(renderMan, argv[i], rendererName.c_str())
                                : std::make_unique<GMANRIBParse>(renderMan, argv[i]);

          // just parse it...
          parser->parse();

        } catch (GMANError& e) {
          GMANHandleError(e);
          rc = EXIT_FAILURE;
        }
      }
    } catch (GMANError& e) {
      GMANHandleError(e);
      rc = EXIT_FAILURE;
    }
  }
  return rc;
}

/* Are you freaking kidding? */
RtVoid usage(char* myname) {

  std::cerr << myname << ": -[hdiweql] [-r renderer] [--version] files ..." << std::endl;
  std::cerr << "\tParse RIB input files." << std::endl << std::endl;
  std::cerr << "\t-h - print this help message." << std::endl;
  std::cerr << "\t-d - set logging to: debug output." << std::endl;
  std::cerr << "\t-i - set logging to: information output." << std::endl;
  std::cerr << "\t-w - set logging to: warning output." << std::endl;
  std::cerr << "\t-e - set logging to: error output." << std::endl;
  std::cerr << "\t-q - set logging to: quiet, only report disasters." << std::endl;
  std::cerr << "\t-l - enable a log based on the filename of the rib." << std::endl;
  std::cerr << "\t-r renderer - choose the renderer plugin (default: gmanzbuffer)." << std::endl;
  std::cerr << "\t--version - print the version and compiled drivers, then exit." << std::endl;
}
