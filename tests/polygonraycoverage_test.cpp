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
 * The ray tracer draws GeneralPolygon, PointsPolygons and
 * PointsGeneralPolygons with the z-buffer's own coverage: each of the
 * four fixtures below, rendered under both `-r gmanzbuffer` (the default)
 * and `-r gmanraytracer`, covers (alpha > 0) the same pixels to within a
 * small fraction of the z-buffer's own covered count. A renderer drawing
 * nothing fails this check: every one of the z-buffer's own covered
 * pixels then differs.
 */

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include <sys/wait.h>

#include <tiffio.h>

#include "check.h"
#include "checkertexture.h"

namespace {

int runGman(const std::string& gman, const std::string& rib, const std::string& rendererFlag) {
  const std::string command = "\"" + gman + "\" " + rendererFlag + " \"" + rib + "\" >/dev/null 2>&1";
  int status = std::system(command.c_str());
  return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

struct RawImage {
  bool ok = false;
  uint32_t width = 0, height = 0;
  int samplesPerPixel = 0;
  std::vector<unsigned char> data;

  unsigned char alpha(uint32_t x, uint32_t y) const {
    return data[((std::size_t)y * width + x) * (std::size_t)samplesPerPixel + 3];
  }
};

// The raw bytes OutputTIFF::save wrote, read back through TIFFReadScanline
// rather than TIFFReadRGBAImageOriented -- the latter may un-premultiply
// associated alpha on decode, coveragealpha_test.cpp's own rationale.
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

std::size_t countCovered(RawImage const& img) {
  std::size_t covered = 0;
  for (uint32_t y = 0; y < img.height; ++y) {
    for (uint32_t x = 0; x < img.width; ++x) {
      if (img.alpha(x, y) > 0) {
        ++covered;
      }
    }
  }
  return covered;
}

// The fraction of the z-buffer's own covered pixels a differing pixel may
// account for.
constexpr double kMaxDifferingFraction = 0.02;

void checkCoverageMatches(const std::string& gman, const std::string& ribDir, const std::string& fixture) {
  const std::string rib = ribDir + "/" + fixture + ".rib";
  const std::string zbufferTif = fixture + "_zbuffer.tif";
  const std::string raytracerTif = fixture + "_raytracer.tif";

  check(runGman(gman, rib, "") == 0, fixture + ": renders under -r gmanzbuffer");
  check(std::rename((fixture + ".tif").c_str(), zbufferTif.c_str()) == 0, fixture + ": z-buffer output renames");

  check(runGman(gman, rib, "-r gmanraytracer") == 0, fixture + ": renders under -r gmanraytracer");
  check(std::rename((fixture + ".tif").c_str(), raytracerTif.c_str()) == 0, fixture + ": ray tracer output renames");

  RawImage const zImg = readRawTIFF(zbufferTif);
  RawImage const rImg = readRawTIFF(raytracerTif);
  check(zImg.ok && rImg.ok, fixture + ": both images read back");
  if (!zImg.ok || !rImg.ok) {
    return;
  }
  check(zImg.width == rImg.width && zImg.height == rImg.height, fixture + ": both images share one size");
  if (zImg.width != rImg.width || zImg.height != rImg.height) {
    return;
  }

  std::size_t const zCovered = countCovered(zImg);
  std::size_t differing = 0;
  for (uint32_t y = 0; y < zImg.height; ++y) {
    for (uint32_t x = 0; x < zImg.width; ++x) {
      if ((zImg.alpha(x, y) > 0) != (rImg.alpha(x, y) > 0)) {
        ++differing;
      }
    }
  }

  check(zCovered > 0, fixture + ": the z-buffer covers at least one pixel");
  std::printf("%s: z-buffer covered=%zu differing=%zu (%.2f%%)\n", fixture.c_str(), zCovered, differing,
              zCovered > 0 ? 100.0 * (double)differing / (double)zCovered : 0.0);
  check(differing <= (std::size_t)(kMaxDifferingFraction * (double)zCovered),
        fixture + ": the ray tracer's coverage matches the z-buffer's within " +
            std::to_string(kMaxDifferingFraction * 100.0) + "% of its own covered count");
}

} // namespace

int main(int argc, char* argv[]) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s <gman-binary> <rib-dir>\n", argv[0]);
    return 2;
  }
  const std::string gman = argv[1];
  const std::string ribDir = argv[2];

  check(writeCheckerTexture("checker_texture.tif"), "checker_texture.tif writes into the render's working directory");

  checkCoverageMatches(gman, ribDir, "generalpolygon_hole_twin");
  checkCoverageMatches(gman, ribDir, "pointsgeneralpolygons_hole");
  checkCoverageMatches(gman, ribDir, "pointspolygons_cube");
  checkCoverageMatches(gman, ribDir, "pointspolygons_textured");

  return checkSummary("the ray tracer's coverage matches the z-buffer's on every polygon-mesh fixture");
}
