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
 * Proves OutputTIFF's "rgba" alpha byte under both `-r gmanzbuffer` (the
 * default) and `-r gmanraytracer`: an uncovered pixel reads 0, a pixel
 * every sample of which hit opaque geometry reads 255, a pixel straddling
 * a silhouette reads an intermediate value tracking that same pixel's own
 * colour-implied coverage fraction, and a uniformly semi-transparent
 * surface's pixel reads its own Opacity. Every asserted pixel's R, G and B
 * stay at or below its own alpha byte.
 *
 * Every read goes through TIFFReadScanline rather than
 * TIFFReadRGBAImageOriented: the latter may un-premultiply associated
 * alpha on decode, which would corrupt exactly the relation the
 * premultiplication checks below assert.
 */

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include <sys/wait.h>

#include <tiffio.h>

#include "check.h"

namespace {

int runGman(const std::string& gman, const std::string& rib, const std::string& rendererFlag) {
  const std::string command = "\"" + gman + "\" " + rendererFlag + " \"" + rib + "\" >/dev/null 2>&1";
  int status = std::system(command.c_str());
  return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

void writeFile(const std::string& path, const std::string& contents) {
  std::ofstream out(path);
  out << contents;
}

// The raw, unreinterpreted bytes OutputTIFF::save wrote: no RGBA
// reinterpretation or un-premultiplication, since libtiff's own RGBA
// reader can perform either on an associated-alpha image.
struct RawImage {
  bool ok = false;
  uint32_t width = 0, height = 0;
  int samplesPerPixel = 0;
  std::vector<unsigned char> data;

  unsigned char at(uint32_t x, uint32_t y, int channel) const {
    return data[((std::size_t)y * width + x) * (std::size_t)samplesPerPixel + (std::size_t)channel];
  }
  unsigned char r(uint32_t x, uint32_t y) const { return at(x, y, 0); }
  unsigned char g(uint32_t x, uint32_t y) const { return at(x, y, 1); }
  unsigned char b(uint32_t x, uint32_t y) const { return at(x, y, 2); }
  unsigned char a(uint32_t x, uint32_t y) const { return at(x, y, 3); }
};

RawImage readRawTIFF(const std::string& path) {
  RawImage img;
  TIFF* tif = TIFFOpen(path.c_str(), "r");
  if (tif == nullptr) {
    return img;
  }

  uint32_t width = 0, height = 0;
  uint16_t samplesPerPixel = 0;
  TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &width);
  TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &height);
  TIFFGetField(tif, TIFFTAG_SAMPLESPERPIXEL, &samplesPerPixel);

  const tmsize_t scanlineSize = TIFFScanlineSize(tif);
  const std::size_t linebytes = (std::size_t)width * (std::size_t)samplesPerPixel;
  std::vector<unsigned char> scanline((std::size_t)scanlineSize);

  img.data.resize((std::size_t)height * linebytes);
  bool ok = true;
  for (uint32_t y = 0; y < height; ++y) {
    if (TIFFReadScanline(tif, scanline.data(), y) < 0) {
      ok = false;
      break;
    }
    std::copy(scanline.begin(), scanline.begin() + (std::ptrdiff_t)linebytes,
              img.data.begin() + (std::ptrdiff_t)((std::size_t)y * linebytes));
  }
  TIFFClose(tif);

  img.width = width;
  img.height = height;
  img.samplesPerPixel = samplesPerPixel;
  img.ok = ok;
  return img;
}

// Asserts R, G and B each stay at or below alpha at (x, y) -- the
// associated-alpha invariant OutputTIFF's own tag promises.
void checkPremultiplied(const RawImage& img, uint32_t x, uint32_t y, const std::string& label) {
  const int alpha = img.a(x, y);
  check(img.r(x, y) <= alpha, label + ": R at or below alpha");
  check(img.g(x, y) <= alpha, label + ": G at or below alpha");
  check(img.b(x, y) <= alpha, label + ": B at or below alpha");
}

