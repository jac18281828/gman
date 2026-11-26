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

#include <functional>
#include <sstream>
#include <string>
#include <variant>
#include <vector>

#include <CLI/CLI.hpp>

#include "gmancommandline.h"
#include "gmanlog.h"

namespace gman {

namespace {

std::string const loggingGroup = "Logging";
std::string const defaultProgramName = "gman";

CLI::Validator const nonEmpty(
    [](std::string const& name) {
      if (name.empty()) {
        return std::string("must not be empty");
      }
      return std::string();
    },
    "");

void declareLevelFlag(CLI::App& app, CommandLine& into, std::string const& names, GMANLogLevel level,
                      std::string const& description) {
  auto const setLevel = [&into, level] { into.level = level; };
  app.add_flag_callback(names, setLevel, description)->trigger_on_parse()->group(loggingGroup);
}

void declareCommandLine(CLI::App& app, CommandLine& into, std::function<std::string()> const& versionText) {
  auto const printVersion = [&versionText] { throw CLI::CallForVersion(versionText(), 0); };
  app.add_flag_callback("--version", printVersion, "Print the version and the drivers built in, then exit")
      ->trigger_on_parse();

  declareLevelFlag(app, into, "-d,--debug", LOGLVL_DEBUG, "Log everything, debug output included");
  declareLevelFlag(app, into, "-i,--info", LOGLVL_INFO, "Log information and above (the default)");
  declareLevelFlag(app, into, "-w,--warning", LOGLVL_WARNING, "Log warnings and above");
  declareLevelFlag(app, into, "-e,--error", LOGLVL_ERROR, "Log errors and above");
  declareLevelFlag(app, into, "-q,--quiet", LOGLVL_DISASTER, "Log disasters only");
  app.add_flag("-l,--log-file", into.writeLog, "Also write a log named after each RIB file")->group(loggingGroup);

  app.add_option("-r,--renderer", into.renderer, "Renderer plugin to load (default: gmanzbuffer)")
      ->type_name("NAME")
      ->take_last()
      ->check(nonEmpty);
  app.add_option("files", into.files, "The RIB files to render, in order")->required();
}

std::vector<char const*> cStringsOf(std::vector<std::string> const& strings) {
  std::vector<char const*> pointers;
  for (std::string const& string : strings) {
    pointers.push_back(string.c_str());
  }
  return pointers;
}

} // namespace

std::variant<CommandLine, CommandLineExit> parseCommandLine(std::vector<std::string> const& argv,
                                                            std::function<std::string()> const& versionText) {
  std::vector<std::string> arguments = argv;
  if (arguments.empty()) {
    arguments.push_back(defaultProgramName);
  }
  if (arguments.size() == 1) {
    arguments.push_back("--help");
  }

  CLI::App app{"Parse RIB input files."};
  CommandLine commandLine;
  declareCommandLine(app, commandLine, versionText);

  std::vector<char const*> const pointers = cStringsOf(arguments);
  try {
    app.parse(static_cast<int>(pointers.size()), pointers.data());
  } catch (CLI::ParseError const& error) {
    std::ostringstream out;
    std::ostringstream err;
    int const status = app.exit(error, out, err);
    return CommandLineExit{status, out.str(), err.str()};
  }
  return commandLine;
}

} // namespace gman
