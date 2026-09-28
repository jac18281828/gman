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
 * gman's opening banner: one line, "gman <version> -- LGPL-2.1-or-later,
 * see COPYING", in place of the four-line license recitation GMANLog used
 * to log. Pins that the banner is stdout's first line and that neither the
 * old notice's "redistribute" nor its Pixar attribution survives.
 */

#include <cstdio>
#include <cstdlib>
#include <string>

#include "check.h"
#include "rungman.h"

namespace {

// stdout's first line, without its terminating newline.
std::string firstLine(std::string const& text) {
  const auto eol = text.find('\n');
  return text.substr(0, eol);
}

} // namespace

int main(int argc, char* argv[]) {
  if (argc != 4) {
    std::fprintf(stderr, "usage: %s <gman-binary> <sphere.rib> <version>\n", argv[0]);
    return 2;
  }
  const std::string gman = argv[1];
  const std::string rib = argv[2];
  const std::string expected = "gman " + std::string(argv[3]) + " -- LGPL-2.1-or-later, see COPYING";

  GMANRunOptions options;
  options.capture = GMANRunOptions::Capture::stdoutOnly;
  GMANRunResult const run = runGman(gman, {rib}, options);
  check(run.exitStatus == 0, "gman exits 0");
  check(firstLine(run.output) == expected, "gman's first stdout line is the banner (\"" + expected + "\")");
  check(run.output.find("redistribute") == std::string::npos,
        "the retired license recitation's \"redistribute\" is gone");
  check(run.output.find("Pixar") == std::string::npos, "the retired license recitation's Pixar attribution is gone");

  return checkSummary("gman's banner names its version and license, nothing more");
}
