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

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include <sys/wait.h>

#include <tiffio.h>

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

// ---- Polygons (commit 3) ----

// The square's object corners are (0,0), (1,0), (1,1), (0,1); placed by
// Translate -1 1 0; Scale 2 -2 1 so world x = 2*object_x - 1, world y =
// 1 - 2*object_y, filling texture.rib's own [-1,1]^2 footprint, mirrored
// on y. With no "s"/"t"/"st" supplied, each vertex's s, t default to its
// own object x, y (before the CTM): raster (80,80) is object (0,0), s,t=
// (0,0), red; (120,80) is object (1,0), s,t=(1,0), green; (120,120) is
// object (1,1), s,t=(1,1), white; (80,120) is object (0,1), s,t=(0,1),
// blue (texture.rib's own raster = 20*x+100, 100-20*y).
//
// The polygon is two ear-clipped triangles sharing the (120,80)-(80,120)
// diagonal (triangulateEarClipping clips vertex 0 -- (80,80), the first
// convex ear in a convex ring -- first): T1 = ((80,120), (80,80),
// (120,80)), a right triangle with the right angle at (80,80); T2 =
// ((120,80), (120,120), (80,120)), right angle at (120,120). Reading a
// pixel exactly at a corner sits on its own tip, half outside the
// polygon; each case below instead reads a pixel offset from its own
// corner along both of T1 or T2's legs, and derives the exact Gouraud
// (barycentric) blend from the two other vertices sharing that triangle.
//
// (88,88): T1, 8 px along each leg from (80,80). weight(80,80) =
// 1 - 8/40 - 8/40 = 0.6, weight(120,80) [green] = 8/40 = 0.2,
// weight(80,120) [blue] = 8/40 = 0.2. Colour = 0.6*red + 0.2*green +
// 0.2*blue = (0.18, 0.06, 0.06).
// (110,86): T1, legs measured from (80,80): 30/40 toward (120,80)
// [green], 6/40 toward (80,120) [blue]. weight(80,80) = 1 - 0.75 - 0.15 =
// 0.10, weight(120,80) = 0.75, weight(80,120) = 0.15. Colour = 0.10*red +
// 0.75*green + 0.15*blue = (0.03, 0.225, 0.045).
// (86,110): T1, legs from (80,80): 6/40 toward green, 30/40 toward blue.
// weight(80,80) = 0.10, weight(120,80) = 0.15, weight(80,120) = 0.75.
// Colour = 0.10*red + 0.15*green + 0.75*blue = (0.03, 0.045, 0.225).
// (112,112): T2, 8 px along each leg from (120,120) [white].
// weight(120,120) = 0.6, weight(120,80) [green] = 8/40 = 0.2,
// weight(80,120) [blue] = 8/40 = 0.2. Colour = 0.6*white + 0.2*green +
// 0.2*blue = (0.18, 0.24, 0.24).
//
// Revert "polygons pass zero for u, v, s, t again": every vertex reads
// s,t=(0,0) -- red -- so the whole polygon is flat red (0.3,0,0); every
// pixel above differs from that by more than 3*kColorTol in some channel.
//
// Revert "polygon defaults read 'P' after the CTM": the placing
// transform's y-flip sends each vertex's default to a different texel --
// (80,80) [red] to camera-space (-1,1), clamping to (col 0, row 1) =
// blue; (120,80) [green] to (1,1), clamping to (col 1, row 1) = white;
// (120,120) [white] to (1,-1), clamping to (col 1, row 0) = green;
// (80,120) [blue] to (-1,-1), clamping to (col 0, row 0) = red. Re-blending
// each case above with these swapped vertex colours differs from the
// correct colour by more than 3*kColorTol in some channel at every one
// of the four pixels (largest: (110,86)'s blue channel, 0.255 vs 0.045).
void testPolygonDefault(const std::string &gman, const std::string &ribDir) {
  Image img = renderFixture(gman, ribDir, "polygon_default");
  if (!img.ok) return;
  checkPixel(img, 88, 88, {0.18, 0.06, 0.06}, "polygon_default red-leaning");
  checkPixel(img, 110, 86, {0.03, 0.225, 0.045}, "polygon_default green-leaning");
  checkPixel(img, 86, 110, {0.03, 0.045, 0.225}, "polygon_default blue-leaning");
  checkPixel(img, 112, 112, {0.18, 0.24, 0.24}, "polygon_default white-leaning");
}

