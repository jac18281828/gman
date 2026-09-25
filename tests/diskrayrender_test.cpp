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
 * `gman -r gmanraytracer` renders tests/rib/r5a_disk.rib -- a
 * single matte disk, thetamax 270, under an ambient and a distant light --
 * matching a checked-in golden image. Reverting GMANRayObjectManager::
 * getRSDisk to `return create();` renders nothing but the black background,
 * failing this check.
 */

#include <cstdio>
#include <cstdlib>
#include <string>

#include <sys/wait.h>

#include "check.h"
#include "goldenimage.h"

namespace {

int runGman(const std::string& command) {
  int status = std::system(command.c_str());
  return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

} // namespace

int main(int argc, char* argv[]) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s <gman-binary> <rib-dir>\n", argv[0]);
    return 2;
  }
  std::string const gman = argv[1];
  std::string const ribDir = argv[2];
  std::string const rib = ribDir + "/r5a_disk.rib";

  std::remove("r5a_disk.tif");

  int const status = runGman("\"" + gman + "\" -r gmanraytracer \"" + rib + "\" >/dev/null 2>&1");
  check(status == 0, "r5a_disk.rib renders under -r gmanraytracer (exit " + std::to_string(status) + ")");

  checkGoldenImage("r5a_disk.tif", ribDir + "/r5a_disk_golden.tif", GOLDEN_CHANNEL_TOL, GOLDEN_MAX_FRACTION,
                   "r5a_disk_diff.tif");

  return checkSummary("R5a: gman -r gmanraytracer renders a partial-thetamax disk");
}
