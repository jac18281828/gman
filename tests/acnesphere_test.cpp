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
 * R7 proof, §8 check 2: a lone sphere lit by one off-axis distant light,
 * rendered at x1, x1000 and x0.001 (acnesphere_x1.rib,
 * acnesphere_x1000.rib, acnesphere_xsmall.rib -- camera distance and
 * Clipping scaled with the geometry in each). Self-shadow acne is
 * scale-dependent (the self-hit root grows with the hit point's own
 * coordinate magnitude), so the three renders agreeing pixel-for-pixel is
 * the real gate; a per-render local-dip scan catches isolated speckle
 * too, though a contiguous band -- the way an undersized offset actually
 * fails -- only the cross-scale comparison can catch.
 *
 * A sphere's intersector is well conditioned; this fixture cannot produce
 * the acne a swept quadric's does at a comparable magnitude, so it does
 * not defend kSelfShadowOffsetScale for a Cylinder, Cone, Paraboloid or
 * Hyperboloid. quadricsrayrender_test.cpp, against r5b_quadrics.rib and
 * its golden, is what does.
 *
 * The local-dip scan (worstLocalDip) also reads 0 at every scale tested,
 * including with the offset scale forced to zero: acne on a sphere is a
 * uniformly darkened band, not a speckle darker than both neighbors, so
 * this scan never fires against this fixture's own failure mode. The
 * cross-scale comparison above carries the check's real weight.
 */

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

#include <sys/wait.h>

#include <tiffio.h>

#include "check.h"
#include "goldenimage.h"
#include "ri.h"

