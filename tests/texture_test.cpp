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
 * The texture cache (gmantexture.h) and the texture() shadeop it feeds,
 * proved two ways: directly, against the cache's own sampling contract,
 * and by rendering tests/rib/texture.rib -- a screen-facing Patch
 * "bilinear" with a paintedplastic surface -- and reading back the four
 * distinct colours its four quadrants should show.
 *
 * The fixture's own texture is 2x2 and asymmetric on purpose (row 0, top:
 * red, green; row 1, bottom: blue, white) -- a checker survives a
 * vertical flip, a horizontal flip and an s/t transposition, and all
 * three are the mistakes this test exists to catch.
 *
 * Quadrant geometry: the patch's "P" spans world x, y in [-1, 1] at z=0,
 * and createParametric sets s=u, t=v directly, so a shading vertex at
 * (u, v) = (0.25, 0.25) has world position (-0.5, -0.5) and texture
 * coordinate (s, t) = (0.25, 0.25) -- the exact centre of the 2x2
 * texture's top-left texel. The fixture's camera (Translate 0 0 5, fov
 * 90) is polygon_test.cpp's own: world z=0 maps to camera z=5, so the
 * projection is exactly affine at this fixed depth (raster = 20*x + 100,
 * 100 - 20*y). Hand-projecting the four quadrant centres:
 *
 *   (u, v)        world (x, y)   (s, t)        raster      texel
 *   (0.25, 0.25)  (-0.5, -0.5)   (0.25, 0.25)  (90, 110)   top-left  (red)
 *   (0.75, 0.25)  ( 0.5, -0.5)   (0.75, 0.25)  (110, 110)  top-right (green)
 *   (0.25, 0.75)  (-0.5,  0.5)   (0.25, 0.75)  (90, 90)    bot-left  (blue)
 *   (0.75, 0.75)  ( 0.5,  0.5)   (0.75, 0.75)  (110, 90)   bot-right (white)
 *
 * u=0.25 and u=0.75 land exactly on grid vertices (URES=16 in
 * createParametric), so bilinear texture sampling there returns the
 * texel's own colour with no blend, and Gouraud interpolation introduces
 * no gradient at a grid vertex itself.
 *
 * Lighting matches polygon_test.cpp's own fixture: a single ambientlight
 * at intensity 0.3, Ka=1, Kd=0, Ks=0, so
 * Ci = Os * texture() * Cs * Ka * ambient_intensity = texel * 0.3
 * (GMANPaintedPlastic::computeCi), independent of N/I/E.
 *
 * Revert-falsifiable by construction: a texture() returning any constant
 * collapses all four quadrants to one colour and fails at least three of
 * the four kQuadrant assertions below. Verified by actually reverting
 * GMANSurfaceEnv::texture to return a constant white -- see this task's
 * closing report, not this file, for that result.
 */

#include <sys/wait.h>

#include <tiffio.h>

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "check.h"
#include "gmancolor.h"
#include "gmantexture.h"

namespace {

const int kColorTol = 8; // per-channel tolerance out of 255, this
                          // codebase's standard (polygon_test.cpp,
                          // silhouette_test.cpp, lighting_test.cpp)

// Direct GMANColor comparisons below are exact rational arithmetic over
// texel centres (0.25, 0.75) and half-integer texel-space offsets, so a
// tight float tolerance still leaves headroom for rounding.
const RtFloat kSampleTol = (RtFloat) 1.0e-4;

const GMANColor kRed((RtFloat) 1.0, (RtFloat) 0.0, (RtFloat) 0.0);
const GMANColor kGreen((RtFloat) 0.0, (RtFloat) 1.0, (RtFloat) 0.0);
const GMANColor kBlue((RtFloat) 0.0, (RtFloat) 0.0, (RtFloat) 1.0);
const GMANColor kWhite((RtFloat) 1.0, (RtFloat) 1.0, (RtFloat) 1.0);
const GMANColor kBlack((RtFloat) 0.0, (RtFloat) 0.0, (RtFloat) 0.0);

bool colorNear(const GMANColor &got, const GMANColor &want, RtFloat tol) {
  return std::fabs(got.getRed() - want.getRed()) <= tol &&
         std::fabs(got.getGreen() - want.getGreen()) <= tol &&
         std::fabs(got.getBlue() - want.getBlue()) <= tol;
}

std::string describe(const GMANColor &c) {
  return "(" + std::to_string(c.getRed()) + ", " +
         std::to_string(c.getGreen()) + ", " + std::to_string(c.getBlue()) +
         ")";
}

void checkColor(const GMANColor &got, const GMANColor &want,
                 const std::string &what) {
  check(colorNear(got, want, kSampleTol),
        what + ": got " + describe(got) + ", want " + describe(want));
}

// Writes a 2x2 RGB TIFF, row 0 first -- ORIENTATION_TOPLEFT (set below)
// then makes that row the image's top row on read-back, matching
// gmanoutputtiff.cpp's own write order.
bool writeCheckerTexture(const std::string &path) {
  TIFF *tif = TIFFOpen(path.c_str(), "w");
  if (tif == nullptr) {
    return false;
  }
  TIFFSetField(tif, TIFFTAG_IMAGEWIDTH, (uint32_t) 2);
  TIFFSetField(tif, TIFFTAG_IMAGELENGTH, (uint32_t) 2);
  TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, 8);
  TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, 3);
  TIFFSetField(tif, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT);
  TIFFSetField(tif, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB);
  TIFFSetField(tif, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
  TIFFSetField(tif, TIFFTAG_ROWSPERSTRIP, 1);

  // Row 0 (top): red, green. Row 1 (bottom): blue, white.
  const unsigned char row0[6] = {255, 0, 0, 0, 255, 0};
  const unsigned char row1[6] = {0, 0, 255, 255, 255, 255};
  bool ok = TIFFWriteScanline(tif, (void *) row0, 0, 0) >= 0 &&
            TIFFWriteScanline(tif, (void *) row1, 1, 0) >= 0;
  TIFFClose(tif);
  return ok;
}

