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

#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "gmanlog.h"

namespace gman {

// What gman's command line asks for. The parse does no I/O: the caller prints, exits and renders.
struct CommandLine {
  enum class Outcome { run, version, noArguments, help, unrecognized, missingRenderer, emptyRenderer };
  Outcome outcome = Outcome::run;
  // The argument the unrecognized-flag error names; set when outcome is unrecognized.
  std::string unrecognized;
  // The last level flag's level; empty when none was given.
  std::optional<GMANLogLevel> level;
  bool writeLog = false;
  // The last renderer name given; empty when none was.
  std::optional<std::string> renderer;
  // The RIB files, in order; set when outcome is run.
  std::vector<std::string> files;
};

// Parses argv[1..argc), the program name excluded.
CommandLine parseCommandLine(std::vector<std::string> const& arguments);

// The usage text, for a program invoked as programName.
std::string usageText(std::string_view programName);

// The whole stderr text of an error outcome; empty for every other outcome.
std::string errorText(CommandLine const& commandLine, std::string_view programName);

// The spellings of each option the usage text lists, in its order, --version last.
std::vector<std::vector<std::string>> optionSpellings();

} // namespace gman
