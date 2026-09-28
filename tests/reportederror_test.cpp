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
 * gman's own error handler prints every report exactly as RiErrorPrint
 * does, then stops the file being parsed at RIE_ERROR or above: no image
 * written, exit 1, and the next file on the command line still runs.
 * Below that threshold nothing stops the file -- reportederror_warning.rib
 * only ever warns, through gmanlog's PixelSamples clamp, since no RIB
 * request in this tree reaches the handler below RIE_ERROR. Three
 * fixtures cover the three paths: reportederror.rib reports RIE_BADTOKEN
 * parsing Projection, before WorldBegin; reportederror_thrown.rib throws
 * RIE_SYNTAX declaring a bad type. Each runs alone and paired with a file
 * that still renders, out of process through tests/rungman.h's runGman,
 * every exit-status check requiring crashed and timedOut false.
 */

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include <tiffio.h>

#include "check.h"
#include "goldenimage.h"
#include "rungman.h"

namespace {

// Counts non-overlapping occurrences of needle in haystack.
int countOccurrences(std::string const& haystack, std::string const& needle) {
  int count = 0;
  std::size_t pos = 0;
  while ((pos = haystack.find(needle, pos)) != std::string::npos) {
    ++count;
    pos += needle.size();
  }
  return count;
}

GMANRunResult runReporting(std::string const& gman, std::vector<std::string> const& ribs) {
  GMANRunOptions options;
  options.timeoutSeconds = 10;
  return runGman(gman, ribs, options);
}

} // namespace

int main(int argc, char* argv[]) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s <gman-binary> <tests/rib dir>\n", argv[0]);
    return 2;
  }
  const std::string gman = argv[1];
  const std::string ribDir = argv[2];

  const std::string reportedTif = "reportederror.tif";
  const std::string warningTif = "reportederror_warning.tif";
  const std::string thrownTif = "reportederror_thrown.tif";

  // 1. A reported error at RIE_ERROR stops the file: exit 1, no image,
  // the report printed once, no SEVERE.
  {
    std::remove(reportedTif.c_str());
    GMANRunResult const r = runReporting(gman, {ribDir + "/reportederror.rib"});
    check(!r.crashed && !r.timedOut, "reportederror.rib: does not crash or hang");
    check(r.exitStatus == EXIT_FAILURE, "reportederror.rib: exits 1");
    check(!readGmanTIFF(reportedTif).ok, "reportederror.rib: writes no reportederror.tif");
    check(countOccurrences(r.output, "ERROR: RIE_BADTOKEN -- GMANDictionary: TOKEN_NOT_FOUND") == 1,
          "reportederror.rib: prints the RIE_BADTOKEN report exactly once");
    check(r.output.find("SEVERE:") == std::string::npos, "reportederror.rib: prints no SEVERE");
  }

  // 2. Below RIE_ERROR, nothing stops the file: the PixelSamples clamp is
  // a gmanlog warning, not an RI report, and the scene renders on.
  {
    std::remove(warningTif.c_str());
    GMANRunResult const r = runReporting(gman, {ribDir + "/reportederror_warning.rib"});
    check(!r.crashed && !r.timedOut, "reportederror_warning.rib: does not crash or hang");
    check(r.exitStatus == EXIT_SUCCESS, "reportederror_warning.rib: exits 0");
    GmanImage const image = readGmanTIFF(warningTif);
    check(image.ok, "reportederror_warning.rib: writes reportederror_warning.tif");
    if (image.ok) {
      uint32_t const corner = image.at(0, 0);
      check(TIFFGetA(corner) == 0, "reportederror_warning.rib: corner pixel alpha is 0");
      uint32_t const centre = image.at(image.width / 2, image.height / 2);
      check(TIFFGetA(centre) > 0, "reportederror_warning.rib: centre pixel alpha is above 0");
      check(TIFFGetR(centre) > 0 || TIFFGetG(centre) > 0 || TIFFGetB(centre) > 0,
            "reportederror_warning.rib: centre pixel has a colour channel above 0");
    }
    check(r.output.find("xsamples 0 out of [1,16], clamping.") != std::string::npos,
          "reportederror_warning.rib: prints the clamp warning");
    check(r.output.find("ERROR:") == std::string::npos, "reportederror_warning.rib: prints no ERROR");
    check(r.output.find("SEVERE:") == std::string::npos, "reportederror_warning.rib: prints no SEVERE");
  }

  // 3. A stop ends its file and main moves to the next: the first file
  // writes nothing, the second still renders, and the exit status carries
  // the stop.
  {
    std::remove(reportedTif.c_str());
    std::remove(warningTif.c_str());
    GMANRunResult const r = runReporting(gman, {ribDir + "/reportederror.rib", ribDir + "/reportederror_warning.rib"});
    check(!r.crashed && !r.timedOut, "reportederror.rib + reportederror_warning.rib: does not crash or hang");
    check(r.exitStatus == EXIT_FAILURE, "reportederror.rib + reportederror_warning.rib: exits 1");
    check(!readGmanTIFF(reportedTif).ok, "reportederror.rib + reportederror_warning.rib: writes no reportederror.tif");
    check(readGmanTIFF(warningTif).ok,
          "reportederror.rib + reportederror_warning.rib: writes reportederror_warning.tif");
    check(countOccurrences(r.output, "ERROR: RIE_BADTOKEN -- GMANDictionary: TOKEN_NOT_FOUND") == 1,
          "reportederror.rib + reportederror_warning.rib: prints the RIE_BADTOKEN report exactly once");
  }

  // 4. A thrown error keeps its own behaviour: exit 1, no image, the
  // report printed once, and the next file still renders.
  {
    std::remove(thrownTif.c_str());
    std::remove(warningTif.c_str());
    GMANRunResult const r =
        runReporting(gman, {ribDir + "/reportederror_thrown.rib", ribDir + "/reportederror_warning.rib"});
    check(!r.crashed && !r.timedOut, "reportederror_thrown.rib + reportederror_warning.rib: does not crash or hang");
    check(r.exitStatus == EXIT_FAILURE, "reportederror_thrown.rib + reportederror_warning.rib: exits 1");
    check(!readGmanTIFF(thrownTif).ok,
          "reportederror_thrown.rib + reportederror_warning.rib: writes no reportederror_thrown.tif");
    check(readGmanTIFF(warningTif).ok,
          "reportederror_thrown.rib + reportederror_warning.rib: writes reportederror_warning.tif");
    check(countOccurrences(r.output, "ERROR: RIE_SYNTAX -- BAD_SYNTAX") == 1,
          "reportederror_thrown.rib + reportederror_warning.rib: prints the RIE_SYNTAX report exactly once");
  }

  return checkSummary("gman stops a RIB file at any error it reports at RIE_ERROR or above");
}