// backgroundcolor_test.cpp's own fixture: a small red matte sphere,
// ambient-only, leaving every corner of the frame uncovered.
std::string extremesRib(const std::string& display) {
  return "Display \"" + display +
         "\" \"file\" \"rgba\"\n"
         "Format 8 8 1\n"
         "PixelSamples 1 1\n"
         // A box filter no wider than one pixel: the default Gaussian's
         // width-2 support would blend a fully-covered pixel's own single
         // sample with an uncovered neighbour's, so no pixel this small a
         // sphere covers would ever resolve to pure alpha 255.
         "PixelFilter \"box\" 1 1\n"
         "Projection \"perspective\" \"fov\" [40]\n"
         "Clipping 0.5 50\n"
         "Translate 0 0 5\n"
         "WorldBegin\n"
         "LightSource \"ambientlight\" 1 \"intensity\" [1]\n"
         "Color [1 0 0]\n"
         "Surface \"matte\"\n"
         "Sphere 0.4 -0.4 0.4 360\n"
         "WorldEnd\n";
}

void testExtremes(const std::string& gman, const std::string& rendererFlag, const std::string& tag) {
  const std::string rib = "extremes_" + tag + ".rib";
  const std::string tif = "extremes_" + tag + ".tif";
  writeFile(rib, extremesRib(tif));
  check(runGman(gman, rib, rendererFlag) == 0, tag + ": extremes scene renders");

  RawImage img = readRawTIFF(tif);
  check(img.ok, tag + ": extremes scene reads back");
  if (!img.ok) {
    return;
  }

  const uint32_t corners[4][2] = {{0, 0}, {img.width - 1, 0}, {0, img.height - 1}, {img.width - 1, img.height - 1}};
  const char* names[4] = {"top-left", "top-right", "bottom-left", "bottom-right"};
  for (int i = 0; i < 4; ++i) {
    check(img.a(corners[i][0], corners[i][1]) == 0,
          tag + ": " + names[i] + " corner reads alpha 0, got " + std::to_string(img.a(corners[i][0], corners[i][1])));
  }

  // The frame's own brightest pixel, found by scanning rather than assumed
  // at a fixed coordinate: every sample covering it hit the fully opaque
  // sphere.
  uint32_t brightX = 0, brightY = 0;
  int brightest = -1;
  for (uint32_t y = 0; y < img.height; ++y) {
    for (uint32_t x = 0; x < img.width; ++x) {
      const int sum = (int)img.r(x, y) + (int)img.g(x, y) + (int)img.b(x, y);
      if (sum > brightest) {
        brightest = sum;
        brightX = x;
        brightY = y;
      }
    }
  }
  check(img.a(brightX, brightY) == 255,
        tag + ": the frame's brightest pixel reads alpha 255, got " + std::to_string(img.a(brightX, brightY)));
  checkPremultiplied(img, brightX, brightY, tag + ": extremes brightest pixel");
}

// samplebuffer_test.cpp's own edgeRib fixture: a matte rectangle with one
// hard vertical edge at world x=0.031, off any pixel or sample grid line.
std::string edgeRib(const std::string& display) {
  return "Display \"" + display +
         "\" \"file\" \"rgba\"\n"
         "Format 100 100 1\n"
         "Projection \"orthographic\"\n"
         "Clipping 0.5 50\n"
         "PixelFilter \"box\" 1 1\n"
         "PixelSamples 4 4\n"
         "Translate 0 0 5\n"
         "WorldBegin\n"
         "LightSource \"ambientlight\" 1 \"intensity\" [1]\n"
         "Sides 2\n"
         "Color [0.5 0.5 0.5]\n"
         "Surface \"matte\" \"Ka\" [1] \"Kd\" [0]\n"
         "Polygon \"P\" [ -2 -2 0  0.031 -2 0  0.031 2 0  -2 2 0 ]\n"
         "WorldEnd\n";
}

// samplebuffer_test.cpp's own isIntermediate: a value strictly between two
// references taken from the rendered image itself, at least guard away
// from either.
bool isIntermediate(int coveredR, int backgroundR, int guard, int r) {
  const int lo = std::min(coveredR, backgroundR) + guard;
  const int hi = std::max(coveredR, backgroundR) - guard;
  return r > lo && r < hi;
}

// 1/16 is one sample's own weight at 4x4 supersampling -- the coarsest
// step either colour or alpha can move by, since resolve() filters both
// over the same samples with the same weights. Doubled for the
// independent 8-bit rounding each byte (colour's and alpha's) carries on
// top of that shared sampling grid.
constexpr double kFractionTolerance = 2.0 / 16.0;

