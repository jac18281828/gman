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
 * A small red matte sphere, ambient-only, leaves every corner of an 8x8
 * frame as uncovered background. DefaultBGColor (libgman/gmandefaults.cpp)
 * is the RISpec's black, not opaque white, so every corner reads
 * (0, 0, 0) under both renderers. Never asserts on alpha -- OutputTIFF's
 * constant alpha is untested here.
 */

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include <tiffio.h>

#include "check.h"
#include "rungman.h"

namespace {

char const* kSceneRib = "Display \"bg.tif\" \"file\" \"rgba\"\n"
                        "Format 8 8 1\n"
                        "PixelSamples 1 1\n"
                        "Projection \"perspective\" \"fov\" [40]\n"
                        "Clipping 0.5 50\n"
                        "Translate 0 0 5\n"
                        "WorldBegin\n"
                        "LightSource \"ambientlight\" 1 \"intensity\" [1]\n"
                        "Color [1 0 0]\n"
                        "Surface \"matte\"\n"
                        "Sphere 0.4 -0.4 0.4 360\n"
                        "WorldEnd\n";

void writeFile(std::string const& path, std::string const& contents) {
  std::FILE* f = std::fopen(path.c_str(), "w");
  if (f == nullptr) {
    return;
  }
  std::fwrite(contents.data(), 1, contents.size(), f);
  std::fclose(f);
}

// Every corner of an 8x8 frame the sphere never reaches: RGB must read
// exactly (0, 0, 0) against the RISpec default, whatever the renderer.
void checkCornersBlack(std::string const& path, std::string const& label) {
  TIFF* tif = TIFFOpen(path.c_str(), "r");
  check(tif != nullptr, label + ": " + path + " opens");
  if (tif == nullptr) {
    return;
  }

  uint32_t width = 0, height = 0;
  TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &width);
  TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &height);

  std::vector<uint32_t> raster(width * height);
  bool const read = TIFFReadRGBAImageOriented(tif, width, height, raster.data(), ORIENTATION_TOPLEFT, 0);
  check(read, label + ": " + path + " reads back");
  TIFFClose(tif);
  if (!read) {
    return;
  }

  const uint32_t corners[4] = {
      raster[0],                                // (0, 0)
      raster[width - 1],                        // (width-1, 0)
      raster[(height - 1) * width],             // (0, height-1)
      raster[(height - 1) * width + width - 1], // (width-1, height-1)
  };
  char const* names[4] = {"top-left", "top-right", "bottom-left", "bottom-right"};

  for (int i = 0; i < 4; ++i) {
    uint32_t const p = corners[i];
    check(TIFFGetR(p) == 0 && TIFFGetG(p) == 0 && TIFFGetB(p) == 0,
          label + ": " + names[i] + " corner reads (0, 0, 0), got (" + std::to_string(TIFFGetR(p)) + "," +
              std::to_string(TIFFGetG(p)) + "," + std::to_string(TIFFGetB(p)) + ")");
  }
}

} // namespace

int main(int argc, char* argv[]) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: %s <gman-binary>\n", argv[0]);
    return 2;
  }
  std::string const gman = argv[1];

  writeFile("bg_scene.rib", kSceneRib);

  std::remove("bg_zbuffer.tif");
  std::remove("bg_raytracer.tif");
  std::remove("bg.tif");

  check(runGman(gman, {"bg_scene.rib"}).exitStatus == 0, "background scene renders under the default z-buffer");
  check(std::rename("bg.tif", "bg_zbuffer.tif") == 0, "z-buffer output renames to bg_zbuffer.tif");
  checkCornersBlack("bg_zbuffer.tif", "z-buffer");

  check(runGman(gman, {"-r", "gmanraytracer", "bg_scene.rib"}).exitStatus == 0,
        "background scene renders under -r gmanraytracer");
  check(std::rename("bg.tif", "bg_raytracer.tif") == 0, "ray-traced output renames to bg_raytracer.tif");
  checkCornersBlack("bg_raytracer.tif", "ray tracer");

  return checkSummary("DefaultBGColor is black at every uncovered corner, under both renderers");
}
