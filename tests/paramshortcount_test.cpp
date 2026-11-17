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
 * A RIB parameter supplied with fewer values than one element of its
 * declared type makes a consumer read past the array. The parser is expected
 * to drop that one parameter with a logged error naming the request and the
 * token, and to carry on with the request and the file.
 *
 * The two controls bound the rule: exactly one element's worth of values and
 * more than that are both accepted without a word, since a consumer reads
 * only the declared length.
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

  paramrejection::checkRejectedParameter(gman, dir, "paramshort_option_float4.rib", "Option", "float[4] weights");
  paramrejection::checkRejectedParameter(gman, dir, "paramshort_hider_float4.rib", "Hider", "float[4] weights");
  paramrejection::checkRejectedParameter(gman, dir, "paramshort_display_float4.rib", "Display", "float[4] weights");
  paramrejection::checkRejectedParameter(gman, dir, "paramshort_surface_color2.rib", "Surface", "color tint");

  paramrejection::checkAcceptedParameter(gman, dir, "paramshort_option_pair_exact.rib");
  paramrejection::checkAcceptedParameter(gman, dir, "paramshort_option_pair_over.rib");

  return checkSummary("A parameter shorter than one element of its type is dropped");
}
