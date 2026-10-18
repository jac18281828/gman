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
 * `gman -r gmanraytracer` renders tests/rib/radiosity_rim.rib, the lower-left
 * rim of a matte sphere in a lit room, naming the radiosity pass: the rim
 * renders without a black arc. The pass picks an element's side from the
 * hit's own normal; picked from the diced facet's chord normal instead, a
 * ray grazing the silhouette reads the element's back side, exactly black
 * on a closed opaque surface, and the rim shows a dark arc.
 *
 * The test counts dark pixels, each well under its neighbours' luminance,
 * and reads one rim pixel the arc crosses. Picking the side from the facet's
 * normal in GMANRadiosityPass::irradiance leaves 41 dark pixels and the
 * rim pixel at a channel sum of 6, where the hit's own normal leaves none and
 * a sum of 51.
 */

#include <cstdint>
#include <cstdio>
#include <string>

#include <tiffio.h>

#include "check.h"
#include "goldenimage.h"
#include "rungman.h"

namespace {

constexpr uint32_t kFrameSize = 48;

// A pixel's brightness is the sum of its three 8-bit channels.
constexpr int kDarkNeighbourFloor = 30;
constexpr int kNeighbourDistance = 3;
constexpr int kDarkNeighboursNeeded = 3;

// A rim pixel the unfixed render leaves at a channel sum of 6, where a
// lit rim reads 51: the threshold sits between them.
constexpr uint32_t kRimPixelX = 15;
constexpr uint32_t kRimPixelY = 14;
constexpr int kRimPixelLitSum = 25;

// Neither the pass's prepare() warnings (a bad element size, a skipped or
// capped primitive, an unconverged solve) nor the loader's failure line
// appear in output.
bool printsNoRadiosityWarning(std::string const& output) {
  return output.find("Indirect-light pass \"radiosity\"") == std::string::npos &&
         output.find("Radiosity:") == std::string::npos;
}

int channelSum(GmanImage const& image, uint32_t x, uint32_t y) {
  uint32_t const p = image.at(x, y);
  return (int)TIFFGetR(p) + (int)TIFFGetG(p) + (int)TIFFGetB(p);
}

// A pixel is dark when at least kDarkNeighboursNeeded of its four
// neighbours at kNeighbourDistance, left, right, above and below, are lit
// (channel sum of kDarkNeighbourFloor or more) and more than twice its own
// sum. Pixels closer than kNeighbourDistance + 1 to an edge are not counted.
long countDarkPixels(GmanImage const& image) {
  uint32_t const d = kNeighbourDistance;
  long dark = 0;
  for (uint32_t y = d; y + d < image.height; ++y) {
    for (uint32_t x = d; x + d < image.width; ++x) {
      int const own = channelSum(image, x, y);
      int const neighbours[4] = {channelSum(image, x - d, y), channelSum(image, x + d, y), channelSum(image, x, y - d),
                                 channelSum(image, x, y + d)};
      int brighter = 0;
      for (int const neighbour : neighbours) {
        if (neighbour >= kDarkNeighbourFloor && 2 * own < neighbour) {
          ++brighter;
        }
      }
      if (brighter >= kDarkNeighboursNeeded) {
        ++dark;
      }
    }
  }
  return dark;
}

void checkRim(std::string const& gman, std::string const& ribDir) {
  std::string const tifName = "radiosity_rim.tif";
  std::remove(tifName.c_str());
  GMANRunResult const result = runGman(gman, {"-r", "gmanraytracer", ribDir + "/radiosity_rim.rib"});
  GmanImage const image = readGmanTIFF(tifName);

  check(result.exitStatus == 0, "rim: radiosity_rim.rib exits 0");
  check(printsNoRadiosityWarning(result.output), "rim: radiosity_rim.rib prints no radiosity warning");
  check(image.ok && image.width == kFrameSize && image.height == kFrameSize,
        "rim: the render's TIFF reads back at " + std::to_string(kFrameSize) + " x " + std::to_string(kFrameSize));
  if (!image.ok || image.width != kFrameSize || image.height != kFrameSize) {
    return;
  }

  long const dark = countDarkPixels(image);
  int const rimSum = channelSum(image, kRimPixelX, kRimPixelY);
  std::printf("rim: %ld dark pixel(s); rim pixel (%u, %u) channel sum %d\n", dark, kRimPixelX, kRimPixelY, rimSum);
  check(dark == 0, "rim: the sphere's rim has no dark arc, " + std::to_string(dark) + " dark pixel(s) found");
  check(rimSum > kRimPixelLitSum,
        "rim: the rim pixel the arc crosses reads lit, channel sum " + std::to_string(rimSum));
}

} // namespace

int main(int argc, char* argv[]) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s <gman-binary> <tests/rib-dir>\n", argv[0]);
    return 2;
  }
  std::string const gman = argv[1];
  std::string const ribDir = argv[2];

  checkRim(gman, ribDir);

  return checkSummary("A sphere's rim in a radiosity-lit room renders without a black arc");
}
