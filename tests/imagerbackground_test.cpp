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
 * Imager "background" places its colour behind each pixel's uncovered part
 * under every renderer. A camera-facing disk lit head-on shades uniformly to
 * its silhouette, so each fixture holds interior pixels (alpha 255), exterior
 * pixels (alpha 0) and edge pixels that hold a bright premultiplied colour at
 * partial coverage, which a composite that took the colour as straight would
 * miss. The imager runs after exposure: its backdrop is not exposed, and the
 * covered colour is exposed exactly once. It writes alpha 1 everywhere,
 * defaults to white, and multiplies in BMRT's "color bgcolor".
 */

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

#include <tiffio.h>

#include "check.h"
#include "goldenimage.h"
#include "rungman.h"

namespace {

struct Rgba {
  int r;
  int g;
  int b;
  int a;
};

struct Pixel {
  std::uint32_t x;
  std::uint32_t y;
};

struct Rendered {
  GMANRunResult result;
  GmanImage image;
};

struct Classes {
  std::vector<Pixel> interior;
  std::vector<Pixel> exterior;
  std::vector<Pixel> edge;
};

constexpr int kBackdropTolerance = 1;
constexpr int kEdgeTolerance = 2;
constexpr int kControlLow = 64;
constexpr int kControlHigh = 127;
constexpr int kOpaque = 255;
constexpr int kEdgeLow = 64;
constexpr int kEdgeHigh = 191;
constexpr std::size_t kLeastEdgePixels = 4;
constexpr int kBackdrop[3] = {51, 77, 102};
constexpr float kBackdropColor[3] = {0.2f, 0.3f, 0.4f};

Rgba at(GmanImage const& image, std::uint32_t x, std::uint32_t y) {
  std::uint32_t const p = image.at(x, y);
  return {static_cast<int>(TIFFGetR(p)), static_cast<int>(TIFFGetG(p)), static_cast<int>(TIFFGetB(p)),
          static_cast<int>(TIFFGetA(p))};
}

Rendered renderFixture(std::string const& gman, std::string const& renderer, std::string const& ribDir,
                       std::string const& stem) {
  std::string const tifName = stem + ".tif";
  std::remove(tifName.c_str());
  Rendered rendered;
  rendered.result = runGman(gman, {"-r", renderer, ribDir + "/" + stem + ".rib"});
  rendered.image = readGmanTIFF(tifName);
  return rendered;
}

bool neighbourhoodAlpha(GmanImage const& control, std::uint32_t x, std::uint32_t y, int alpha) {
  if (x == 0 || y == 0 || x + 1 >= control.width || y + 1 >= control.height) {
    return false;
  }
  for (std::uint32_t ny = y - 1; ny <= y + 1; ++ny) {
    for (std::uint32_t nx = x - 1; nx <= x + 1; ++nx) {
      if (at(control, nx, ny).a != alpha) {
        return false;
      }
    }
  }
  return true;
}

Classes classify(GmanImage const& control) {
  Classes classes;
  for (std::uint32_t y = 0; y < control.height; ++y) {
    for (std::uint32_t x = 0; x < control.width; ++x) {
      int const alpha = at(control, x, y).a;
      if (neighbourhoodAlpha(control, x, y, kOpaque)) {
        classes.interior.push_back({x, y});
      } else if (neighbourhoodAlpha(control, x, y, 0)) {
        classes.exterior.push_back({x, y});
      } else if (alpha >= kEdgeLow && alpha <= kEdgeHigh) {
        classes.edge.push_back({x, y});
      }
    }
  }
  return classes;
}

bool rendersCleanly(Rendered const& rendered) {
  return rendered.result.exitStatus == 0 && rendered.image.ok &&
         rendered.result.output.find("ERROR:") == std::string::npos &&
         rendered.result.output.find("SEVERE:") == std::string::npos;
}

int channelDeviation(Rgba const& p, int const (&expected)[3]) {
  return std::max({std::abs(p.r - expected[0]), std::abs(p.g - expected[1]), std::abs(p.b - expected[2])});
}

bool rgbEqual(Rgba const& a, Rgba const& b) { return a.r == b.r && a.g == b.g && a.b == b.b; }

int worstBackdropDeviation(GmanImage const& image, std::vector<Pixel> const& pixels, int const (&expected)[3]) {
  int worst = 0;
  for (Pixel const& p : pixels) {
    worst = std::max(worst, channelDeviation(at(image, p.x, p.y), expected));
  }
  return worst;
}

void checkBackdrop(std::string const& tag, std::string const& what, GmanImage const& image,
                   std::vector<Pixel> const& exterior, int const (&expected)[3], int tolerance) {
  int const worst = worstBackdropDeviation(image, exterior, expected);
  check(worst <= tolerance, tag + ": " + what + " (largest deviation " + std::to_string(worst) + ")");
}

void checkCovered(std::string const& tag, std::string const& what, GmanImage const& image, GmanImage const& reference,
                  std::vector<Pixel> const& interior) {
  std::size_t differing = 0;
  for (Pixel const& p : interior) {
    if (!rgbEqual(at(image, p.x, p.y), at(reference, p.x, p.y))) {
      ++differing;
    }
  }
  check(differing == 0,
        tag + ": " + what + " (" + std::to_string(differing) + " of " + std::to_string(interior.size()) + " differ)");
}

void checkEdge(std::string const& tag, GmanImage const& image, GmanImage const& control,
               std::vector<Pixel> const& edge) {
  int worst = 0;
  for (Pixel const& p : edge) {
    Rgba const got = at(image, p.x, p.y);
    Rgba const ref = at(control, p.x, p.y);
    int const missing = kOpaque - ref.a;
    int const expected[3] = {ref.r + static_cast<int>(missing * kBackdropColor[0] + 0.5f),
                             ref.g + static_cast<int>(missing * kBackdropColor[1] + 0.5f),
                             ref.b + static_cast<int>(missing * kBackdropColor[2] + 0.5f)};
    worst = std::max(worst, channelDeviation(got, expected));
  }
  check(worst <= kEdgeTolerance, tag +
                                     ": every edge pixel is its premultiplied colour plus the uncovered share of the "
                                     "backdrop (largest deviation " +
                                     std::to_string(worst) + " over " + std::to_string(edge.size()) + " pixels)");
}

void checkAlpha(std::string const& tag, std::string const& stem, GmanImage const& image) {
  std::size_t notOpaque = 0;
  for (std::uint32_t y = 0; y < image.height; ++y) {
    for (std::uint32_t x = 0; x < image.width; ++x) {
      if (at(image, x, y).a != kOpaque) {
        ++notOpaque;
      }
    }
  }
  check(notOpaque == 0,
        tag + ": " + stem + " writes alpha 255 at every pixel (" + std::to_string(notOpaque) + " do not)");
}

std::pair<int, int> interiorRange(GmanImage const& control, std::vector<Pixel> const& interior) {
  int low = kOpaque;
  int high = 0;
  for (Pixel const& p : interior) {
    Rgba const c = at(control, p.x, p.y);
    for (int const v : {c.r, c.g, c.b}) {
      low = std::min(low, v);
      high = std::max(high, v);
    }
  }
  return {low, high};
}

} // namespace

