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
 * One rule for coverage in every renderer: a surface whose Oi is NaN or +inf
 * is opaque (a NaN clamped to 0 would read transparent), and one whose
 * Oi is -inf is transparent. The z-buffer reads the vertex opacity before it
 * interpolates, since infinite vertices interpolate to NaN, and its per-sample
 * alpha reads the NaN that interpolating finite vertices of +-3e38 overflows
 * to, as opaque. The plugin
 * tests/nonfinitesurface_test_plugin.cpp answers each, with a finite red Ci, over a sphere at the centre of the frame;
 * the centre pixel's alpha and colour read back from the TIFF. A NaN or infinite Ci shades black rather than reaching
 * the pixel.
 */

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include <tiffio.h>

#include "check.h"
#include "rungman.h"

namespace {

constexpr std::uint32_t kBlockHalf = 6;
constexpr double kExtremeMeanAlphaFloor = 0.6;

struct Pixel {
  bool ok = false;
  int red = 0;
  int green = 0;
  int blue = 0;
  int alpha = 0;
};

Pixel centrePixel(std::string const& path) {
  Pixel pixel;
  TIFF* const tif = TIFFOpen(path.c_str(), "r");
  if (tif == nullptr) {
    return pixel;
  }
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &width);
  TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &height);
  std::vector<std::uint32_t> raster(static_cast<std::size_t>(width) * height);
  if (TIFFReadRGBAImageOriented(tif, width, height, raster.data(), ORIENTATION_TOPLEFT, 0)) {
    std::uint32_t const value = raster[static_cast<std::size_t>(height / 2) * width + width / 2];
    pixel.ok = true;
    pixel.red = static_cast<int>(TIFFGetR(value));
    pixel.green = static_cast<int>(TIFFGetG(value));
    pixel.blue = static_cast<int>(TIFFGetB(value));
    pixel.alpha = static_cast<int>(TIFFGetA(value));
  }
  TIFFClose(tif);
  return pixel;
}

double centreBlockMeanAlpha(std::string const& path) {
  TIFF* const tif = TIFFOpen(path.c_str(), "r");
  if (tif == nullptr) {
    return -1.0;
  }
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &width);
  TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &height);
  std::vector<std::uint32_t> raster(static_cast<std::size_t>(width) * height);
  double sum = 0.0;
  int count = 0;
  if (TIFFReadRGBAImageOriented(tif, width, height, raster.data(), ORIENTATION_TOPLEFT, 0)) {
    for (std::uint32_t y = height / 2 - kBlockHalf; y < height / 2 + kBlockHalf; ++y) {
      for (std::uint32_t x = width / 2 - kBlockHalf; x < width / 2 + kBlockHalf; ++x) {
        sum += static_cast<double>(TIFFGetA(raster[static_cast<std::size_t>(y) * width + x])) / 255.0;
        ++count;
      }
    }
  }
  TIFFClose(tif);
  return count == 0 ? -1.0 : sum / count;
}

std::string renderToFile(std::string const& gman, std::string const& renderer, std::string const& ci,
                         std::string const& oi) {
  std::string const stem = "coverage_" + renderer + "_" + ci + "_" + oi;
  std::string const tif = stem + ".tif";
  std::string const rib = stem + ".rib";
  std::ofstream(rib) << "Display \"" << tif << "\" \"file\" \"rgba\"\n"
                     << "Format 40 40 1\n"
                     << "Projection \"perspective\" \"fov\" [30]\n"
                     << "Translate 0 0 5\n"
                     << "WorldBegin\n"
                     << "Color [1 0 0]\n"
                     << "Surface \"nonfinitesurface\" \"string ci\" [\"" << ci << "\"] \"string oi\" [\"" << oi
                     << "\"]\n"
                     << "Sphere 1 -1 1 360\n"
                     << "WorldEnd\n";
  GMANRunResult const result = runGman(gman, {"-r", renderer, rib});
  check(result.exitStatus == 0, renderer + " renders ci " + ci + " oi " + oi);
  return tif;
}

Pixel render(std::string const& gman, std::string const& renderer, std::string const& ci, std::string const& oi) {
  return centrePixel(renderToFile(gman, renderer, ci, oi));
}

void testExtremeOpacity(std::string const& gman, std::string const& renderer) {
  double const mean = centreBlockMeanAlpha(renderToFile(gman, renderer, "finite", "extreme"));
  check(mean > kExtremeMeanAlphaFloor, renderer + ": opacity of +-3e38 over the sphere's centre averages alpha " +
                                           std::to_string(mean) + ", above " + std::to_string(kExtremeMeanAlphaFloor));
}

void testOpacity(std::string const& gman, std::string const& renderer, std::string const& oi, int expectedAlpha) {
  Pixel const pixel = render(gman, renderer, "finite", oi);
  check(pixel.ok && pixel.alpha == expectedAlpha, renderer + ": Oi " + oi + " reads as alpha " +
                                                      std::to_string(expectedAlpha) + ", got " +
                                                      std::to_string(pixel.alpha));
}

void testRedSurvivesNonFiniteOpacity(std::string const& gman, std::string const& renderer, std::string const& oi) {
  Pixel const pixel = render(gman, renderer, "finite", oi);
  check(pixel.ok && pixel.red > 200 && pixel.green < 50 && pixel.blue < 50,
        renderer + ": a finite red Ci under Oi " + oi + " stays red, got (" + std::to_string(pixel.red) + ", " +
            std::to_string(pixel.green) + ", " + std::to_string(pixel.blue) + ")");
}

void testNonFiniteColourShadesBlack(std::string const& gman, std::string const& renderer, std::string const& ci) {
  Pixel const pixel = render(gman, renderer, ci, "finite");
  check(pixel.ok && pixel.red == 0 && pixel.green == 0 && pixel.blue == 0 && pixel.alpha == 255,
        renderer + ": Ci " + ci + " shades an opaque black pixel, got (" + std::to_string(pixel.red) + ", " +
            std::to_string(pixel.green) + ", " + std::to_string(pixel.blue) + ", " + std::to_string(pixel.alpha) + ")");
}

} // namespace

int main(int argc, char* argv[]) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s <gman-binary> <renderer>\n", argv[0]);
    return 2;
  }
  std::string const gman = argv[1];
  std::string const renderer = argv[2];
  bool const tracesColour = renderer != "gmanpathtracer";

  testOpacity(gman, renderer, "finite", 255);
  testOpacity(gman, renderer, "nan", 255);
  testOpacity(gman, renderer, "inf", 255);
  testOpacity(gman, renderer, "-inf", 0);
  if (renderer == "gmanzbuffer") {
    testExtremeOpacity(gman, renderer);
  }
  if (tracesColour) {
    testRedSurvivesNonFiniteOpacity(gman, renderer, "nan");
    testRedSurvivesNonFiniteOpacity(gman, renderer, "inf");
    testNonFiniteColourShadesBlack(gman, renderer, "nan");
    testNonFiniteColourShadesBlack(gman, renderer, "inf");
    testNonFiniteColourShadesBlack(gman, renderer, "-inf");
  }
  return checkSummary(("coverage reads the same under " + renderer).c_str());
}
