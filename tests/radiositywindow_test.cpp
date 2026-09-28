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
 * (radiositywindow_opaque.rib) no light enters at all, so that brightening
 * disappears; with the pass named but the pane opaque or translucent
 * (radiositywindow_direct.rib), the ceiling reads whatever ambient light
 * alone gives -- here, with none, exactly zero.
 *
 * Two renders of radiositywindow.rib are also asserted byte-identical,
 * proving the solver's own determinism reaches the shipped image.
 */

#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include <sys/wait.h>

#include <tiffio.h>

#include "check.h"
#include "goldenimage.h"

namespace {

// The ceiling sampling strip: a row band just below the top image edge,
// centred columns clear of the side walls and, per the fixture's own
// window placement, clear of the window pane and anywhere its footprint
// could appear through reflection or compositing.
constexpr int kCeilingStripX = 20;
constexpr int kCeilingStripWidth = 20;
constexpr int kCeilingStripY = 1;
constexpr int kCeilingStripHeight = 6;

// Measured on this fixture triple: with the pass on, the translucent
// pane's ceiling strip reads about 39 counts brighter than the opaque
// pane's; kWindowBleedDelta sits strictly between that and the opaque
// pane's own (zero) contribution, comfortably more than two steps clear
// of each, independent of GOLDEN_CHANNEL_TOL, which bounds a different
// comparison (one render against its own golden).
constexpr double kWindowBleedDelta = 15.0;

// tests/rib/radiositywindow.rib carries no ambient light, so Cs*Ka*ambient()
// is exactly zero and every pixel in the pass-off ceiling strip must read
// exactly zero (decision on GMANOutput::save's dither: a value of exactly
// zero writes zero whatever the draw).
constexpr double kAmbientBaselineTolerance = 0.0;

struct Result {
  int exitStatus;
  std::string output;
};

Result runGman(std::string const& gman, std::string const& renderer, std::string const& rib) {
  std::string const command = "\"" + gman + "\" -r " + renderer + " \"" + rib + "\" 2>&1";
  std::FILE* pipe = popen(command.c_str(), "r");
  Result result{-1, ""};
  if (pipe == nullptr) {
    return result;
  }
  char buffer[512];
  while (std::fgets(buffer, sizeof buffer, pipe) != nullptr) {
    result.output += buffer;
  }
  int const status = pclose(pipe);
  result.exitStatus = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
  return result;
}

struct Rendered {
  Result result;
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

  // ---- check D: the window scene renders and matches its golden ----
  check(translucentOn.result.exitStatus == 0, "window: radiositywindow.rib exits 0");
  check(printsNoRadiosityWarning(translucentOn.result.output),
        "window: radiositywindow.rib prints no radiosity warning");
  check(opaqueOn.result.exitStatus == 0, "window: radiositywindow_opaque.rib exits 0");
  check(printsNoRadiosityWarning(opaqueOn.result.output),
        "window: radiositywindow_opaque.rib prints no radiosity warning");
  check(translucentOff.result.exitStatus == 0, "window: radiositywindow_direct.rib exits 0");

  check(translucentOn.image.ok && opaqueOn.image.ok && translucentOff.image.ok,
        "window: all three renders' TIFFs read back");
  if (!translucentOn.image.ok || !opaqueOn.image.ok || !translucentOff.image.ok) {
    return checkSummary("The radiosity pass, not direct-light transmission, brightens a window room's ceiling");
  }

  checkGoldenImage("radiositywindow.tif", ribDir + "/radiositywindow_golden.tif", GOLDEN_CHANNEL_TOL,
                   GOLDEN_MAX_FRACTION, "radiositywindow_diff.tif");

  // ---- check E: the pass, not direct transmission, lights the ceiling ----
  double const translucentOnMean = ceilingStripMeanRed(translucentOn.image);
  double const opaqueOnMean = ceilingStripMeanRed(opaqueOn.image);
  double const translucentOffMean = ceilingStripMeanRed(translucentOff.image);
  std::printf("window: ceiling strip mean -- translucent+pass %.2f, opaque+pass %.2f, translucent+direct %.2f\n",
              translucentOnMean, opaqueOnMean, translucentOffMean);

  check(translucentOnMean - opaqueOnMean > kWindowBleedDelta,
        "window: with the pass on, the translucent pane's ceiling strip exceeds the opaque pane's by more than "
        "kWindowBleedDelta");
  check(std::fabs(translucentOffMean - 0.0) <= kAmbientBaselineTolerance,
        "window: with the pass off, the translucent pane's ceiling strip matches the fixture's zero ambient "
        "baseline");

  // ---- check F: two renders of radiositywindow.rib are byte-identical ----
  std::vector<char> const firstBytes = readWholeFile("radiositywindow.tif");
  Rendered const onAgain = renderFixture(gman, ribDir, "radiositywindow.rib", "radiositywindow.tif");
  check(onAgain.result.exitStatus == 0, "window: the second radiositywindow.rib render exits 0");
  check(printsNoRadiosityWarning(onAgain.result.output),
        "window: the second radiositywindow.rib render prints no radiosity warning");
  std::vector<char> const secondBytes = readWholeFile("radiositywindow.tif");
  check(!firstBytes.empty() && firstBytes == secondBytes,
        "window: two renders of radiositywindow.rib are byte-identical");

  return checkSummary("The radiosity pass, not direct-light transmission, brightens a window room's ceiling");
}