int main(int argc, char* argv[]) {
  if (argc < 4) {
    std::fprintf(stderr, "usage: %s <gman-binary> <tests/rib-dir> <renderer>\n", argv[0]);
    return 2;
  }
  std::string const gman = argv[1];
  std::string const ribDir = argv[2];
  std::string const renderer = argv[3];

  std::vector<std::string> const stems = {"imagerbackground_control",  "imagerbackground",
                                          "imagerbackground_default",  "imagerbackground_exposure_control",
                                          "imagerbackground_exposure", "imagerbackground_bgcolor"};
  std::vector<Rendered> rendered;
  for (std::string const& stem : stems) {
    rendered.push_back(renderFixture(gman, renderer, ribDir, stem));
    check(rendersCleanly(rendered.back()),
          renderer + ": " + stem + " renders with exit 0, a TIFF and no ERROR or SEVERE");
  }
  if (!std::all_of(rendered.begin(), rendered.end(), [](Rendered const& r) { return r.image.ok; })) {
    return checkSummary("imager background");
  }

  GmanImage const& control = rendered[0].image;
  GmanImage const& explicitBackdrop = rendered[1].image;
  GmanImage const& defaulted = rendered[2].image;
  GmanImage const& exposureControl = rendered[3].image;
  GmanImage const& exposed = rendered[4].image;
  GmanImage const& aliased = rendered[5].image;

  Classes const classes = classify(control);
  std::string const tag = renderer;
  std::string const counts = std::to_string(classes.interior.size()) + " interior, " +
                             std::to_string(classes.exterior.size()) + " exterior, " +
                             std::to_string(classes.edge.size()) + " edge";
  check(!classes.interior.empty() && !classes.exterior.empty() && classes.edge.size() >= kLeastEdgePixels,
        tag + ": control has interior, exterior and at least four edge pixels (" + counts + ")");
  auto const [lowest, highest] = interiorRange(control, classes.interior);
  check(lowest >= kControlLow && highest <= kControlHigh,
        tag + ": control's covered colour lies in [64, 127] in every channel (reads " + std::to_string(lowest) +
            " to " + std::to_string(highest) + ")");
  if (classes.interior.empty() || classes.exterior.empty() || classes.edge.empty()) {
    return checkSummary("imager background");
  }

  checkBackdrop(tag, "uncovered pixels read the backdrop", explicitBackdrop, classes.exterior, kBackdrop,
                kBackdropTolerance);
  checkCovered(tag, "covered pixels equal the control's", explicitBackdrop, control, classes.interior);
  checkEdge(tag, explicitBackdrop, control, classes.edge);

  checkAlpha(tag, "imagerbackground", explicitBackdrop);
  checkAlpha(tag, "imagerbackground_default", defaulted);
  checkAlpha(tag, "imagerbackground_exposure", exposed);
  checkAlpha(tag, "imagerbackground_bgcolor", aliased);

  int const white[3] = {kOpaque, kOpaque, kOpaque};
  checkBackdrop(tag, "no parameter gives a white backdrop", defaulted, classes.exterior, white, 0);

  checkBackdrop(tag, "bgcolor multiplies the backdrop", aliased, classes.exterior, kBackdrop, kBackdropTolerance);

  checkBackdrop(tag, "the backdrop is not exposed", exposed, classes.exterior, kBackdrop, kBackdropTolerance);
  checkCovered(tag, "covered pixels are exposed once", exposed, exposureControl, classes.interior);

  return checkSummary("Imager \"background\" places its colour behind the uncovered part of each pixel");
}