// ---- direct cache assertions: tests/rib/texture.rib's own fixture is
// the same checker, so these pin the sampler's contract independent of
// the shading and rasterization the render assertion also exercises. ----

void testBilinearAtTexelCentres(const std::string &name) {
  GMANTextureCache &cache = gmanTextureCache();
  checkColor(cache.sample(name, 0.25, 0.25, GMAN_TEXTURE_CLAMP), kRed,
             "texel centre (0.25, 0.25) is the top-left texel exactly");
  checkColor(cache.sample(name, 0.75, 0.25, GMAN_TEXTURE_CLAMP), kGreen,
             "texel centre (0.75, 0.25) is the top-right texel exactly");
  checkColor(cache.sample(name, 0.25, 0.75, GMAN_TEXTURE_CLAMP), kBlue,
             "texel centre (0.25, 0.75) is the bottom-left texel exactly");
  checkColor(cache.sample(name, 0.75, 0.75, GMAN_TEXTURE_CLAMP), kWhite,
             "texel centre (0.75, 0.75) is the bottom-right texel exactly");
}

void testBilinearHalfway(const std::string &name) {
  GMANTextureCache &cache = gmanTextureCache();
  const GMANColor redGreenMean((RtFloat) 0.5, (RtFloat) 0.5, (RtFloat) 0.0);
  const GMANColor redBlueMean((RtFloat) 0.5, (RtFloat) 0.0, (RtFloat) 0.5);
  checkColor(cache.sample(name, 0.5, 0.25, GMAN_TEXTURE_CLAMP), redGreenMean,
             "halfway between the top two texel centres is their mean");
  checkColor(cache.sample(name, 0.25, 0.5, GMAN_TEXTURE_CLAMP), redBlueMean,
             "halfway between the left two texel centres is their mean");
}

void testTopRowConvention(const std::string &name) {
  GMANTextureCache &cache = gmanTextureCache();
  checkColor(cache.sample(name, 0.25, 0.0, GMAN_TEXTURE_CLAMP), kRed,
             "t=0 samples the image's top row (red), not the bottom");
  checkColor(cache.sample(name, 0.25, 1.0, GMAN_TEXTURE_CLAMP), kBlue,
             "t=1 samples the image's bottom row (blue), not the top");
}

void testWrapModes(const std::string &name) {
  GMANTextureCache &cache = gmanTextureCache();

  checkColor(cache.sample(name, -0.5, 0.25, GMAN_TEXTURE_CLAMP), kRed,
             "clamp: s<0 holds the left edge texel");
  checkColor(cache.sample(name, 1.5, 0.25, GMAN_TEXTURE_CLAMP), kGreen,
             "clamp: s>1 holds the right edge texel");

  checkColor(cache.sample(name, -0.75, 0.25, GMAN_TEXTURE_PERIODIC), kRed,
             "periodic: s=-0.75 wraps to the same texel as s=0.25");
  checkColor(cache.sample(name, 1.25, 0.25, GMAN_TEXTURE_PERIODIC), kRed,
             "periodic: s=1.25 wraps to the same texel as s=0.25");

  checkColor(cache.sample(name, -0.5, 0.25, GMAN_TEXTURE_BLACK), kBlack,
             "black: s<0 returns black");
  checkColor(cache.sample(name, 1.5, 0.25, GMAN_TEXTURE_BLACK), kBlack,
             "black: s>1 returns black");
}

void testMissingFile() {
  GMANColor c = gmanTextureCache().sample("texture_test_missing_9f3ab2.tif",
                                           0.5, 0.5, GMAN_TEXTURE_CLAMP);
  checkColor(c, kBlack, "a name that cannot be decoded returns opaque black");
}

