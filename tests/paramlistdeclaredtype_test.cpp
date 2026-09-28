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
 * GMANParameterList's INTEGER branch reads its buffer through an
 * (RtInt*) cast, so a parameter stored as RtFloat regardless of its
 * declared type would come back as its bit pattern reinterpreted as an
 * integer, not its value. GMANRIBParse::parseParameterList resolves a
 * parameter's declared type from the same GMANDictionary the consumer
 * uses, through GMANRenderMan::getDictionary, and stores a
 * declared-INTEGER value as RtInt.
 *
 * Checks 1 through 4 run gman out of process against a fixture, capturing
 * its combined output under a timeout (tests/rungman.h) --
 * tests/ribmalformed_test.cpp's own checks observe neither exit status nor
 * message, so nothing here belongs there. Check 4's fixture reports
 * GMANParameterList's own "ERROR: RIE_BADTOKEN -- GMANDictionary:
 * TOKEN_NOT_FOUND", which stops the file; gman prints nothing of its
 * own, and the diagnostic is unrelated to declared-type resolution.
 *
 * Checks 1 through 4 assert only exit status and diagnostic text, so every
 * one of them would still pass if the parser stored a wrong-but-non-throwing
 * integer. Nothing in the tree reads a declared-INTEGER parameter back --
 * RI_ORIGIN is the only built-in one, and nothing consumes it -- so check 5
 * drives a RIB directly through GMANRIBParse and a GMANRenderManImpl
 * subclass overriding only RiSurfaceV, the one non-pure-virtual override
 * the RenderMan interface leaves reachable without a from-scratch,
 * 99-pure-virtual test double, to read the value the parser actually
 * produced.
 */

#include <cstdio>
#include <cstdlib>
#include <string>

#include "check.h"
#include "gmanrendermanimpl.h"
#include "gmanribparse.h"
#include "ri.h"
#include "rungman.h"

namespace {

GMANRunResult runCapturingOutput(const std::string& gman, const std::string& rib, int timeoutSeconds) {
  GMANRunOptions options;
  options.timeoutSeconds = timeoutSeconds;
  return runGman(gman, {rib}, options);
}

// Records what RiSurfaceV actually received for "myint", without keeping
// the pointer itself: parseParameterList's buffer is freed once the
// Surface request's own parseStream iteration ends, so the value is read
// out during the call, not after it returns.
class CapturingRenderMan : public GMANRenderManImpl {
public:
  bool found = false;
  RtInt capturedInt = 0;

  RtVoid RiSurfaceV(RtToken name, RtInt n, RtToken tokens[], RtPointer parms[]) override {
    for (RtInt i = 0; i < n; ++i) {
      if (std::string(tokens[i]) == "myint") {
        found = true;
        capturedInt = *(RtInt*)parms[i];
      }
    }
    GMANRenderManImpl::RiSurfaceV(name, n, tokens, parms);
  }
};

} // namespace

int main(int argc, char* argv[]) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s <gman-binary> <tests/rib/malformed dir>\n", argv[0]);
    return 2;
  }
  const std::string gman = argv[1];
  const std::string dir = argv[2];

  // 1. The discriminator: a float literal in a declared-integer parameter
  // raises RIE_SYNTAX and exits non-zero.
  {
    GMANRunResult r = runCapturingOutput(gman, dir + "/paramtype_declared_int_float_literal.rib", 10);
    check(!r.timedOut, "float-literal-in-integer: does not hang (10s bound)");
    check(!r.crashed, "float-literal-in-integer: does not crash");
    check(r.exitStatus == EXIT_FAILURE, "float-literal-in-integer: exits with failure");
    check(r.output.find("RIE_SYNTAX") != std::string::npos &&
              r.output.find("Non-integer in array.") != std::string::npos,
          "float-literal-in-integer: raises RIE_SYNTAX (\"Non-integer in array.\")");
  }

  // 2. The positive case: the same token given an integer literal parses,
  // exits 0, and emits no diagnostic.
  {
    GMANRunResult r = runCapturingOutput(gman, dir + "/paramtype_declared_int_positive.rib", 10);
    check(!r.timedOut, "declared-integer-positive: does not hang (10s bound)");
    check(!r.crashed, "declared-integer-positive: does not crash");
    check(r.exitStatus == EXIT_SUCCESS, "declared-integer-positive: exits cleanly");
    check(r.output.find("ERROR") == std::string::npos, "declared-integer-positive: emits no diagnostic");
  }

  // 3. The float regression guard: an integer literal and a mixed
  // int/real array, both in float-declared slots, keep working through the
  // float path unchanged.
  {
    GMANRunResult r = runCapturingOutput(gman, dir + "/paramtype_float_regression.rib", 10);
    check(!r.timedOut, "float-regression: does not hang (10s bound)");
    check(!r.crashed, "float-regression: does not crash");
    check(r.exitStatus == EXIT_SUCCESS, "float-regression: exits cleanly");
    check(r.output.find("ERROR") == std::string::npos, "float-regression: emits no diagnostic");
  }

  // 4. The undeclared-token report: getTokenId's RIE_BADTOKEN reaches
  // gman's error handler, which prints it and stops the file. Exit
  // failure, no crash, GMANParameterList's own diagnostic printed, and no
  // RIE_SYNTAX from a parser that instead threw.
  {
    GMANRunResult r = runCapturingOutput(gman, dir + "/paramtype_undeclared_fallback.rib", 10);
    check(!r.timedOut, "undeclared-token-report: does not hang (10s bound)");
    check(!r.crashed, "undeclared-token-report: does not crash");
    check(r.exitStatus == EXIT_FAILURE, "undeclared-token-report: exits with failure");
    check(r.output.find("RIE_BADTOKEN") != std::string::npos,
          "undeclared-token-report: GMANParameterList's own diagnostic is printed");
    check(r.output.find("RIE_SYNTAX") == std::string::npos, "undeclared-token-report: prints no RIE_SYNTAX");
  }

  // 5. The value arrives: a token declared uniform integer and given [4]
  // reaches the Ri*V boundary as RtInt 4, not as the float bit pattern of
  // 4.0f (1082130432).
  {
    CapturingRenderMan renderMan;
    const std::string rib = dir + "/paramtype_declared_int_positive.rib";
    {
      GMANRIBParse parser(renderMan, rib.c_str());
      parser.parse();
    }
    check(renderMan.found, "declared-integer-boundary: \"myint\" reaches RiSurfaceV");
    check(renderMan.capturedInt == 4, "declared-integer-boundary: arrives as RtInt 4, not float bits");
  }

  return checkSummary("A declared-integer parameter is stored as RtInt");
}
