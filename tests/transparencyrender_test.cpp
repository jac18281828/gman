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
 * R-transparency proof, checks 2 and 3: `gman -r gmanraytracer` renders four
 * scenes -- a semi-transparent front square over a larger opaque back
 * square (transparency_composite.rib), the front square alone at the same
 * Opacity (transparency_front_alone.rib), the front square alone fully
 * opaque (transparency_front_opaque.rib), and the back square alone
 * (transparency_back_alone.rib) -- sharing one light placed so the front
 * casts no shadow on the back. Reading each render's own corner pixel as
 * background and its centre pixel as the surface(s)' contribution, two
 * relations must hold within 3 counts per channel (this fixture's 8-bit
 * quantization budget):
 *
 *   front_alone == 0.4 * front_opaque + 0.6 * bg
 *   composite   == front_alone + 0.6 * (back_alone - bg)
 *
 * The composite render also matches a checked-in golden.
 */

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

#include <sys/wait.h>

#include <tiffio.h>

#include "check.h"
#include "goldenimage.h"

namespace {

constexpr int kCentreX = 100;
constexpr int kCentreY = 100;
constexpr int kTolerance = 3;

int runGman(const std::string& command) {
  int status = std::system(command.c_str());
  return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

// front_alone's own prediction from front_opaque and bg, or composite's
// own prediction from front_alone and back_alone -- both are one channel's
// worth of "a + w * (b - c)" (w = 0.6, the front's own 1 - Opacity), shared
// since check 2 states both relations in that shape.
double predict(double base, double weight, double from, double toward) { return base + weight * (from - toward); }

bool withinTolerance(double predicted, uint32_t actualChannel) {
  return std::fabs(predicted - (double)actualChannel) <= (double)kTolerance;
}

} // namespace

int main(int argc, char* argv[]) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s <gman-binary> <rib-dir>\n", argv[0]);
    return 2;
  }
  std::string const gman = argv[1];
  std::string const ribDir = argv[2];

  std::remove("transparency_composite.tif");
  std::remove("transparency_front_alone.tif");
  std::remove("transparency_front_opaque.tif");
  std::remove("transparency_back_alone.tif");

  char const* const scenes[] = {"transparency_composite", "transparency_front_alone", "transparency_front_opaque",
                                "transparency_back_alone"};
  for (char const* scene : scenes) {
    check(runGman("\"" + gman + "\" -r gmanraytracer \"" + ribDir + "/" + scene + ".rib\" >/dev/null 2>&1") == 0,
          std::string(scene) + ".rib renders under -r gmanraytracer");
  }

  GmanImage const composite = readGmanTIFF("transparency_composite.tif");
  GmanImage const frontAlone = readGmanTIFF("transparency_front_alone.tif");
  GmanImage const frontOpaque = readGmanTIFF("transparency_front_opaque.tif");
  GmanImage const backAlone = readGmanTIFF("transparency_back_alone.tif");
  check(composite.ok && frontAlone.ok && frontOpaque.ok && backAlone.ok, "all four renders read back");
  if (!composite.ok || !frontAlone.ok || !frontOpaque.ok || !backAlone.ok) {
    return checkSummary("R-transparency's composite arithmetic");
  }

  // 3. The composite render matches a checked-in golden.
  checkGoldenImage("transparency_composite.tif", ribDir + "/transparency_composite_golden.tif", GOLDEN_CHANNEL_TOL,
                   GOLDEN_MAX_FRACTION, "transparency_composite_diff.tif");

  uint32_t const compositePixel = composite.at(kCentreX, kCentreY);
  uint32_t const frontAlonePixel = frontAlone.at(kCentreX, kCentreY);
  uint32_t const frontOpaquePixel = frontOpaque.at(kCentreX, kCentreY);
  uint32_t const backAlonePixel = backAlone.at(kCentreX, kCentreY);
  // Every fixture leaves its own corner uncovered -- background.
  uint32_t const bgPixel = composite.at(0, 0);

  std::printf("front_opaque rgb(%d,%d,%d) front_alone rgb(%d,%d,%d) back_alone rgb(%d,%d,%d) composite "
              "rgb(%d,%d,%d) bg rgb(%d,%d,%d)\n",
              TIFFGetR(frontOpaquePixel), TIFFGetG(frontOpaquePixel), TIFFGetB(frontOpaquePixel),
              TIFFGetR(frontAlonePixel), TIFFGetG(frontAlonePixel), TIFFGetB(frontAlonePixel), TIFFGetR(backAlonePixel),
              TIFFGetG(backAlonePixel), TIFFGetB(backAlonePixel), TIFFGetR(compositePixel), TIFFGetG(compositePixel),
              TIFFGetB(compositePixel), TIFFGetR(bgPixel), TIFFGetG(bgPixel), TIFFGetB(bgPixel));

  // Channel c of a packed TIFFReadRGBAImage pixel, matching TIFFGetR/G/B's
  // own layout (macros, so not addressable as a function pointer).
  auto channel = [](uint32_t pixel, int c) -> uint32_t { return (pixel >> (8 * c)) & 0xff; };
  char const* const channelNames[3] = {"red", "green", "blue"};

  // 2. front_alone == 0.4 * front_opaque + 0.6 * bg -- the relation with
  // teeth: it predicts the front term absolutely, so a second Oi on the
  // running colour (which scales every front-surface term alike) cannot
  // cancel out of it.
  for (int c = 0; c < 3; ++c) {
    double const predicted = 0.4 * (double)channel(frontOpaquePixel, c) + 0.6 * (double)channel(bgPixel, c);
    check(withinTolerance(predicted, channel(frontAlonePixel, c)),
          std::string("check 2, ") + channelNames[c] +
              ": front_alone matches 0.4*front_opaque + 0.6*bg within 3 counts");
  }

  // 2. composite == front_alone + 0.6 * (back_alone - bg) -- linear in the
  // renders it compares, so a mutation scaling every front-surface term
  // alike cancels out of this relation and passes; check 2's first
  // relation above is what catches that mutation.
  for (int c = 0; c < 3; ++c) {
    double const predicted = predict((double)channel(frontAlonePixel, c), 0.6, (double)channel(backAlonePixel, c),
                                     (double)channel(bgPixel, c));
    check(withinTolerance(predicted, channel(compositePixel, c)),
          std::string("check 2, ") + channelNames[c] +
              ": composite matches front_alone + 0.6*(back_alone - bg) within 3 counts");
  }

  return checkSummary("R-transparency's composite arithmetic: front-to-back blending matches Opacity's own math");
}
