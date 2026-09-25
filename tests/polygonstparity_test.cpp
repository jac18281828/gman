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
 * GMANRayPolygon::intersect resolves and interpolates "st" the way
 * GMANPatchPolyObjectManager's own polygon rule (resolvePolygonTextureCoordinates)
 * does. Check 1 pins the ray tracer's own output against the checker
 * texture's known texels, at points a wrong implementation (hit.u =
 * hit.v = 0) would flatten to one texel's colour. Checks 2-3
 * cross-check the ray tracer against the z-buffer's own polygon output,
 * on a texture affine in (s, t) -- see gradienttexture.h for why an
 * affine texture, not the checker, is the right tool for a
 * cross-renderer proof.
 */

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

#include <sys/wait.h>

#include <tiffio.h>

#include "check.h"
#include "checkertexture.h"
#include "goldenimage.h"
#include "gradienttexture.h"

namespace {

const double kAmbient = 0.3;

struct RGB {
  double r, g, b;
};

RGB scaled(const RGB& texel) { return {texel.r * kAmbient, texel.g * kAmbient, texel.b * kAmbient}; }

int runGman(const std::string& command) {
  int status = std::system(command.c_str());
  return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

void checkPixelColor(const GmanImage& img, uint32_t x, uint32_t y, const RGB& want, int tol, const std::string& what) {
  uint32_t const p = img.at(x, y);
  double const gotR = TIFFGetR(p), gotG = TIFFGetG(p), gotB = TIFFGetB(p);
  double const wantR = want.r * 255.0, wantG = want.g * 255.0, wantB = want.b * 255.0;
  check(std::fabs(gotR - wantR) <= tol && std::fabs(gotG - wantG) <= tol && std::fabs(gotB - wantB) <= tol,
        what + " (" + std::to_string(x) + "," + std::to_string(y) + "): got (" + std::to_string((int)gotR) + "," +
            std::to_string((int)gotG) + "," + std::to_string((int)gotB) + "), want (" + std::to_string((int)wantR) +
            "," + std::to_string((int)wantG) + "," + std::to_string((int)wantB) + ")");
}

bool colorsMatch(uint32_t pa, uint32_t pb, int tol) {
  return std::abs((int)TIFFGetR(pa) - (int)TIFFGetR(pb)) <= tol &&
         std::abs((int)TIFFGetG(pa) - (int)TIFFGetG(pb)) <= tol &&
         std::abs((int)TIFFGetB(pa) - (int)TIFFGetB(pb)) <= tol;
}

bool pixelsMatch(const GmanImage& a, const GmanImage& b, uint32_t x, uint32_t y, int tol) {
  return colorsMatch(a.at(x, y), b.at(x, y), tol);
}

// Three interior points well inside the quad's own silhouette (raster
// [80,120] x [80,120]), away from any edge: object (0.25,0.25), (0.75,
// 0.25), (0.25,0.75) under polygon_default.rib's own object-to-raster
// mapping (texcoords_test.cpp's own derivation).
struct Sample {
  uint32_t x, y;
};
const Sample kSamples[3] = {{90, 90}, {110, 90}, {90, 110}};

// True when at least one pair of the three samples differs by more than
// tol on some channel -- real spatial variation, not one constant colour
// both renderers happen to share.
bool samplesVary(const GmanImage& img, int tol) {
  for (int i = 0; i < 3; ++i) {
    for (int j = i + 1; j < 3; ++j) {
      if (!colorsMatch(img.at(kSamples[i].x, kSamples[i].y), img.at(kSamples[j].x, kSamples[j].y), tol)) {
        return true;
      }
    }
  }
  return false;
}

// Renders rib under -r gmanraytracer and, separately, the default
// z-buffer, and reads back both -- tests/polygonzbuffer_test.cpp's own
// render/rename/readback idiom, so the second render's Display output
// does not clobber the first before it is read.
void parityCheck(const std::string& gman, const std::string& rib, const std::string& displayName,
                 const std::string& label) {
  const std::string rayTif = displayName + "_raytraced.tif";
  const std::string zTif = displayName + "_zbuffer.tif";
  std::remove(rayTif.c_str());
  std::remove(zTif.c_str());
  std::remove((displayName + ".tif").c_str());

  int const rayStatus = runGman("\"" + gman + "\" -r gmanraytracer \"" + rib + "\" >/dev/null 2>&1");
  check(rayStatus == 0, label + ": renders under -r gmanraytracer (exit " + std::to_string(rayStatus) + ")");
  check(std::rename((displayName + ".tif").c_str(), rayTif.c_str()) == 0, label + ": renames the ray-traced output");

  int const zStatus = runGman("\"" + gman + "\" \"" + rib + "\" >/dev/null 2>&1");
  check(zStatus == 0, label + ": renders under the default z-buffer (exit " + std::to_string(zStatus) + ")");
  check(std::rename((displayName + ".tif").c_str(), zTif.c_str()) == 0, label + ": renames the z-buffer output");

  GmanImage raytraced = readGmanTIFF(rayTif);
  GmanImage zbuffer = readGmanTIFF(zTif);
  check(raytraced.ok && zbuffer.ok, label + ": both renders read back");
  if (!raytraced.ok || !zbuffer.ok) {
    return;
  }

  for (const Sample& s : kSamples) {
    check(pixelsMatch(raytraced, zbuffer, s.x, s.y, GOLDEN_CHANNEL_TOL),
          label + ": ray tracer and z-buffer agree at (" + std::to_string(s.x) + "," + std::to_string(s.y) + ")");
  }
  check(samplesVary(raytraced, GOLDEN_CHANNEL_TOL),
        label + ": the three sample points are not all mutually within tolerance (real spatial variation)");
}

} // namespace

int main(int argc, char* argv[]) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s <gman-binary> <tests/rib-dir>\n", argv[0]);
    return 2;
  }
  const std::string gman = argv[1];
  const std::string ribDir = argv[2];

  check(writeCheckerTexture("checker_texture.tif"), "checker_texture.tif writes into the render's working directory");
  check(writeGradientTexture("gradient_texture.tif"),
        "gradient_texture.tif writes into the render's working directory");

  // 1. Non-flat, ray-tracer only: today's bug (hit.u = hit.v = 0) renders
  // every one of these four points at texel (0, 0)'s own colour (scaled
  // red); the fix must instead resolve and interpolate "st" per vertex, so
  // each point reads its own quadrant's texel.
  {
    const std::string rib = ribDir + "/texcoords/polygon_default.rib";
    std::remove("polygon_default.tif");
    int const status = runGman("\"" + gman + "\" -r gmanraytracer \"" + rib + "\" >/dev/null 2>&1");
    check(status == 0, "polygon_default.rib renders under -r gmanraytracer (exit " + std::to_string(status) + ")");
    GmanImage img = readGmanTIFF("polygon_default.tif");
    check(img.ok, "polygon_default.tif (ray tracer) reads back");
    if (img.ok) {
      checkPixelColor(img, 88, 88, scaled({1.0, 0.0, 0.0}), GOLDEN_CHANNEL_TOL, "ray-traced polygon red quadrant");
      checkPixelColor(img, 110, 86, scaled({0.0, 1.0, 0.0}), GOLDEN_CHANNEL_TOL, "ray-traced polygon green quadrant");
      checkPixelColor(img, 86, 110, scaled({0.0, 0.0, 1.0}), GOLDEN_CHANNEL_TOL, "ray-traced polygon blue quadrant");
      checkPixelColor(img, 112, 112, scaled({1.0, 1.0, 1.0}), GOLDEN_CHANNEL_TOL, "ray-traced polygon white quadrant");
    }
  }

  // 2. Default-mapping parity, on a texture affine in (s, t).
  parityCheck(gman, ribDir + "/polygon_gradient_default.rib", "polygon_gradient_default", "default-mapping parity");

  // 3. "st"-override parity, same fixture family, transposed "st".
  parityCheck(gman, ribDir + "/polygon_gradient_st.rib", "polygon_gradient_st", "\"st\"-override parity");

  return checkSummary("polygonstparity: the ray tracer resolves and interpolates \"st\" on a Polygon");
}
