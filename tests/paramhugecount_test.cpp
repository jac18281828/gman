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
 * A RIB parameter declaring more values than an int can count, such as
 * "point[1000000000]", multiplies out past INT_MAX in the dictionary's
 * allocation arithmetic. The parser is expected to compare the supplied
 * count with the declared minimum in 64 bits and drop the parameter with a
 * logged error naming the request and the token, so no consumer reads the
 * overflowed size.
 *
 * Each fixture supplies one point; none allocates the declared size.
 */

#include <cstdio>
#include <string>

#include "check.h"
#include "paramrejection.h"

int main(int argc, char* argv[]) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s <gman-binary> <tests/rib/malformed dir>\n", argv[0]);
    return 2;
  }
  const std::string gman = argv[1];
  const std::string dir = argv[2];

  paramrejection::checkStoppedParameter(gman, dir, "paramhuge_option.rib", "Option", "point[1000000000] cloud");
  paramrejection::checkStoppedParameter(gman, dir, "paramhuge_hider.rib", "Hider", "point[1000000000] cloud");
  paramrejection::checkStoppedParameter(gman, dir, "paramhuge_display.rib", "Display", "point[1000000000] cloud");
  paramrejection::checkStoppedParameter(gman, dir, "paramhuge_surface.rib", "Surface", "point[1000000000] cloud");

  return checkSummary("A parameter declaring more than an int can count is dropped");
}
