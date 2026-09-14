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
 * The texture cache (gmantexture.h), the texture() shadeop it feeds, and
 * gmanMakeTexture (RiMakeTexture's implementation), proved three ways:
 * directly, against the cache's own sampling contract and the writer's;
 * by rendering tests/rib/texture.rib -- a screen-facing Patch "bilinear"
 * with a paintedplastic surface -- and reading back the four distinct
 * colours its four quadrants should show; and through
 * tests/rib/maketexture_wrap.rib, which writes a texture with MakeTexture
 * before rendering the same quadrants from it.
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
#include <filesystem>
#include <string>
#include <vector>

#include "check.h"
#include "gmancolor.h"
#include "gmanshaderenvironment.h"
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
// gmanoutputtiff.cpp's own write order. A non-empty swrap also sets
// TIFFTAG_PIXAR_WRAPMODES to "<swrap>,<twrap>" -- the tag RiMakeTexture
// writes and GMANTexture reads back; the default leaves a plain, untagged
// checker.
bool writeCheckerTexture(const std::string &path,
                          const std::string &swrap = "",
                          const std::string &twrap = "") {
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
  if (!swrap.empty()) {
    const std::string wrapModes = swrap + "," + twrap;
    TIFFSetField(tif, TIFFTAG_PIXAR_WRAPMODES, wrapModes.c_str());
  }

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

// The three-argument cache sample() reads a file's own recorded wrap
// modes instead of taking one from the caller -- proved against a tag of
// "periodic,black", asymmetric on purpose so an axis swap is visible.
void testRecordedWrapModes(const std::string &pbName,
                            const std::string &plainName,
                            const std::string &mirrorClampName) {
  check(writeCheckerTexture(pbName, "periodic", "black"),
        pbName + " writes with wrap tag \"periodic,black\"");
  GMANTextureCache &cache = gmanTextureCache();

  checkColor(cache.sample(pbName, 1.25, 0.25), kRed,
             "periodic,black: s=1.25 wraps periodically to red");
  checkColor(cache.sample(pbName, 0.25, 1.5), kBlack,
             "periodic,black: t=1.5 is black outside the image");
  checkColor(cache.sample(pbName, 0.25, 0.25), kRed,
             "periodic,black: texel centre (0.25, 0.25) is red");
  checkColor(cache.sample(pbName, 0.75, 0.25), kGreen,
             "periodic,black: texel centre (0.75, 0.25) is green");
  checkColor(cache.sample(pbName, 0.25, 0.75), kBlue,
             "periodic,black: texel centre (0.25, 0.75) is blue");
  checkColor(cache.sample(pbName, 0.75, 0.75), kWhite,
             "periodic,black: texel centre (0.75, 0.75) is white");

  check(writeCheckerTexture(plainName), plainName + " writes with no tag");
  checkColor(cache.sample(plainName, 1.5, 0.25), kGreen,
             "no tag: s=1.5 clamps to the right edge texel");

  check(writeCheckerTexture(mirrorClampName, "mirror", "clamp"),
        mirrorClampName + " writes with wrap tag \"mirror,clamp\"");
  checkColor(cache.sample(mirrorClampName, 1.5, 0.25), kGreen,
             "unparseable tag: falls back to clamp on both axes");
}

// The shadeop: a default-constructed GMANSurfaceEnv's texture() reaches
// the same three-argument cache sample(), so it also honors the file's
// own recorded modes.
void testShadeopUsesRecordedWrapModes(const std::string &pbName) {
  GMANSurfaceEnv env;
  checkColor(env.texture(pbName, 1.25, 0.25), kRed,
             "texture() shadeop: s=1.25 wraps periodically to red");
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

// texture.rib and maketexture_wrap.rib share this checker, this camera and
// this ambientlight, so both read back the same four quadrant colours.
void checkQuadrants(const Image &img) {
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
  checkQuadrants(img);
}

// ---- the writer: gmanMakeTexture (commit 2) ----

std::string readAsciiTag(const std::string &path, ttag_t tag) {
  TIFF *tif = TIFFOpen(path.c_str(), "r");
  if (tif == nullptr) {
    return std::string();
  }
  char *value = nullptr;
  std::string result;
  if (TIFFGetField(tif, tag, &value) && value != nullptr) {
    result = value;
  }
  TIFFClose(tif);
  return result;
}

bool fileExists(const std::string &path) {
  return std::filesystem::exists(path);
}

void testMakeTextureWriter(const std::string &picture,
                            const std::string &texture) {
  std::remove(texture.c_str());
  check(gmanMakeTexture(picture.c_str(), texture.c_str(), "periodic",
                         "black"),
        "gmanMakeTexture(\"periodic\", \"black\") returns true");

  check(readAsciiTag(texture, TIFFTAG_PIXAR_WRAPMODES) == "periodic,black",
        texture + "'s TIFFTAG_PIXAR_WRAPMODES is \"periodic,black\"");
  check(readAsciiTag(texture, TIFFTAG_PIXAR_TEXTUREFORMAT) ==
            "Plain Texture",
        texture + "'s TIFFTAG_PIXAR_TEXTUREFORMAT is \"Plain Texture\"");

  GMANTextureCache &cache = gmanTextureCache();
  checkColor(cache.sample(texture, 0.25, 0.25), kRed,
             "made texture: texel centre (0.25, 0.25) is red");
  checkColor(cache.sample(texture, 0.75, 0.25), kGreen,
             "made texture: texel centre (0.75, 0.25) is green");
  checkColor(cache.sample(texture, 0.25, 0.75), kBlue,
             "made texture: texel centre (0.25, 0.75) is blue");
  checkColor(cache.sample(texture, 0.75, 0.75), kWhite,
             "made texture: texel centre (0.75, 0.75) is white");
  checkColor(cache.sample(texture, 1.25, 0.25), kRed,
             "made texture: s=1.25 wraps periodically to red");
  checkColor(cache.sample(texture, 0.25, 1.5), kBlack,
             "made texture: t=1.5 is black outside the image");
}

void testMakeTextureEviction(const std::string &picture,
                              const std::string &texture) {
  std::remove(texture.c_str());
  GMANTextureCache &cache = gmanTextureCache();

  check(gmanMakeTexture(picture.c_str(), texture.c_str(), "clamp", "clamp"),
        "gmanMakeTexture(\"clamp\", \"clamp\") returns true");
  checkColor(cache.sample(texture, 1.25, 0.25), kGreen,
             "eviction: clamp holds the right edge texel (green)");

  check(gmanMakeTexture(picture.c_str(), texture.c_str(), "periodic",
                         "periodic"),
        "gmanMakeTexture(\"periodic\", \"periodic\") returns true");
  checkColor(cache.sample(texture, 1.25, 0.25), kRed,
             "eviction: a second MakeTexture forgets the cached name, so "
             "this reads periodic (red)");
}

void testMakeTextureFailures(const std::string &picture) {
  const std::string badWrap = "made_bad_wrap.tex";
  std::remove(badWrap.c_str());
  check(! gmanMakeTexture(picture.c_str(), badWrap.c_str(), "mirror",
                           "black"),
        "gmanMakeTexture with an unknown wrap name returns false");
  check(! fileExists(badWrap), badWrap + " is not written");

  const std::string missingPictureTarget = "made_missing_picture.tex";
  std::remove(missingPictureTarget.c_str());
  check(! gmanMakeTexture("texture_test_missing_9f3ab2.tif",
                           missingPictureTarget.c_str(), "clamp", "clamp"),
        "gmanMakeTexture with a picture that does not exist returns false");
  check(! fileExists(missingPictureTarget),
        missingPictureTarget + " is not written");

  check(! gmanMakeTexture(picture.c_str(), "", "clamp", "clamp"),
        "gmanMakeTexture with an empty texture name returns false");
}

// ---- through RIB: MakeTexture followed by a render (commit 2) ----

void testMadeTextureRenders(const std::string &gman,
                             const std::string &ribDir) {
  check(writeCheckerTexture("checker_texture.tif"),
        "checker_texture.tif writes for maketexture_wrap.rib's MakeTexture");
  std::remove("checker_made.tex");

  const std::string rib = ribDir + "/maketexture_wrap.rib";
  check(runGman(gman, rib) == 0, "maketexture_wrap.rib renders");

  check(readAsciiTag("checker_made.tex", TIFFTAG_PIXAR_WRAPMODES) ==
            "periodic,black",
        "checker_made.tex's TIFFTAG_PIXAR_WRAPMODES is \"periodic,black\"");

  Image img = readTIFF("maketexture_wrap.tif");
  check(img.ok, "maketexture_wrap.tif reads back");
  if (!img.ok) {
    return;
  }
  checkQuadrants(img);
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

  std::remove("checker_pb.tif");
  std::remove("checker_plain.tif");
  std::remove("checker_mirror_clamp.tif");
  testRecordedWrapModes("checker_pb.tif", "checker_plain.tif",
                         "checker_mirror_clamp.tif");
  testShadeopUsesRecordedWrapModes("checker_pb.tif");

  testRenderedQuadrants(gman, ribDir);

  testMakeTextureWriter("checker_direct.tif", "made_pb.tex");
  testMakeTextureEviction("checker_direct.tif", "made_evict.tex");
  testMakeTextureFailures("checker_direct.tif");

  testMadeTextureRenders(gman, ribDir);

  return checkSummary("texture holds");
}
