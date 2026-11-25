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
 * A RIB parameter whose value is a string where its token declares a number,
 * or a number where it declares a string, is stored by its literal and read
 * by its declaration, so a consumer reads memory as the wrong type. The
 * parser is expected to stop the file with RIE_CONSISTENCY, naming the
 * request and the token, before any image is written.
 *
 * The final checks parse the Surface fixture in process against a
 * GMANRenderManImpl subclass overriding RiSurfaceV. The Surface before the
 * bad request dispatches, then the parse throws RIE_CONSISTENCY at the bad
 * one; a well-formed control dispatches both requests and the second carries
 * its three parameters.
 */

#include <cstdio>
#include <string>
#include <vector>

#include "check.h"
#include "gmanrendermanimpl.h"
#include "gmanribparse.h"
#include "paramrejection.h"
#include "ri.h"

namespace {

struct SurfaceCall {
  RtInt n;
  std::vector<std::string> tokens;
};

class RecordingRenderMan : public GMANRenderManImpl {
public:
  std::vector<SurfaceCall> surfaceCalls;

  RtVoid RiSurfaceV(RtToken, RtInt n, RtToken tokens[], RtPointer[]) override {
    SurfaceCall call{n, {}};
    for (RtInt i = 0; i < n; ++i) {
      call.tokens.push_back(tokens[i]);
    }
    surfaceCalls.push_back(call);
  }
};

bool carries(SurfaceCall const& call, std::string const& token) {
  for (std::string const& t : call.tokens) {
    if (t == token) {
      return true;
    }
  }
  return false;
}

} // namespace

int main(int argc, char* argv[]) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s <gman-binary> <tests/rib/malformed dir>\n", argv[0]);
    return 2;
  }
  const std::string gman = argv[1];
  const std::string dir = argv[2];

  paramrejection::checkStoppedParameter(gman, dir, "paramliteral_option_string_number.rib", "Option", "string mode");
  paramrejection::checkStoppedParameter(gman, dir, "paramliteral_option_declared_string.rib", "Option", "mode");
  paramrejection::checkStoppedParameter(gman, dir, "paramliteral_option_float_string.rib", "Option", "float weight");
  paramrejection::checkStoppedParameter(gman, dir, "paramliteral_hider_string_number.rib", "Hider", "string mode");
  paramrejection::checkStoppedParameter(gman, dir, "paramliteral_display_string_number.rib", "Display", "string mode");
  paramrejection::checkStoppedParameter(gman, dir, "paramliteral_surface_string_number.rib", "Surface", "string mode");

  {
    RecordingRenderMan renderMan;
    const std::string rib = dir + "/paramliteral_surface_string_number.rib";
    bool stopped = false;
    {
      GMANRIBParse parser(renderMan, rib.c_str());
      try {
        parser.parse();
      } catch (GMANError const& error) {
        stopped = error.getCode() == RIE_CONSISTENCY;
      }
    }
    check(stopped, "surface: the request with the bad parameter stops the parse with RIE_CONSISTENCY");
    check(renderMan.surfaceCalls.size() == 1, "surface: only the Surface before the bad request dispatches (" +
                                                  std::to_string(renderMan.surfaceCalls.size()) +
                                                  " Surface calls, expected 1)");
  }

  paramrejection::checkAcceptedParameter(gman, dir, "paramliteral_surface_well_formed.rib");

  {
    RecordingRenderMan renderMan;
    const std::string rib = dir + "/paramliteral_surface_well_formed.rib";
    {
      GMANRIBParse parser(renderMan, rib.c_str());
      parser.parse();
    }
    const bool twoCalls = renderMan.surfaceCalls.size() == 2;
    check(twoCalls, "surface control: both requests dispatch (" + std::to_string(renderMan.surfaceCalls.size()) +
                        " Surface calls, expected 2)");
    if (twoCalls) {
      SurfaceCall const& second = renderMan.surfaceCalls[1];
      check(second.n == 3, "surface control: the dispatched request carries three parameters");
      check(carries(second, "Ka") && carries(second, "Kd") && carries(second, "string mode"),
            "surface control: the dispatched request keeps Ka, Kd and string mode");
    }
  }

  return checkSummary("A parameter whose literal kind contradicts its declaration stops the file");
}
