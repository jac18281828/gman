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
 * gman -r's edge cases: a missing name, an explicit empty name, and
 * -rNAME glued into the same argv (the getopt convention). Before this
 * fix, the glued form took the *next* argv -- the rib path -- as the
 * renderer name, leaving no rib file to parse and no image written, but
 * still exiting 0.
 */

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

#include <sys/wait.h>

#include "check.h"

namespace {

int runGman(const std::string& command) {
  int status = std::system(command.c_str());
  return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

} // namespace

int main(int argc, char* argv[]) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s <gman-binary> <sphere.rib>\n", argv[0]);
    return 2;
  }
  std::string const gman = argv[1];
  std::string const rib = argv[2];

  // -r with no name at all: a usage error, not exit 0.
  int const noNameExit = runGman("\"" + gman + "\" -r >/dev/null 2>&1");
  check(noNameExit != 0, "-r with no name at all exits nonzero");

  // -r with an explicit empty name: also a usage error, not a silent
  // fall-through to the default renderer.
  int const emptyNameExit = runGman("\"" + gman + "\" -r \"\" \"" + rib + "\" >/dev/null 2>&1");
  check(emptyNameExit != 0, "-r \"\" exits nonzero");

  // -rNAME, glued: asserts the fixed behavior actually renders, which the
  // old (next-argv) parse could not -- it had already consumed the rib
  // path as the name, so nothing was ever parsed.
  std::filesystem::remove("sphere.tif");
  int const gluedExit = runGman("\"" + gman + "\" -rgmanzbuffer \"" + rib + "\" >/dev/null 2>&1");
  check(gluedExit == 0, "-rgmanzbuffer (glued) exits 0");
  check(std::filesystem::exists("sphere.tif"),
        "-rgmanzbuffer (glued) renders sphere.tif -- the rib path was not swallowed as the name");

  return checkSummary("gman -r's edge cases: missing, empty and glued names");
}
