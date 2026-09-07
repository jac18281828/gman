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
 * 0.7 -- the sample buffer and pixel filtering.
 *
 * Both proofs render the same fixture: a matte, pure-black rectangle
 * against the default white background, with one hard vertical edge at
 * world x=0.031 -- deliberately off any pixel or sample grid line, so a
 * pixel straddling it genuinely gets partial coverage rather than landing
 * exactly on a sample point by construction. Orthographic (no perspective
 * distortion to reason about) and Clipping 0.5 50 per AGENTS.md's RIB
 * authoring section: this polygon is flat in z, the shape the near-clip
 * precision defect (SPEC.md S8) corrupts hardest at the default near.
 *
 * testSupersamplingEdge is the falsification proof: at 1x1 sample/pixel a
 * hard edge produces only fully-covered or fully-uncovered pixels (one
 * sample per pixel is binary, wherever the edge falls); at 4x4 the pixel
 * the edge crosses gets a genuine intermediate value. Reverting the
 * resolve to take a single sample collapses the 4x4 case back to the 1x1
 * one -- verified by hand, see samplebuffer-REPORT.md, not asserted here
 * (there is no source knob this test can flip at run time to reproduce a
 * revert).
 *
 * testPixelFilterDiffers needs RiPixelFilter's RIB wiring to be real: a
 * box and a Gaussian filter over the same 4x4 samples must resolve to
 * different pixels, or GMANOptions::getPixelFilter still has no real
 * caller.
 */

#include <sys/wait.h>

#include <tiffio.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include "check.h"

namespace {

int runGman(const std::string &gman, const std::string &rib) {
  const std::string command = "\"" + gman + "\" \"" + rib + "\" >/dev/null 2>&1";
  int status = std::system(command.c_str());
  return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

void writeFile(const std::string &path, const std::string &contents) {
  std::ofstream out(path);
  out << contents;
}

struct Image {
  bool ok = false;
  uint32_t width = 0, height = 0;
  std::vector<uint32_t> raster;

  uint32_t at(uint32_t x, uint32_t y) const { return raster[y * width + x]; }
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
                                      img.raster.data(), ORIENTATION_TOPLEFT, 0);
  TIFFClose(tif);
  return img;
}

std::string edgeRib(const std::string &display, const std::string &filterLine,
                     const std::string &samplesLine) {
  return
      "Display \"" + display + "\" \"file\" \"rgba\"\n"
      "Format 100 100 1\n"
      "Projection \"orthographic\"\n"
      "Clipping 0.5 50\n" +
      filterLine + samplesLine +
      "Translate 0 0 5\n"
      "WorldBegin\n"
      "LightSource \"ambientlight\" 1 \"intensity\" [1]\n"
      "Sides 2\n"
      "Color [0 0 0]\n"
      "Surface \"matte\" \"Ka\" [1] \"Kd\" [0]\n"
      "Polygon \"P\" [ -2 -2 0  0.031 -2 0  0.031 2 0  -2 2 0 ]\n"
      "WorldEnd\n";
}

// A pixel reading strictly between the black covered colour and the white
// background -- neither fully covered nor fully uncovered.
bool isIntermediate(uint32_t p) {
  int r = (int) TIFFGetR(p);
  return r > 4 && r < 251;
}

void testSupersamplingEdge(const std::string &gman) {
  writeFile("edge_1x1.rib",
            edgeRib("edge_1x1.tif", "PixelFilter \"box\" 1 1\n",
                    "PixelSamples 1 1\n"));
  check(runGman(gman, "edge_1x1.rib") == 0, "1x1 edge scene renders");

  writeFile("edge_4x4.rib",
            edgeRib("edge_4x4.tif", "PixelFilter \"box\" 1 1\n",
                    "PixelSamples 4 4\n"));
  check(runGman(gman, "edge_4x4.rib") == 0, "4x4 edge scene renders");

  Image img1 = readTIFF("edge_1x1.tif");
  Image img4 = readTIFF("edge_4x4.tif");
  check(img1.ok, "1x1 edge scene: TIFF read back");
  check(img4.ok, "4x4 edge scene: TIFF read back");
  if (!img1.ok || !img4.ok) {
    return;
  }

  const uint32_t y = img1.height / 2;
  bool oneSampleIntermediate = false;
  for (uint32_t x = 0; x < img1.width; ++x) {
    if (isIntermediate(img1.at(x, y))) {
      oneSampleIntermediate = true;
      break;
    }
  }
  check(!oneSampleIntermediate,
        "1x1: the edge's scanline has only fully-covered or "
        "fully-uncovered pixels, no intermediate value");

  bool fourSampleIntermediate = false;
  for (uint32_t x = 0; x < img4.width; ++x) {
    if (isIntermediate(img4.at(x, y))) {
      fourSampleIntermediate = true;
      break;
    }
  }
  check(fourSampleIntermediate,
        "4x4: the same edge's scanline has at least one intermediate "
        "value -- partial sample coverage the resolve actually filtered");
}

void testPixelFilterDiffers(const std::string &gman) {
  // Width held constant at 2 for both fixtures -- only the filter's name
  // (its shape: box vs Gaussian) varies. A fixture that also changes width
  // (as this used to, box at width 1 against Gaussian at width 2) can pass
  // from the width difference alone, without the filter function itself
  // ever being consulted -- falsified by hand: with the resolve's weight
  // forced to a constant 1.0 (RiPixelFilter's name-to-function selection
  // effectively disabled), box-1-vs-gaussian-2 still passed every check
  // including all 5 goldens; box-2-vs-gaussian-2 goes red under the same
  // change, and green again once the kernel is restored.
  writeFile("edge_filter_box.rib",
            edgeRib("edge_filter_box.tif", "PixelFilter \"box\" 2 2\n",
                    "PixelSamples 4 4\n"));
  check(runGman(gman, "edge_filter_box.rib") == 0, "box-filter scene renders");

  writeFile("edge_filter_gaussian.rib",
            edgeRib("edge_filter_gaussian.tif", "PixelFilter \"gaussian\" 2 2\n",
                    "PixelSamples 4 4\n"));
  check(runGman(gman, "edge_filter_gaussian.rib") == 0,
        "gaussian-filter scene renders");

  Image box = readTIFF("edge_filter_box.tif");
  Image gaussian = readTIFF("edge_filter_gaussian.tif");
  check(box.ok, "box-filter scene: TIFF read back");
  check(gaussian.ok, "gaussian-filter scene: TIFF read back");
  if (!box.ok || !gaussian.ok) {
    return;
  }

  const uint32_t y = box.height / 2;
  bool differs = false;
  for (uint32_t x = 0; x < box.width; ++x) {
    int d = std::abs((int) TIFFGetR(box.at(x, y)) -
                      (int) TIFFGetR(gaussian.at(x, y)));
    if (d > 4) {
      differs = true;
      break;
    }
  }
  check(differs,
        "box and Gaussian filters over the same samples produce "
        "different pixels -- RiPixelFilter's RIB wiring is real, not "
        "just parsed and discarded");
}

} // namespace

int main(int argc, char *argv[]) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: %s <gman-binary>\n", argv[0]);
    return 2;
  }
  const std::string gman = argv[1];

  testSupersamplingEdge(gman);
  testPixelFilterDiffers(gman);

  return checkSummary("samplebuffer holds");
}
