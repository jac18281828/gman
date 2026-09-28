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
 * `gman -r gmanraytracer` renders tests/rib/cornellbox.rib, a closed box
 * with red and green side walls placed symmetrically about the light,
 * naming the radiosity pass: the back wall reads redder near the red wall
 * and greener near the green wall, purely from colour bleeding, since the
 * point light itself sits on the box's central plane. cornellbox_direct.rib
 * is the same scene naming no pass, where that asymmetry vanishes.
 *
 * Two renders of cornellbox.rib are also asserted byte-identical: the
 * solver draws no random numbers, so this proves that determinism reaches
 * the shipped image, not only GMANRadiositySolver's own unit tests.
 */

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include <sys/wait.h>

#include <tiffio.h>

#include "check.h"
#include "goldenimage.h"

namespace {

// The back-wall sampling strips: columns just inside the red and green
// walls' own edges (verified against the fixture's render to fall on the
// back wall itself, not the side walls), and a row band clear of the
// floor and ceiling. Mirrored around the image's own centre column (63 -
// (kNearRedStripX + kStripWidth - 1) == kNearGreenStripX), matching the
// fixture's own light-and-wall symmetry about that centre.
constexpr int kNearRedStripX = 7;
constexpr int kNearGreenStripX = 53;
constexpr int kStripWidth = 4;
constexpr int kStripY = 26;
constexpr int kStripHeight = 13;

// Measured on this fixture pair: the back wall's own colour-bleed
// asymmetry (check B) is about 8.0 counts; cornellbox_direct.rib's own
// residual, dither-only asymmetry (check C) is about 0.04. kBleedDelta
// sits strictly between the two, comfortably more than two steps clear of
// each, so it discriminates bleed from dither noise without relying on
// GOLDEN_CHANNEL_TOL, which bounds a different comparison (one render
// against its own golden, not two regions within one render).
constexpr double kBleedDelta = 4.0;

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

// The full file's bytes, for a literal cmp -- distinct from decoding
// through libtiff, which would report two files equal on their decoded
// raster even if their encoded bytes differed.
std::vector<char> readWholeFile(std::string const& path) {
  std::ifstream in(path, std::ios::binary);
  return std::vector<char>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// Neither the loader's failure line nor the pass's own prepare() warnings
// (a bad element size, a skipped or capped primitive, an unconverged
// solve) appear in output.
bool printsNoRadiosityWarning(std::string const& output) {
  return output.find("Indirect-light pass \"radiosity\"") == std::string::npos &&
         output.find("Radiosity:") == std::string::npos;
}

// Mean red and green channel over the strip at (x0, kStripY), width
// kStripWidth, height kStripHeight.
struct StripMeans {
  double meanR;
  double meanG;
};

StripMeans stripMeans(GmanImage const& image, int x0) {
  double sumR = 0;
  double sumG = 0;
  long count = 0;
  for (int y = kStripY; y < kStripY + kStripHeight; ++y) {
    for (int x = x0; x < x0 + kStripWidth; ++x) {
      uint32_t const p = image.at((uint32_t)x, (uint32_t)y);
      sumR += TIFFGetR(p);
      sumG += TIFFGetG(p);
      ++count;
    }
  }
  return {sumR / (double)count, sumG / (double)count};
}

// Checks A, B and F: the box scene, its colour bleed, and two renders of
// it agreeing byte for byte.
void checkBox(std::string const& gman, std::string const& ribDir) {
  Rendered const on = renderFixture(gman, ribDir, "cornellbox.rib", "cornellbox.tif");

  check(on.result.exitStatus == 0, "box: cornellbox.rib exits 0");
  check(printsNoRadiosityWarning(on.result.output), "box: cornellbox.rib prints no radiosity warning");
  check(on.image.ok, "box: the render's TIFF reads back");
  if (!on.image.ok) {
    return;
  }

  checkGoldenImage("cornellbox.tif", ribDir + "/cornellbox_golden.tif", GOLDEN_CHANNEL_TOL, GOLDEN_MAX_FRACTION,
                   "cornellbox_diff.tif");

  StripMeans const nearRed = stripMeans(on.image, kNearRedStripX);
  StripMeans const nearGreen = stripMeans(on.image, kNearGreenStripX);
  double const redDelta = nearRed.meanR - nearGreen.meanR;
  double const greenDelta = nearGreen.meanG - nearRed.meanG;
  std::printf("box: near-red strip mean R=%.2f G=%.2f; near-green strip mean R=%.2f G=%.2f; redDelta=%.2f "
              "greenDelta=%.2f\n",
              nearRed.meanR, nearRed.meanG, nearGreen.meanR, nearGreen.meanG, redDelta, greenDelta);
  check(redDelta > kBleedDelta,
        "box: the near-red strip's mean red exceeds the near-green strip's by more than kBleedDelta");
  check(greenDelta > kBleedDelta,
        "box: the near-green strip's mean green exceeds the near-red strip's by more than kBleedDelta");

  // ---- check F: two renders of cornellbox.rib are byte-identical ----
  // The RIB's Display line always names "cornellbox.tif", so the first
  // render's file is copied aside before the second overwrites it.
  std::vector<char> const firstBytes = readWholeFile("cornellbox.tif");
  Rendered const onAgain = renderFixture(gman, ribDir, "cornellbox.rib", "cornellbox.tif");
  check(onAgain.result.exitStatus == 0, "box: the second cornellbox.rib render exits 0");
  check(printsNoRadiosityWarning(onAgain.result.output),
        "box: the second cornellbox.rib render prints no radiosity warning");
  std::vector<char> const secondBytes = readWholeFile("cornellbox.tif");
  check(!firstBytes.empty() && firstBytes == secondBytes, "box: two renders of cornellbox.rib are byte-identical");
}

// Check C: naming no pass, the same two strip comparisons each fall under
// kBleedDelta -- a small nonzero residual is still expected, since the
// strips sample different pixels and so draw different dither values.
void checkDirect(std::string const& gman, std::string const& ribDir) {
  Rendered const off = renderFixture(gman, ribDir, "cornellbox_direct.rib", "cornellbox_direct.tif");

  check(off.result.exitStatus == 0, "direct: cornellbox_direct.rib exits 0");
  check(printsNoRadiosityWarning(off.result.output), "direct: cornellbox_direct.rib prints no radiosity warning");
  check(off.image.ok, "direct: the render's TIFF reads back");
  if (!off.image.ok) {
    return;
  }

  StripMeans const nearRed = stripMeans(off.image, kNearRedStripX);
  StripMeans const nearGreen = stripMeans(off.image, kNearGreenStripX);
  double const redDelta = nearRed.meanR - nearGreen.meanR;
  double const greenDelta = nearGreen.meanG - nearRed.meanG;
  std::printf("direct: near-red strip mean R=%.2f G=%.2f; near-green strip mean R=%.2f G=%.2f; redDelta=%.2f "
              "greenDelta=%.2f\n",
              nearRed.meanR, nearRed.meanG, nearGreen.meanR, nearGreen.meanG, redDelta, greenDelta);
  check(redDelta < kBleedDelta, "direct: the near-red/near-green mean red difference falls under kBleedDelta");
  check(greenDelta < kBleedDelta, "direct: the near-green/near-red mean green difference falls under kBleedDelta");
}

} // namespace

int main(int argc, char* argv[]) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s <gman-binary> <tests/rib-dir>\n", argv[0]);
    return 2;
  }
  std::string const gman = argv[1];
  std::string const ribDir = argv[2];

  checkBox(gman, ribDir);
  checkDirect(gman, ribDir);

  return checkSummary("The radiosity pass bleeds colour from each Cornell box side wall onto the back wall beside "
                      "it, naming no pass leaves the back wall symmetric, and two renders of the pass's own scene "
                      "are byte-identical");
}
