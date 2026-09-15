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
 * s, t (and, for a polygon, u, v) reaching texture() on quadrics, Patch and
 * (General)Polygon -- RiTextureCoordinates and the "s"/"t"/"st" primvars
 * GMANPatchPolyObjectManager now resolves. Every fixture under
 * tests/rib/texcoords/ copies tests/rib/texture.rib's own frame (camera,
 * Clipping, ambient light at 0.3, paintedplastic reading
 * checker_texture.tif with Ka=1 Kd=0 Ks=0) and its off-screen desync
 * Sphere, so a rendered texel is that texel's own colour times 0.3, within
 * texture_test.cpp's kColorTol -- and the fixture RIBs derive every pixel
 * they read from tests/checkertexture.h's checker: row 0 (top) red, green;
 * row 1 (bottom) blue, white.
 *
 * Every case's own comment derives its expected colour, and the colour the
 * revert named beside it would produce at that same pixel -- proved by
 * actually reverting the named change and rerunning this suite (see this
 * task's closing report, not this file, for each revert's result).
 */

#include <sys/wait.h>

#include <tiffio.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "check.h"
#include "checkertexture.h"

namespace {

const int kColorTol = 8; // texture_test.cpp's own tolerance, out of 255.

const double kAmbient = 0.3;

struct RGB {
  double r, g, b;
};

const RGB kRed{1.0, 0.0, 0.0};
const RGB kGreen{0.0, 1.0, 0.0};
const RGB kBlue{0.0, 0.0, 1.0};
const RGB kWhite{1.0, 1.0, 1.0};

// A texel's own colour, times the fixture's ambient intensity -- what the
// paintedplastic surface (Ka=1, Kd=0, Ks=0) actually renders it as
// (texture_test.cpp's own Ci = Os * texture() * Cs * Ka * ambient).
RGB scaled(const RGB &texel) {
  return {texel.r * kAmbient, texel.g * kAmbient, texel.b * kAmbient};
}

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

void checkPixel(const Image &img, int x, int y, const RGB &want,
                 const std::string &what) {
  uint32_t p = img.at(x, y);
  double gotR = TIFFGetR(p), gotG = TIFFGetG(p), gotB = TIFFGetB(p);
  double wantR = want.r * 255.0, wantG = want.g * 255.0, wantB = want.b * 255.0;
  check(std::fabs(gotR - wantR) <= kColorTol &&
            std::fabs(gotG - wantG) <= kColorTol &&
            std::fabs(gotB - wantB) <= kColorTol,
        what + " (" + std::to_string(x) + "," + std::to_string(y) +
            "): got (" + std::to_string((int) gotR) + "," +
            std::to_string((int) gotG) + "," + std::to_string((int) gotB) +
            "), want (" + std::to_string((int) wantR) + "," +
            std::to_string((int) wantG) + "," + std::to_string((int) wantB) +
            ")");
}

// Renders rib and reads back its own Display target; every case shares
// this shape, so it is not repeated per case.
Image renderFixture(const std::string &gman, const std::string &ribDir,
                     const std::string &name) {
  const std::string rib = ribDir + "/" + name + ".rib";
  check(runGman(gman, rib) == 0, name + ".rib renders");
  Image img = readTIFF(name + ".tif");
  check(img.ok, name + ".tif reads back");
  return img;
}

// ---- Parametric (commit 2) ----

// TextureCoordinates 0 0  0 1  1 0  1 1 transposes the corners: bilinear
// interpolation at (u,v) then samples the texel that would sit at (v,u)
// under the default mapping. texture.rib's own frame projects grid
// vertices (u,v) = (0.25,0.25), (0.75,0.25), (0.25,0.75), (0.75,0.75) to
// raster (90,110), (110,110), (90,90), (110,90) (texture_test.cpp derives
// this projection); transposing each (u,v) before sampling gives (s,t) =
// (0.25,0.25), (0.25,0.75), (0.75,0.25), (0.75,0.75) -- red, blue, green,
// white.
void testPatchTextureCoordinates(const std::string &gman,
                                  const std::string &ribDir) {
  Image img = renderFixture(gman, ribDir, "patch_texturecoordinates");
  if (!img.ok) return;
  checkPixel(img, 90, 110, scaled(kRed), "patch_texturecoordinates red");
  // Revert "the helper ignores attr's coordinates": corners fall back to
  // the identity default (s=u,t=v), so this pixel reads green (u=0.75,
  // v=0.25's own default texel), not blue.
  checkPixel(img, 110, 110, scaled(kBlue), "patch_texturecoordinates blue");
  checkPixel(img, 90, 90, scaled(kGreen), "patch_texturecoordinates green");
  checkPixel(img, 110, 90, scaled(kWhite), "patch_texturecoordinates white");
}

// "st" [0 0  0 1  1 0  1 1] is the same transpose as
// patch_texturecoordinates.rib's own RiTextureCoordinates, and outranks
// the fixture's mirrored-s RiTextureCoordinates entirely, so the four
// pixels read the same red, blue, green, white.
void testPatchSt(const std::string &gman, const std::string &ribDir) {
  Image img = renderFixture(gman, ribDir, "patch_st");
  if (!img.ok) return;
  checkPixel(img, 90, 110, scaled(kRed), "patch_st red");
  checkPixel(img, 110, 110, scaled(kBlue), "patch_st blue");
  checkPixel(img, 90, 90, scaled(kGreen), "patch_st green");
  checkPixel(img, 110, 90, scaled(kWhite), "patch_st white");
}

// "st" transposes (s1..s4=0,0,1,1; t1..t4=0,1,0,1); "s" [1 0  1 0]
// overrides s1=1, s2=0, s3=1, s4=0, t untouched. Bilinear corners:
// (s1,t1)=(1,0), (s2,t2)=(0,1), (s3,t3)=(1,0), (s4,t4)=(0,1).
//
// At (u,v)=(0.25,0.25), weights ((1-u)(1-v), u(1-v), (1-u)v, uv) =
// (0.5625, 0.1875, 0.1875, 0.0625): s = 0.5625*1+0.1875*0+0.1875*1+
// 0.0625*0 = 0.75; t = 0.5625*0+0.1875*1+0.1875*0+0.0625*1 = 0.25 ->
// (0.75, 0.25) = green.
// At (0.75,0.25), weights (0.1875,0.5625,0.0625,0.1875): s=0.1875+0.0625=
// 0.25; t=0.5625+0.1875=0.75 -> (0.25,0.75) = blue.
// At (0.25,0.75), weights (0.1875,0.0625,0.5625,0.1875): s=0.1875+0.5625=
// 0.75; t=0.0625+0.1875=0.25 -> (0.75,0.25) = green.
// At (0.75,0.75), weights (0.0625,0.1875,0.1875,0.5625): s=0.0625+0.1875=
// 0.25; t=0.1875+0.5625=0.75 -> (0.25,0.75) = blue.
//
// Revert "'st' outranks 's'": both components would come from "st" alone
// (as if "s" were never supplied), the same transpose
// patch_texturecoordinates.rib reads -- red at (90,110), white at
// (110,90). Revert "'s' is ignored" reads identically, since the
// resolver would never look up "s" at all.
void testPatchSOverSt(const std::string &gman, const std::string &ribDir) {
  Image img = renderFixture(gman, ribDir, "patch_s_over_st");
  if (!img.ok) return;
  checkPixel(img, 90, 110, scaled(kGreen), "patch_s_over_st green (top-left)");
  checkPixel(img, 110, 110, scaled(kBlue), "patch_s_over_st blue (top-right)");
  checkPixel(img, 90, 90, scaled(kGreen), "patch_s_over_st green (bottom-left)");
  checkPixel(img, 110, 90, scaled(kBlue), "patch_s_over_st blue (bottom-right)");
}

// TextureCoordinates gives every corner (0.75, 0.75): bilinear
// interpolation of four identical corners is that same constant
// regardless of (u,v), so every grid vertex on the Disk samples the white
// texel exactly. u=0.25 (theta=90 deg of thetamax=360), v=0.25 (r=0.25 of
// radius=1) lands on a grid vertex (URES=VRES=16) at object (x,y,z) =
// (r*cos(theta), r*sin(theta), height) = (0, 0.25, 0) -- world position
// unchanged, camera-facing -- raster (100, 95) by texture.rib's own
// projection (raster = 20*x+100, 100-20*y).
//
// Revert "createParametric shades with u, v again": this pixel's default
// mapping is s=u=0.25, t=v=0.25 -- the texel centre (0.25,0.25), red, not
// white.
void testDiskTextureCoordinates(const std::string &gman,
                                 const std::string &ribDir) {
  Image img = renderFixture(gman, ribDir, "disk_texturecoordinates");
  if (!img.ok) return;
  checkPixel(img, 100, 95, scaled(kWhite), "disk_texturecoordinates white");
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

  check(writeCheckerTexture("checker_texture.tif"),
        "checker_texture.tif writes into the render's working directory");

  testPatchTextureCoordinates(gman, ribDir);
  testPatchSt(gman, ribDir);
  testPatchSOverSt(gman, ribDir);
  testDiskTextureCoordinates(gman, ribDir);

  return checkSummary("texcoords holds");
}
