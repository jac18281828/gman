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
 * A flat, fully-covered matte plane samples 1.6 everywhere -- above 1,
 * so the framebuffer must hold it unclamped for Exposure to recover it.
 * exposurerecovery_half.rib halves the gain before quantizing (0.8, byte
 * 204); exposurerecovery_unity.rib leaves it at 1.6, which only Quantize's
 * clamp to its request's max, not an end clamp, bounds to 255.
 */

#include <cstdio>
#include <cstdlib>
#include <string>

#include <sys/wait.h>

#include <tiffio.h>

#include "check.h"
#include "goldenimage.h"

namespace {

int runGman(std::string const& gman, std::string const& renderer, std::string const& rib) {
  const std::string command = "\"" + gman + "\" -r " + renderer + " \"" + rib + "\" >/dev/null 2>&1";
  const int status = std::system(command.c_str());
  return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

// Every pixel's R, G and B read exactly value; the fixture covers the
// whole 16x16 frame, so one mismatched pixel is one mismatched check.
void checkUniform(GmanImage const& image, unsigned value, std::string const& tag) {
  check(image.ok, tag + ": TIFF read back");
  if (!image.ok) {
    return;
  }
  long mismatched = 0;
  for (uint32_t y = 0; y < image.height; ++y) {
    for (uint32_t x = 0; x < image.width; ++x) {
      const uint32_t px = image.at(x, y);
      if (TIFFGetR(px) != value || TIFFGetG(px) != value || TIFFGetB(px) != value) {
        ++mismatched;
      }
    }
  }
  check(mismatched == 0, tag + ": every pixel reads " + std::to_string(value) + " on R, G and B (" +
                             std::to_string(mismatched) + " differed)");
}

} // namespace

int main(int argc, char* argv[]) {
  if (argc < 4) {
    std::fprintf(stderr, "usage: %s <gman-binary> <tests/rib-dir> <renderer>\n", argv[0]);
    return 2;
  }
  const std::string gman = argv[1];
  const std::string ribDir = argv[2];
  const std::string renderer = argv[3];

  std::remove("exposurerecovery_half.tif");
  check(runGman(gman, renderer, ribDir + "/exposurerecovery_half.rib") == 0, "half: renders");
  checkUniform(readGmanTIFF("exposurerecovery_half.tif"), 204, "half");

  std::remove("exposurerecovery_unity.tif");
  check(runGman(gman, renderer, ribDir + "/exposurerecovery_unity.rib") == 0, "unity: renders");
  checkUniform(readGmanTIFF("exposurerecovery_unity.tif"), 255, "unity");

  return checkSummary("Exposure below 1 recovers colour above 1; the end clamp alone bounds unity gain");
}
