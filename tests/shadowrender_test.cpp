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
 * `gman -r gmanraytracer` renders
 * tests/rib/shadow.rib -- a small sphere between a distant light and a
 * large receiving sphere, plus an ambient light -- matching a checked-in
 * golden, and pixel (197, 101), inside the cast shadow on the receiver's
 * lit side, holds two further relations: it reads within 2 counts per
 * channel of shadow_nolight.rib's render at the same pixel (fully
 * occluded reads the same as no distant light at all), and
 * shadow_noblocker.rib's render at that pixel reads brighter by more than
 * GOLDEN_CHANNEL_TOL (removing the blocker relights it).
 *
 * The three renders write shadow.tif/shadow_nolight.tif/
 * shadow_noblocker.tif; any left by a prior run are removed first, so a
 * failed render cannot leave this run judging a stale image.
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

constexpr int kShadowPixelX = 197;
constexpr int kShadowPixelY = 101;

int runGman(const std::string& command) {
  int status = std::system(command.c_str());
  return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

bool channelsNear(uint32_t a, uint32_t b, int tol) {
  return std::abs(int(TIFFGetR(a)) - int(TIFFGetR(b))) <= tol && std::abs(int(TIFFGetG(a)) - int(TIFFGetG(b))) <= tol &&
         std::abs(int(TIFFGetB(a)) - int(TIFFGetB(b))) <= tol;
}

} // namespace

int main(int argc, char* argv[]) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s <gman-binary> <rib-dir>\n", argv[0]);
    return 2;
  }
  std::string const gman = argv[1];
  std::string const ribDir = argv[2];

  std::remove("shadow.tif");
  std::remove("shadow_nolight.tif");
  std::remove("shadow_noblocker.tif");

  // 3. shadow.rib matches its golden.
  check(runGman("\"" + gman + "\" -r gmanraytracer \"" + ribDir + "/shadow.rib\" >/dev/null 2>&1") == 0,
        "shadow.rib renders under -r gmanraytracer");
  checkGoldenImage("shadow.tif", ribDir + "/shadow_golden.tif", GOLDEN_CHANNEL_TOL, GOLDEN_MAX_FRACTION,
                   "shadow_diff.tif");

  check(runGman("\"" + gman + "\" -r gmanraytracer \"" + ribDir + "/shadow_nolight.rib\" >/dev/null 2>&1") == 0,
        "shadow_nolight.rib renders under -r gmanraytracer");
  check(runGman("\"" + gman + "\" -r gmanraytracer \"" + ribDir + "/shadow_noblocker.rib\" >/dev/null 2>&1") == 0,
        "shadow_noblocker.rib renders under -r gmanraytracer");

  GmanImage shadow = readGmanTIFF("shadow.tif");
  GmanImage nolight = readGmanTIFF("shadow_nolight.tif");
  GmanImage noblocker = readGmanTIFF("shadow_noblocker.tif");
  check(shadow.ok && nolight.ok && noblocker.ok, "all three renders read back");
  if (!shadow.ok || !nolight.ok || !noblocker.ok) {
    return checkSummary("R7's shadow rays");
  }

  uint32_t const shadowPixel = shadow.at(kShadowPixelX, kShadowPixelY);
  uint32_t const nolightPixel = nolight.at(kShadowPixelX, kShadowPixelY);
  uint32_t const noblockerPixel = noblocker.at(kShadowPixelX, kShadowPixelY);
  std::printf("shadow pixel (%d,%d): shadow rgb(%d,%d,%d) nolight rgb(%d,%d,%d) noblocker rgb(%d,%d,%d)\n",
              kShadowPixelX, kShadowPixelY, TIFFGetR(shadowPixel), TIFFGetG(shadowPixel), TIFFGetB(shadowPixel),
              TIFFGetR(nolightPixel), TIFFGetG(nolightPixel), TIFFGetB(nolightPixel), TIFFGetR(noblockerPixel),
              TIFFGetG(noblockerPixel), TIFFGetB(noblockerPixel));

  // 3. Fully occluded reads like no distant light at all, within 2 counts.
  check(channelsNear(shadowPixel, nolightPixel, 2),
        "shadow pixel: matches shadow_nolight.rib's render within 2 counts per channel");

  // 4. Removing the blocker relights the same pixel by more than
  // GOLDEN_CHANNEL_TOL.
  bool const relit = std::abs(int(TIFFGetR(noblockerPixel)) - int(TIFFGetR(shadowPixel))) > GOLDEN_CHANNEL_TOL ||
                     std::abs(int(TIFFGetG(noblockerPixel)) - int(TIFFGetG(shadowPixel))) > GOLDEN_CHANNEL_TOL ||
                     std::abs(int(TIFFGetB(noblockerPixel)) - int(TIFFGetB(shadowPixel))) > GOLDEN_CHANNEL_TOL;
  check(relit, "shadow pixel: shadow_noblocker.rib's render reads brighter by more than GOLDEN_CHANNEL_TOL");

  return checkSummary("R7's shadow rays: a blocker between a distant light and a receiver casts a shadow");
}
