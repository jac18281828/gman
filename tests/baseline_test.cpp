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
 * The baseline render: tests/rib/sphere_ambient.rib parses, resolves both
 * bracketed "fov" [45] and unbracketed "fov" 45 through
 * GMANRIBParse::parseParameterList, RiWorldBegin loads the zbuffer
 * renderer, and gman runs the sphere through to a real TIFF.
 *
 * The object -> world -> camera -> screen -> NDC -> raster chain (see
 * tests/spacechain_test.cpp and tests/silhouette_test.cpp for the numeric
 * proof) is what puts a real, non-uniform image on the raster: this test
 * asserts the TIFF has more than one distinct pixel value, not just that
 * it exists and is non-empty.
 *
 * A real shader rendering a lightless sphere against the background
 * still gives "more than one distinct value", but is not by itself a
 * demonstration that a real shader ran, since a black sphere and a black
 * bug both look black. What only a *real, deterministic* shader
 * guarantees is that rendering the same RIB twice produces the same
 * image both times; that is the assertion this file adds.
 * (tests/lighting_test.cpp covers the shaded, lit case --
 * tests/rib/lights.rib, with a real gradient and a specular highlight --
 * with its own golden-image comparison; this file stays about the
 * light-free baseline scene.)
 *
 * This file's own render targets `tests/rib/sphere_ambient.rib`: the same
 * sphere as `tests/rib/sphere.rib`, with one `LightSource "ambientlight"`
 * line added, so its silhouette stays distinguishable from the
 * background regardless of which color `DefaultBGColor` is.
 * `tests/rib/sphere.rib` itself stays untouched for every other reader
 * (tests/banner_test.cpp, tests/logfilename_test.cpp, tests/rflag_test.cpp
 * check only exit status and text output, never pixels).
 */

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include <sys/wait.h>

#include <tiffio.h>

#include "check.h"

namespace {

// Distinct pixel values in the produced image -- capped at 2, since this
// only needs to distinguish "uniform" from "not uniform".
int distinctPixelValues(const char* path) {
  TIFF* tif = TIFFOpen(path, "r");
  if (tif == nullptr) {
    return -1;
  }
  uint32_t width = 0, height = 0;
  TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &width);
  TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &height);

  std::vector<uint32_t> raster(width * height);
  bool ok = TIFFReadRGBAImageOriented(tif, width, height, raster.data(), ORIENTATION_TOPLEFT, 0);
  TIFFClose(tif);
  if (!ok || raster.empty()) {
    return -1;
  }

  const uint32_t first = raster[0];
  for (uint32_t p : raster) {
    if (p != first) {
      return 2;
    }
  }
  return 1;
}

// Reads back a TIFF's raster verbatim, for the determinism check: two
// renders of the same RIB must decode to the same pixels, not merely the
// same file size or distinct-value count.
bool readRaster(const char* path, std::vector<uint32_t>& raster) {
  TIFF* tif = TIFFOpen(path, "r");
  if (tif == nullptr) {
    return false;
  }
  uint32_t width = 0, height = 0;
  TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &width);
  TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &height);

  raster.assign(width * height, 0);
  bool ok = TIFFReadRGBAImageOriented(tif, width, height, raster.data(), ORIENTATION_TOPLEFT, 0);
  TIFFClose(tif);
  return ok && !raster.empty();
}

} // namespace

int main(int argc, char* argv[]) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s <gman-binary> <sphere.rib>\n", argv[0]);
    return 2;
  }

  const std::string gman = argv[1];
  const std::string rib = argv[2];
  const char* image = "sphere.tif";

  // CTest runs this in a scratch directory of its own, so the image the RIB
  // asks for would land beside us if it were ever written.
  std::remove(image);

  const std::string command = "\"" + gman + "\" \"" + rib + "\" 2>&1";

  std::FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    std::fprintf(stderr, "FAIL: could not run %s\n", command.c_str());
    return 1;
  }

  std::string output;
  char buffer[512];
  while (std::fgets(buffer, sizeof buffer, pipe) != nullptr) {
    output += buffer;
  }

  const int closeStatus = pclose(pipe);
  const int exitStatus = WIFEXITED(closeStatus) ? WEXITSTATUS(closeStatus) : -1;

  std::printf("--- gman output ---\n%s-------------------\n", output.c_str());

  check(exitStatus == 0, "gman exits 0");
  check(output.find("Parsing") != std::string::npos, "the RIB was opened and parsing started");
  check(output.find("TOKEN_NOT_FOUND") == std::string::npos,
        "the array-parameter bug that pinned exit 1 does not recur");

  // parseParameterList reaches the projection's "fov" [45]; WorldBegin
  // loads the zbuffer renderer, which runs the sphere through to a real
  // TIFF.
  std::FILE* produced = std::fopen(image, "rb");
  check(produced != nullptr, "an image file is produced");
  if (produced != nullptr) {
    std::fseek(produced, 0, SEEK_END);
    long size = std::ftell(produced);
    check(size > 0, "the image file is not empty");
    std::fclose(produced);
  }

  // The coordinate-space chain being connected keeps the sphere from
  // being culled out of existence -- the image has more than one
  // distinct pixel value. (tests/silhouette_test.cpp and
  // tests/spacechain_test.cpp pin the exact geometry; this only pins
  // that *something* is drawn.)
  int distinct = distinctPixelValues(image);
  check(distinct >= 0, "the produced TIFF can be read back");
  check(distinct > 1, "the image is not uniform -- the sphere silhouette is visible");

  // Rendering the same RIB twice must produce the same image both times:
  // a real shader, run on the same geometry and the same lights, cannot
  // do otherwise. A per-vertex random color would pass every check
  // above while still failing this one, since two runs would then
  // differ practically always.
  std::vector<uint32_t> firstRaster;
  check(readRaster(image, firstRaster), "first render's TIFF decodes");

  std::remove(image);
  const int secondStatus = std::system(command.c_str());
  const int secondExit = WIFEXITED(secondStatus) ? WEXITSTATUS(secondStatus) : -1;
  check(secondExit == 0, "second render also exits 0");

  std::vector<uint32_t> secondRaster;
  check(readRaster(image, secondRaster), "second render's TIFF decodes");

  check(firstRaster == secondRaster, "two renders of the same RIB produce pixel-identical images -- "
                                     "real shading is deterministic, confetti was not");

  return checkSummary("baseline holds");
}
