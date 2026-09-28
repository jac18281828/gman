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
 * `gman -r gmanraytracer` renders tests/rib/radiositywindow.rib, a closed
 * room lit only by a distantlight aimed steeply downward through a
 * translucent window pane: the light reaches the floor but, since the
 * ceiling's own inward normal faces away from any light arriving from
 * above, never the ceiling directly. Naming the radiosity pass, the lit
 * floor's own indirect bounce brightens the ceiling; with the pane opaque
 * (radiositywindow_opaque.rib), still naming the pass, no light enters at
 * all, so that brightening disappears; with the pane translucent but the
 * pass omitted (radiositywindow_direct.rib), the ceiling reads whatever
 * ambient light alone gives: here, with none, exactly zero, the same
 * reading the opaque pane gives with the pass named.
 *
 * Two renders of radiositywindow.rib are also asserted byte-identical,
 * proving the solver's own determinism reaches the shipped image.
 */

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <tiffio.h>

#include "check.h"
#include "goldenimage.h"
#include "rungman.h"

namespace {

// The ceiling sampling strip: rows 0 through 5 stay above the row where
// the fixture's camera and geometry place the ceiling-back-wall seam, and
// columns 22 through 41 sit on the 64-pixel frame's own vertical centre,
// clear of the side walls and the window pane in every fixture.
constexpr int kCeilingStripX = 22;
constexpr int kCeilingStripWidth = 20;
constexpr int kCeilingStripY = 0;
constexpr int kCeilingStripHeight = 6;

// kWindowBleedDelta sits strictly between the pass-on translucent pane's
// ceiling brightening and the opaque pane's own zero contribution,
// comfortably more than two steps clear of each, independent of
// GOLDEN_CHANNEL_TOL, which bounds a different comparison: one render
// against its own golden.
constexpr double kWindowBleedDelta = 15.0;

// tests/rib/radiositywindow.rib carries no ambient light, so Cs*Ka*ambient()
// is exactly zero, and a value of exactly zero writes zero whatever
// GMANOutput::save's dither draws: every pixel in the pass-off ceiling
// strip must read exactly kPassOffCeilingBaseline.
constexpr double kPassOffCeilingBaseline = 0.0;
constexpr double kAmbientBaselineTolerance = 0.0;

GMANRunResult runGman(std::string const& gman, std::string const& renderer, std::string const& rib) {
  return ::runGman(gman, {"-r", renderer, rib});
}

struct Rendered {
  GMANRunResult result;
  GmanImage image;
};

Rendered renderFixture(std::string const& gman, std::string const& ribDir, std::string const& ribName,
                       std::string const& tifName) {
  std::remove(tifName.c_str());
  Rendered rendered;
  rendered.result = runGman(gman, "gmanraytracer", ribDir + "/" + ribName);
  rendered.image = readGmanTIFF(tifName);
  return rendered;
}

std::vector<char> readWholeFile(std::string const& path) {
  std::ifstream in(path, std::ios::binary);
  return std::vector<char>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

bool printsNoRadiosityWarning(std::string const& output) {
  return output.find("Indirect-light pass \"radiosity\"") == std::string::npos &&
         output.find("Radiosity:") == std::string::npos;
}

double ceilingStripMeanRed(GmanImage const& image) {
  double sum = 0;
  long count = 0;
  for (int y = kCeilingStripY; y < kCeilingStripY + kCeilingStripHeight; ++y) {
    for (int x = kCeilingStripX; x < kCeilingStripX + kCeilingStripWidth; ++x) {
      sum += TIFFGetR(image.at((uint32_t)x, (uint32_t)y));
      ++count;
    }
  }
  return sum / (double)count;
}

// The window scene renders and matches its golden.
void checkGolden(Rendered const& translucentOn, std::string const& ribDir) {
  check(translucentOn.result.exitStatus == 0, "window: radiositywindow.rib exits 0");
  check(printsNoRadiosityWarning(translucentOn.result.output),
        "window: radiositywindow.rib prints no radiosity warning");
  check(translucentOn.image.ok, "window: radiositywindow.rib's TIFF reads back");
  if (!translucentOn.image.ok) {
    return;
  }

  checkGoldenImage("radiositywindow.tif", ribDir + "/radiositywindow_golden.tif", GOLDEN_CHANNEL_TOL,
                   GOLDEN_MAX_FRACTION, "radiositywindow_diff.tif");
}

// The pass, not direct-light transmission, lights the ceiling: with the
// pass on, the translucent pane's ceiling strip reads brighter than the
// opaque pane's; with the pass off, the translucent pane's ceiling strip
// matches the fixture's zero ambient baseline.
void checkCeiling(Rendered const& translucentOn, Rendered const& opaqueOn, Rendered const& translucentOff) {
  check(opaqueOn.result.exitStatus == 0, "window: radiositywindow_opaque.rib exits 0");
  check(printsNoRadiosityWarning(opaqueOn.result.output),
        "window: radiositywindow_opaque.rib prints no radiosity warning");
  check(translucentOff.result.exitStatus == 0, "window: radiositywindow_direct.rib exits 0");

  check(translucentOn.image.ok && opaqueOn.image.ok && translucentOff.image.ok,
        "window: all three renders' TIFFs read back");
  if (!translucentOn.image.ok || !opaqueOn.image.ok || !translucentOff.image.ok) {
    return;
  }

  double const translucentOnMean = ceilingStripMeanRed(translucentOn.image);
  double const opaqueOnMean = ceilingStripMeanRed(opaqueOn.image);
  double const translucentOffMean = ceilingStripMeanRed(translucentOff.image);
  std::printf("window: ceiling strip mean: translucent+pass %.2f, opaque+pass %.2f, translucent+direct %.2f\n",
              translucentOnMean, opaqueOnMean, translucentOffMean);

  check(translucentOnMean - opaqueOnMean > kWindowBleedDelta,
        "window: with the pass on, the translucent pane's ceiling strip exceeds the opaque pane's by more than "
        "kWindowBleedDelta");
  check(std::fabs(translucentOffMean - kPassOffCeilingBaseline) <= kAmbientBaselineTolerance,
        "window: with the pass off, the translucent pane's ceiling strip matches the fixture's zero ambient "
        "baseline");
}

// Two renders of radiositywindow.rib agree byte for byte: the solver draws
// no random numbers, so determinism reaches the shipped image, not only
// GMANRadiositySolver's own unit tests. Reads the first render's bytes,
// already on disk from main's own render, before the second overwrites
// the file the RIB's Display line always names.
void checkDeterminism(std::string const& gman, std::string const& ribDir) {
  std::vector<char> const firstBytes = readWholeFile("radiositywindow.tif");
  Rendered const again = renderFixture(gman, ribDir, "radiositywindow.rib", "radiositywindow.tif");
  check(again.result.exitStatus == 0, "window: the second radiositywindow.rib render exits 0");
  check(printsNoRadiosityWarning(again.result.output),
        "window: the second radiositywindow.rib render prints no radiosity warning");
  std::vector<char> const secondBytes = readWholeFile("radiositywindow.tif");
  check(!firstBytes.empty() && firstBytes == secondBytes,
        "window: two renders of radiositywindow.rib are byte-identical");
}

} // namespace

int main(int argc, char* argv[]) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s <gman-binary> <tests/rib-dir>\n", argv[0]);
    return 2;
  }
  std::string const gman = argv[1];
  std::string const ribDir = argv[2];

  Rendered const translucentOn = renderFixture(gman, ribDir, "radiositywindow.rib", "radiositywindow.tif");
  Rendered const opaqueOn = renderFixture(gman, ribDir, "radiositywindow_opaque.rib", "radiositywindow_opaque.tif");
  Rendered const translucentOff =
      renderFixture(gman, ribDir, "radiositywindow_direct.rib", "radiositywindow_direct.tif");

  checkGolden(translucentOn, ribDir);
  checkCeiling(translucentOn, opaqueOn, translucentOff);
  checkDeterminism(gman, ribDir);

  return checkSummary("The radiosity pass, not direct-light transmission, brightens a window room's ceiling");
}
