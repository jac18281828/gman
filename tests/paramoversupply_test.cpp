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
 * A RIB-supplied parameter array longer than its declared class already
 * truncated safely -- copy_float/copy_integer/copy_string clamp to the
 * declared length -- but did so without a word to the scene author, so a
 * miscounted array vanished its excess values with no trace. gman is now
 * expected to warn once, naming the parameter and both lengths, and still
 * truncate exactly as before.
 *
 * The second fixture re-runs the existing under-supply warning verbatim,
 * proving the new over-supply branch leaves that message untouched.
 *
 * A truncated request still draws: each fixture covers pixels where its
 * sphere lands, and the scene completes past it.
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "check.h"
#include "imagecoverage.h"
#include "rungman.h"

namespace {

GMANRunResult runCapturingOutput(const std::string& gman, const std::string& rib, int timeoutSeconds) {
  GMANRunOptions options;
  options.timeoutSeconds = timeoutSeconds;
  return runGman(gman, {rib}, options);
}

} // namespace

int main(int argc, char* argv[]) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s <gman-binary> <tests/rib/malformed dir>\n", argv[0]);
    return 2;
  }
  const std::string gman = argv[1];
  const std::string dir = argv[2];

  {
    const std::string rib = dir + "/sphere_over_n.rib";
    std::remove(malformedImagePath);
    GMANRunResult r = runCapturingOutput(gman, rib, 10);
    check(!r.timedOut, "sphere_over_n.rib: does not hang (10s bound)");
    check(!r.crashed, "sphere_over_n.rib: does not crash");
    check(r.exitStatus == 0, "sphere_over_n.rib: exits cleanly (degrade, don't abort)");
    check(r.output.find("\"N\"") != std::string::npos && r.output.find("declared length 12") != std::string::npos &&
              r.output.find("supplied length 18") != std::string::npos,
          "sphere_over_n.rib: warns naming the over-long parameter and both lengths");
    checkDegradedRender("sphere_over_n.rib", 3500);
  }

  {
    const std::string rib = dir + "/sphere_short_n.rib";
    std::remove(malformedImagePath);
    GMANRunResult r = runCapturingOutput(gman, rib, 10);
    check(r.output.find("Parameter \"N\": declared length 12, supplied length 3; "
                        "clamping and zero-filling the remainder.") != std::string::npos,
          "sphere_short_n.rib: the existing under-supply warning is unchanged");
    checkDegradedRender("sphere_short_n.rib", 3500);
  }

  return checkSummary("GMANParameterList warns on an over-long RIB parameter array");
}