void testSecondLookupReadsNoFile(const std::string &name) {
  check(writeCheckerTexture(name), name + " writes");
  GMANColor before =
      gmanTextureCache().sample(name, 0.25, 0.25, GMAN_TEXTURE_CLAMP);
  checkColor(before, kRed, "first lookup decodes the file");

  check(std::remove(name.c_str()) == 0, name + " deletes");
  GMANColor after =
      gmanTextureCache().sample(name, 0.25, 0.25, GMAN_TEXTURE_CLAMP);
  checkColor(after, kRed,
             "second lookup after deleting the file still reads red: the "
             "cache, not a second decode");
}

// ---- the render assertion ----

int runGman(const std::string &gman, const std::string &rib) {
  const std::string command =
      "\"" + gman + "\" \"" + rib + "\" >/dev/null 2>&1";
  int status = std::system(command.c_str());
  return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

struct Image {
  bool ok = false;
  uint32_t width = 0, height = 0;
  std::vector<uint32_t> raster;

  uint32_t at(int x, int y) const { return raster[y * width + x]; }
};

Image readTIFF(const std::string &path) {
  Image img;
  TIFF *tif = TIFFOpen(path.c_str(), "r");
  if (tif == nullptr) {
    return img;
  }
  TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &img.width);
  TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &img.height);
  img.raster.resize(img.width * img.height);
  img.ok = TIFFReadRGBAImageOriented(tif, img.width, img.height,
                                      img.raster.data(), ORIENTATION_TOPLEFT,
                                      0);
  TIFFClose(tif);
  return img;
}

struct Quadrant {
  int x, y;
  const char *label;
  GMANColor expected; // texel colour * 0.3 (ambientlight intensity, Ka=1)
};

void testRenderedQuadrants(const std::string &gman,
                            const std::string &ribDir) {
  check(writeCheckerTexture("checker_texture.tif"),
        "checker_texture.tif writes into the render's working directory");

  const std::string rib = ribDir + "/texture.rib";
  check(runGman(gman, rib) == 0, "texture.rib renders");

  Image img = readTIFF("texture.tif");
  check(img.ok, "texture.tif reads back");
  if (!img.ok) {
    return;
  }

  const RtFloat kAmbient = (RtFloat) 0.3;
  const Quadrant quadrants[4] = {
      {90, 110, "top-left (red)",
       GMANColor(kRed.getRed() * kAmbient, kRed.getGreen() * kAmbient,
                  kRed.getBlue() * kAmbient)},
      {110, 110, "top-right (green)",
       GMANColor(kGreen.getRed() * kAmbient, kGreen.getGreen() * kAmbient,
                  kGreen.getBlue() * kAmbient)},
      {90, 90, "bottom-left (blue)",
       GMANColor(kBlue.getRed() * kAmbient, kBlue.getGreen() * kAmbient,
                  kBlue.getBlue() * kAmbient)},
      {110, 90, "bottom-right (white)",
       GMANColor(kWhite.getRed() * kAmbient, kWhite.getGreen() * kAmbient,
                  kWhite.getBlue() * kAmbient)},
  };

  for (const Quadrant &q : quadrants) {
    uint32_t p = img.at(q.x, q.y);
    double expectedR = (double) q.expected.getRed() * 255.0;
    double expectedG = (double) q.expected.getGreen() * 255.0;
    double expectedB = (double) q.expected.getBlue() * 255.0;
    check(std::fabs((double) TIFFGetR(p) - expectedR) <= kColorTol,
          std::string("quadrant ") + q.label + " red matches (got " +
              std::to_string(TIFFGetR(p)) + ", expected " +
              std::to_string(expectedR) + ")");
    check(std::fabs((double) TIFFGetG(p) - expectedG) <= kColorTol,
          std::string("quadrant ") + q.label + " green matches (got " +
              std::to_string(TIFFGetG(p)) + ", expected " +
              std::to_string(expectedG) + ")");
    check(std::fabs((double) TIFFGetB(p) - expectedB) <= kColorTol,
          std::string("quadrant ") + q.label + " blue matches (got " +
              std::to_string(TIFFGetB(p)) + ", expected " +
              std::to_string(expectedB) + ")");
  }
}

} // namespace

int main(int argc, char *argv[]) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s <gman-binary> <tests/rib-dir>\n",
                  argv[0]);
    return 2;
  }
  const std::string gman = argv[1];
  const std::string ribDir = argv[2];

  check(writeCheckerTexture("checker_direct.tif"),
        "checker_direct.tif writes for the direct cache assertions");
  testBilinearAtTexelCentres("checker_direct.tif");
  testBilinearHalfway("checker_direct.tif");
  testTopRowConvention("checker_direct.tif");
  testWrapModes("checker_direct.tif");
  testMissingFile();
  testSecondLookupReadsNoFile("checker_reload.tif");

  testRenderedQuadrants(gman, ribDir);

  return checkSummary("texture holds");
}
