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
 * R-transparency proof, check 4: `gman -r gmanraytracer` renders four
 * scenes sharing one receiver and one light -- no blocker
 * (transparencyshadow_none.rib), one Opacity 0.4 blocker
 * (transparencyshadow_one.rib), two Opacity 0.4 blockers stacked along the
 * light direction (transparencyshadow_two.rib), and one fully opaque
 * blocker (transparencyshadow_opaque.rib). At the shadowed pixel the four
 * values must fall strictly none > one > two > opaque, each gap wider
 * than GOLDEN_CHANNEL_TOL: the stacked pair is what separates
 * accumulating over every blocker from reading only the nearest one, which
 * would read one and two alike.
 */

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

} // namespace

int main(int argc, char* argv[]) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s <gman-binary> <rib-dir>\n", argv[0]);
    return 2;
  }
  std::string const gman = argv[1];
  std::string const ribDir = argv[2];

  std::remove("transparencyshadow_none.tif");
  std::remove("transparencyshadow_one.tif");
  std::remove("transparencyshadow_two.tif");
  std::remove("transparencyshadow_opaque.tif");

  char const* const scenes[] = {"transparencyshadow_none", "transparencyshadow_one", "transparencyshadow_two",
                                "transparencyshadow_opaque"};
  for (char const* scene : scenes) {
    check(runGman("\"" + gman + "\" -r gmanraytracer \"" + ribDir + "/" + scene + ".rib\" >/dev/null 2>&1") == 0,
          std::string(scene) + ".rib renders under -r gmanraytracer");
  }

  GmanImage const none = readGmanTIFF("transparencyshadow_none.tif");
  GmanImage const one = readGmanTIFF("transparencyshadow_one.tif");
  GmanImage const two = readGmanTIFF("transparencyshadow_two.tif");
  GmanImage const opaque = readGmanTIFF("transparencyshadow_opaque.tif");
  check(none.ok && one.ok && two.ok && opaque.ok, "all four renders read back");
  if (!none.ok || !one.ok || !two.ok || !opaque.ok) {
    return checkSummary("R-transparency's shadow attenuation");
  }

  uint32_t const nonePixel = none.at(kShadowPixelX, kShadowPixelY);
  uint32_t const onePixel = one.at(kShadowPixelX, kShadowPixelY);
  uint32_t const twoPixel = two.at(kShadowPixelX, kShadowPixelY);
  uint32_t const opaquePixel = opaque.at(kShadowPixelX, kShadowPixelY);

  std::printf("shadow pixel (%d,%d): none %d, one %d, two %d, opaque %d\n", kShadowPixelX, kShadowPixelY,
              TIFFGetR(nonePixel), TIFFGetR(onePixel), TIFFGetR(twoPixel), TIFFGetR(opaquePixel));

  check((int)TIFFGetR(nonePixel) - (int)TIFFGetR(onePixel) > GOLDEN_CHANNEL_TOL,
        "check 4: no blocker reads brighter than one blocker by more than GOLDEN_CHANNEL_TOL");
  check((int)TIFFGetR(onePixel) - (int)TIFFGetR(twoPixel) > GOLDEN_CHANNEL_TOL,
        "check 4: one blocker reads brighter than two stacked blockers by more than GOLDEN_CHANNEL_TOL -- "
        "accumulating over every blocker, not just the nearest");
  check((int)TIFFGetR(twoPixel) - (int)TIFFGetR(opaquePixel) > GOLDEN_CHANNEL_TOL,
        "check 4: two stacked blockers read brighter than one opaque blocker by more than GOLDEN_CHANNEL_TOL");

  return checkSummary("R-transparency's shadow attenuation: light through a stack of transparent blockers");
}
