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

#include <functional>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "gmanlog.h"

namespace gman {

// A command line that renders: the level, the log, the renderer, the sample depth and the RIB files, in order.
struct CommandLine {
  GMANLogLevel level = LOGLVL_INFO;
  bool writeLog = false;
  std::optional<std::string> renderer;
  std::optional<int> bitDepth; // 8 or 16 when --bit-depth is given
  std::vector<std::string> files;
};

// A command line that ends the run: help, the version or a usage error. Print out and err, then exit with status.
struct CommandLineExit {
  int status = 0;
  std::string out;
  std::string err;
};

// Parses argv, the program name first. versionText returns the text of --version without a trailing newline.
std::variant<CommandLine, CommandLineExit> parseCommandLine(std::vector<std::string> const& argv,
                                                            std::function<std::string()> const& versionText);

} // namespace gman