namespace {

constexpr int kCrossScaleTolerance = 2;
constexpr int kLocalDipTolerance = 2;
// Any channel this close to the background (0,0,0) counts as
// background itself, not a silhouette pixel the local-dip scan should
// judge.
constexpr int kBackgroundTolerance = 2;

int runGman(std::string const& command) {
  int status = std::system(command.c_str());
  return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

int channelDelta(uint32_t a, uint32_t b) {
  int dr = std::abs((int)TIFFGetR(a) - (int)TIFFGetR(b));
  int dg = std::abs((int)TIFFGetG(a) - (int)TIFFGetG(b));
  int db = std::abs((int)TIFFGetB(a) - (int)TIFFGetB(b));
  return std::max(dr, std::max(dg, db));
}

bool isBackground(uint32_t p) {
  return TIFFGetR(p) >= 255 - kBackgroundTolerance && TIFFGetG(p) >= 255 - kBackgroundTolerance &&
         TIFFGetB(p) >= 255 - kBackgroundTolerance;
}

// The worst cross-scale difference between two same-sized renders that
// are meant to be the same image.
int worstCrossScaleDiff(GmanImage const& a, GmanImage const& b) {
  int worst = 0;
  for (uint32_t y = 0; y < a.height; ++y) {
    for (uint32_t x = 0; x < a.width; ++x) {
      worst = std::max(worst, channelDelta(a.at(x, y), b.at(x, y)));
    }
  }
  return worst;
}

// The sphere's own screen-space radius, measured along the image's
// horizontal centre scanline outward from its centre (the sphere is
// on-axis, so that centre is the image's own). Used to keep the local-dip
// scan off the silhouette's antialiased rim, which blends toward the
// background over a few pixels and reads as a false dip no self-shadowing
// produced -- a fixed pixel margin would need re-tuning for every camera
// this fixture might grow, a fraction of the sphere's own radius does not.
RtFloat silhouetteRadius(GmanImage const& img) {
  uint32_t const cy = img.height / 2;
  uint32_t const cx = img.width / 2;
  uint32_t x = cx;
  while (x < img.width && !isBackground(img.at(x, cy))) {
    ++x;
  }
  return (RtFloat)(x - cx);
}

// Worst amount any interior pixel (comfortably inside the silhouette,
// away from its antialiased rim) reads darker than *both* horizontal
// neighbors -- 0 if no pixel dips at all.
int worstLocalDip(GmanImage const& img, RtFloat radius) {
  RtFloat const cx = (RtFloat)img.width / 2.0f;
  RtFloat const cy = (RtFloat)img.height / 2.0f;
  RtFloat const interiorRadius = radius * 0.85f;
  int worst = 0;
  for (uint32_t y = 0; y < img.height; ++y) {
    for (uint32_t x = 1; x + 1 < img.width; ++x) {
      RtFloat const dx = (RtFloat)x - cx;
      RtFloat const dy = (RtFloat)y - cy;
      if (std::sqrt(dx * dx + dy * dy) >= interiorRadius) {
        continue;
      }
      uint32_t centre = img.at(x, y);
      uint32_t left = img.at(x - 1, y);
      uint32_t right = img.at(x + 1, y);
      int dipLeft = (int)TIFFGetR(left) - (int)TIFFGetR(centre);
      int dipRight = (int)TIFFGetR(right) - (int)TIFFGetR(centre);
      int dip = std::min(dipLeft, dipRight);
      worst = std::max(worst, dip);
    }
  }
  return worst;
}

} // namespace

int main(int argc, char* argv[]) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s <gman-binary> <rib-dir>\n", argv[0]);
    return 2;
  }
  std::string const gman = argv[1];
  std::string const ribDir = argv[2];

  std::remove("acnesphere_x1.tif");
  std::remove("acnesphere_x1000.tif");
  std::remove("acnesphere_xsmall.tif");

  char const* const scenes[] = {"acnesphere_x1", "acnesphere_x1000", "acnesphere_xsmall"};
  for (char const* scene : scenes) {
    check(runGman("\"" + gman + "\" -r gmanraytracer \"" + ribDir + "/" + scene + ".rib\" >/dev/null 2>&1") == 0,
          std::string(scene) + ".rib renders under -r gmanraytracer");
  }

  GmanImage const x1 = readGmanTIFF("acnesphere_x1.tif");
  GmanImage const x1000 = readGmanTIFF("acnesphere_x1000.tif");
  GmanImage const xsmall = readGmanTIFF("acnesphere_xsmall.tif");
  check(x1.ok && x1000.ok && xsmall.ok, "all three renders read back");
  check(x1.width == x1000.width && x1.height == x1000.height && x1.width == xsmall.width && x1.height == xsmall.height,
        "all three renders share one resolution");
  if (!x1.ok || !x1000.ok || !xsmall.ok || x1.width != x1000.width || x1.height != x1000.height ||
      x1.width != xsmall.width || x1.height != xsmall.height) {
    return checkSummary("R7's self-shadow acne, at scale");
  }

  int const worstX1000 = worstCrossScaleDiff(x1, x1000);
  int const worstXsmall = worstCrossScaleDiff(x1, xsmall);
  // The three scenes share one screen-space silhouette by construction, so
  // x1's own radius bounds the interior region for all three renders.
  RtFloat const radius = silhouetteRadius(x1);
  int const dipX1 = worstLocalDip(x1, radius);
  int const dipX1000 = worstLocalDip(x1000, radius);
  int const dipXsmall = worstLocalDip(xsmall, radius);
  std::printf("check 2: cross-scale worst diff x1000=%d xsmall=%d (tolerance %d)\n", worstX1000, worstXsmall,
              kCrossScaleTolerance);
  std::printf("check 2: worst local dip x1=%d x1000=%d xsmall=%d (tolerance %d)\n", dipX1, dipX1000, dipXsmall,
              kLocalDipTolerance);

  check(worstX1000 <= kCrossScaleTolerance, "check 2: x1 and x1000 render the same image within 2 counts per channel");
  check(worstXsmall <= kCrossScaleTolerance,
        "check 2: x1 and x0.001 render the same image within 2 counts per channel");
  check(dipX1 <= kLocalDipTolerance, "check 2: x1 has no pixel darker than both horizontal neighbors by more than 2");
  check(dipX1000 <= kLocalDipTolerance,
        "check 2: x1000 has no pixel darker than both horizontal neighbors by more than 2");
  check(dipXsmall <= kLocalDipTolerance,
        "check 2: x0.001 has no pixel darker than both horizontal neighbors by more than 2");

  return checkSummary("R7's self-shadow acne: a convex primitive renders the same picture at every scale");
}