// Same square, placement and triangulation as polygon_default.rib. "st"
// transposes each vertex's own default (x,y): (0,0) and (1,1) are
// unchanged (on the diagonal), (1,0)->(0,1) [green corner reads blue's
// own texel] and (0,1)->(1,0) [blue corner reads green's own texel] swap.
//
// (88,88) and (112,112) blend the (80,80) [still red] or (120,120)
// [still white] corner with the (120,80) and (80,120) corners at equal
// weight (0.2 each in both cases), so swapping those two changes nothing
// there -- same colours as polygon_default.rib. (110,86) and (86,110)
// weight them unequally (0.75/0.15), so the swap is visible: (110,86),
// mostly (120,80) [now blue], reads (0.03, 0.045, 0.225); (86,110),
// mostly (80,120) [now green], reads (0.03, 0.225, 0.045) -- exactly
// trading polygon_default.rib's own two colours at these two pixels.
//
// Revert "'st' on polygons is ignored": every pixel reads its
// polygon_default.rib colour instead -- (110,86) and (86,110) each
// differ from this fixture's own expected colour by more than
// 3*kColorTol in two channels (green and blue swap entirely).
void testPolygonSt(const std::string &gman, const std::string &ribDir) {
  Image img = renderFixture(gman, ribDir, "polygon_st");
  if (!img.ok) return;
  checkPixel(img, 88, 88, {0.18, 0.06, 0.06}, "polygon_st red-leaning");
  checkPixel(img, 110, 86, {0.03, 0.045, 0.225}, "polygon_st blue-leaning (was green)");
  checkPixel(img, 86, 110, {0.03, 0.225, 0.045}, "polygon_st green-leaning (was blue)");
  checkPixel(img, 112, 112, {0.18, 0.24, 0.24}, "polygon_st white-leaning");
}

// An outer square (object (0,0)-(1,1), same placement as polygon_default)
// with two square holes. The outer loop's first edge, object (0,0)->(1,0)
// -> world (-1,-1)->(1,-1), runs along world +x -- bridgeHoles' own u
// axis. Loop 1 (world x in [-0.5,-0.3], "P" indices 4..7) has rightmostU
// = -0.3+1 = 0.7; loop 2 (world x in [0.3,0.5], "P" indices 8..11) has
// rightmostU = 0.5+1 = 1.5 -- loop 1, listed first in "P", has the
// smaller rightmostU, so bridgeHoles' descending sort commits loop 2
// first and loop 1 second, reversing "P" order.
//
// The outer loop and loop 1 share "st" = (0.75, 0.75) (the white texel)
// at every one of their own vertices, so every triangle touching loop 1's
// own bridge -- whichever ear-clipping happens to cut -- blends between
// vertices that all carry that same value: the region around loop 1 is
// uniformly white. Sampled just outside loop 1's own square (still filled
// -- the hole itself renders nothing), near its (0.25,0.45) "P" corner:
// world (-0.55,-0.15), raster (89,103).
//
// Loop 2 carries a different "st" ((0.25,0.25), red) solely to make a
// commit-order bug visible; nothing samples near loop 2's own hole.
//
// Revert "bridgeHoles assigns hole coordinates in input order instead of
// original 'P' slot": since loop 2 commits before loop 1, an
// implementation indexing by commit position instead of original slot
// reads loop 2's own "st" (red) at loop 1's vertices -- this pixel would
// read red, not white.
void testGeneralPolygonSt(const std::string &gman, const std::string &ribDir) {
  Image img = renderFixture(gman, ribDir, "generalpolygon_st");
  if (!img.ok) return;
  checkPixel(img, 89, 103, scaled(kWhite), "generalpolygon_st white");
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

  testPolygonDefault(gman, ribDir);
  testPolygonSt(gman, ribDir);
  testGeneralPolygonSt(gman, ribDir);

  return checkSummary("texcoords holds");
}