void testPartialCoverage(const std::string& gman, const std::string& rendererFlag, const std::string& tag) {
  const std::string rib = "partial_" + tag + ".rib";
  const std::string tif = "partial_" + tag + ".tif";
  writeFile(rib, edgeRib(tif));
  check(runGman(gman, rib, rendererFlag) == 0, tag + ": partial-coverage scene renders");

  RawImage img = readRawTIFF(tif);
  check(img.ok, tag + ": partial-coverage scene reads back");
  if (!img.ok) {
    return;
  }

  const uint32_t y = img.height / 2;
  const int guard = 4;
  const int coveredR = img.r(0, y);
  const int backgroundR = img.r(img.width - 1, y);
  check(std::abs(coveredR - backgroundR) > 2 * guard,
        tag + ": covered and background references differ enough to classify an intermediate value");

  uint32_t intermediateX = img.width;
  for (uint32_t x = 0; x < img.width; ++x) {
    if (isIntermediate(coveredR, backgroundR, guard, img.r(x, y))) {
      intermediateX = x;
      break;
    }
  }
  check(intermediateX < img.width, tag + ": an intermediate-coverage pixel exists on the edge's scanline");
  if (intermediateX >= img.width) {
    return;
  }

  const int alpha = img.a(intermediateX, y);
  check(alpha > 0 && alpha < 255,
        tag + ": the intermediate pixel's alpha lies strictly between 0 and 255, got " + std::to_string(alpha));

  const double colourFraction = (double)(img.r(intermediateX, y) - backgroundR) / (double)(coveredR - backgroundR);
  const double alphaFraction = (double)alpha / 255.0;
  check(std::abs(colourFraction - alphaFraction) <= kFractionTolerance,
        tag + ": alpha's own coverage fraction (" + std::to_string(alphaFraction) +
            ") tracks colour's own coverage fraction (" + std::to_string(colourFraction) + ") within " +
            std::to_string(kFractionTolerance));

  checkPremultiplied(img, intermediateX, y, tag + ": partial-coverage intermediate pixel");
}

// tests/rib/transparency_front_alone.rib: a fully-covered, uniformly
// semi-transparent square at Opacity 0.4 -- already proven under the ray
// tracer by transparencyrender_test.cpp's own centre-pixel reference.
void testOsReflected(const std::string& gman, const std::string& ribDir, const std::string& rendererFlag,
                     const std::string& tag) {
  const std::string rib = ribDir + "/transparency_front_alone.rib";
  const std::string tif = "os_" + tag + ".tif";
  std::remove("transparency_front_alone.tif");
  check(runGman(gman, rib, rendererFlag) == 0, tag + ": transparency_front_alone.rib renders");
  check(std::rename("transparency_front_alone.tif", tif.c_str()) == 0, tag + ": output renames to " + tif);

  RawImage img = readRawTIFF(tif);
  check(img.ok, tag + ": transparency_front_alone.rib reads back");
  if (!img.ok) {
    return;
  }

  const uint32_t cx = 100, cy = 100;
  const int alpha = img.a(cx, cy);
  const int expected = (int)(0.4 * 255);
  check(std::abs(alpha - expected) <= 3,
        tag + ": centre pixel's alpha approximates 0.4*255 within 3 counts, got " + std::to_string(alpha));

  checkPremultiplied(img, cx, cy, tag + ": Os-reflected centre pixel");
}

} // namespace

int main(int argc, char* argv[]) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s <gman-binary> <rib-dir>\n", argv[0]);
    return 2;
  }
  const std::string gman = argv[1];
  const std::string ribDir = argv[2];

  testExtremes(gman, "", "zbuffer");
  testExtremes(gman, "-r gmanraytracer", "raytracer");

  testPartialCoverage(gman, "", "zbuffer");
  testPartialCoverage(gman, "-r gmanraytracer", "raytracer");

  testOsReflected(gman, ribDir, "", "zbuffer");
  testOsReflected(gman, ribDir, "-r gmanraytracer", "raytracer");

  return checkSummary("coverage alpha tracks real per-sample opacity under both renderers");
}
