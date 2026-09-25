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
 * contactshadow.rib -- a thin vertical blocker standing on a horizontal
 * floor, its base at gap 0.001, lit at near-grazing incidence -- against
 * contactshadow_noblocker.rib, the same floor alone. Row 300, columns
 * 400-415 sit within the true shadow a correct contact must produce, and
 * also within the band a magnitude-scaled bias would leak unshadowed
 * (every pixel there would then match contactshadow_noblocker.rib's
 * render instead, which is what keeps this check non-vacuous): a fixed
 * offset origin makes the shadow ray see the blocker regardless of that
 * bias, so the strip darkens.
 *
 * fov 6 degrees overrides AGENTS.md's default camera for this fixture, on
 * r5b_quadrics.rib's own precedent: the leaked band's width in pixels is
 * 1e-2*sinTheta*Yres/(2*tan(fov/2)); at Yres 480 and sinTheta 0.9982 (the
 * light's 86.57 degree incidence off the floor's normal) that is about 46
 * px, comfortably inside which columns 400-415 (16 px) sit clear of the
 * antialiased edges on every side, including the true umbra edge near
 * column 419.
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

constexpr int kStripRow = 300;
constexpr int kStripColFirst = 400;
constexpr int kStripColLast = 415; // inclusive; 16 columns

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

} // namespace

int main(int argc, char* argv[]) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s <gman-binary> <rib-dir>\n", argv[0]);
    return 2;
  }
  std::string const gman = argv[1];
  std::string const ribDir = argv[2];

  std::remove("contactshadow.tif");
  std::remove("contactshadow_noblocker.tif");

  check(runGman("\"" + gman + "\" -r gmanraytracer \"" + ribDir + "/contactshadow.rib\" >/dev/null 2>&1") == 0,
        "contactshadow.rib renders under -r gmanraytracer");
  check(runGman("\"" + gman + "\" -r gmanraytracer \"" + ribDir + "/contactshadow_noblocker.rib\" >/dev/null 2>&1") ==
            0,
        "contactshadow_noblocker.rib renders under -r gmanraytracer");

  GmanImage const blocker = readGmanTIFF("contactshadow.tif");
  GmanImage const noblocker = readGmanTIFF("contactshadow_noblocker.tif");
  check(blocker.ok && noblocker.ok, "both renders read back");
  check(blocker.width == noblocker.width && blocker.height == noblocker.height, "both renders share one resolution");
  if (!blocker.ok || !noblocker.ok || blocker.width != noblocker.width || blocker.height != noblocker.height) {
    return checkSummary("R7's contact shadow");
  }

  int worst = 0;
  int best = 255;
  for (int x = kStripColFirst; x <= kStripColLast; ++x) {
    int const delta =
        channelDelta(blocker.at((uint32_t)x, (uint32_t)kStripRow), noblocker.at((uint32_t)x, (uint32_t)kStripRow));
    worst = std::max(worst, delta);
    best = std::min(best, delta);
  }
  std::printf("check 1: strip row %d, columns %d-%d (%d px): smallest delta %d, largest delta %d (tolerance %d)\n",
              kStripRow, kStripColFirst, kStripColLast, kStripColLast - kStripColFirst + 1, best, worst,
              GOLDEN_CHANNEL_TOL);

  check(best > GOLDEN_CHANNEL_TOL,
        "check 1: the contact closes -- every pixel in the strip differs from contactshadow_noblocker.rib's "
        "render by more than GOLDEN_CHANNEL_TOL");

  return checkSummary("R7's contact shadow: a blocker resting on its receiver shadows all the way to contact");
}
