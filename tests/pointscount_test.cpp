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
 * GMANRIBParse::parsePoints derives npoints from "P"'s element count. This
 * test drives GMANRIBParse directly, in process, against a
 * GMANRenderManImpl subclass overriding only RiPointsV, following
 * tests/paramlistdeclaredtype_test.cpp's check 5 and
 * tests/ribstringescape_test.cpp's in-process check, to read the npoints
 * value the parser actually produced for each of tests/rib/pointscount.rib's
 * three Points requests.
 */

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "check.h"
#include "gmanrendermanimpl.h"
#include "gmanribparse.h"
#include "ri.h"

namespace {

// Records each RiPointsV call's npoints argument, in call order, then
// forwards to the base implementation.
class CapturingRenderMan : public GMANRenderManImpl {
public:
  std::vector<RtInt> capturedNpoints;

  RtVoid RiPointsV(RtInt npoints, RtInt n, RtToken tokens[], RtPointer parms[]) override {
    capturedNpoints.push_back(npoints);
    GMANRenderManImpl::RiPointsV(npoints, n, tokens, parms);
  }
};

} // namespace

int main(int argc, char* argv[]) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: %s <tests/rib dir>\n", argv[0]);
    return 2;
  }
  const std::string dir = argv[1];

  CapturingRenderMan renderMan;
  const std::string rib = dir + "/pointscount.rib";
  {
    GMANRIBParse parser(renderMan, rib.c_str());
    parser.parse();
  }

  check(renderMan.capturedNpoints.size() == 3, "pointscount: three Points requests reach RiPointsV");
  if (renderMan.capturedNpoints.size() == 3) {
    check(renderMan.capturedNpoints[0] == 2, "multiple-points: a 6-float \"P\" resolves to npoints 2");
    check(renderMan.capturedNpoints[1] == 0, "no-P: a differently-named multiple-of-three parameter leaves npoints 0");
    check(renderMan.capturedNpoints[2] == 0,
          "malformed-P: a 4-float \"P\" (not a multiple of three) resolves to npoints 0");
  }

  return checkSummary("Points' npoints is derived from P's element count");
}
