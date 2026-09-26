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
 * `gman -r gmanraytracer` renders tests/rib/radiosity_box*.rib and
 * tests/rib/radiosity_sphere*.rib: the pass brightens a closed box's own
 * shadowed side without ever dimming a pixel, two renders of the box
 * scene are byte-identical, and a lone sphere -- nothing to bounce light
 * off -- renders exactly as it does with no pass at all.
 */

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include <sys/wait.h>

#include <tiffio.h>

#include "check.h"
#include "goldenimage.h"

namespace {

struct Result {
  int exitStatus;
  std::string output;
};

Result runGman(std::string const& gman, std::string const& renderer, std::string const& rib) {
  std::string const command = "\"" + gman + "\" -r " + renderer + " \"" + rib + "\" 2>&1";
  std::FILE* pipe = popen(command.c_str(), "r");
  Result result{-1, ""};
  if (pipe == nullptr) {
    return result;
  }
  char buffer[512];
  while (std::fgets(buffer, sizeof buffer, pipe) != nullptr) {
    result.output += buffer;
  }
  int const status = pclose(pipe);
  result.exitStatus = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
  return result;
}

struct Rendered {
  Result result;
  GmanImage image;
  std::string path;
};

Rendered renderFixture(std::string const& gman, std::string const& ribDir, std::string const& ribName,
                       std::string const& tifName) {
  std::remove(tifName.c_str());
  Rendered rendered;
  rendered.result = runGman(gman, "gmanraytracer", ribDir + "/" + ribName);
  rendered.image = readGmanTIFF(tifName);
  rendered.path = tifName;
  return rendered;
}

// The full file's own bytes, for a literal cmp -- distinct from decoding
// through libtiff, which would report two files equal on their decoded
// raster even if their own encoded bytes differed.
std::vector<char> readWholeFile(std::string const& path) {
  std::ifstream in(path, std::ios::binary);
  return std::vector<char>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

bool filesByteIdentical(std::string const& a, std::string const& b) {
  std::vector<char> const contentsA = readWholeFile(a);
  std::vector<char> const contentsB = readWholeFile(b);
  return !contentsA.empty() && contentsA == contentsB;
}

// Neither the pass's own prepare() warnings (a bad element size, a
// skipped or capped primitive, an unconverged solve) nor the loader's own
// failure line appear in output.
bool printsNoRadiosityWarning(std::string const& output) {
  return output.find("Indirect-light pass \"radiosity\"") == std::string::npos &&
         output.find("Radiosity:") == std::string::npos;
}

// ---- check 1 & 2: the box scene, with and without the pass ----
void checkBox(std::string const& gman, std::string const& ribDir) {
  Rendered const on = renderFixture(gman, ribDir, "radiosity_box.rib", "radiosity_box.tif");
  Rendered const off = renderFixture(gman, ribDir, "radiosity_box_off.rib", "radiosity_box_off.tif");

  check(on.result.exitStatus == 0, "box: radiosity_box.rib exits 0");
  check(off.result.exitStatus == 0, "box: radiosity_box_off.rib exits 0");
  check(printsNoRadiosityWarning(on.result.output), "box: radiosity_box.rib prints no radiosity warning");
  check(printsNoRadiosityWarning(off.result.output), "box: radiosity_box_off.rib prints no radiosity warning");
  check(on.image.ok && off.image.ok, "box: both renders' TIFFs read back");
  if (!on.image.ok || !off.image.ok || on.image.width != off.image.width || on.image.height != off.image.height) {
    return;
  }

  long blackCount = 0;
  long higherCount = 0;
  long lowerViolations = 0;
  for (uint32_t y = 0; y < off.image.height; ++y) {
    for (uint32_t x = 0; x < off.image.width; ++x) {
      uint32_t const offP = off.image.at(x, y);
      uint32_t const onP = on.image.at(x, y);
      int const offR = TIFFGetR(offP), offG = TIFFGetG(offP), offB = TIFFGetB(offP);
      int const onR = TIFFGetR(onP), onG = TIFFGetG(onP), onB = TIFFGetB(onP);

      if (onR < offR || onG < offG || onB < offB) {
        ++lowerViolations;
      }
      if (offR == 0 && offG == 0 && offB == 0) {
        ++blackCount;
        if (onR > offR || onG > offG || onB > offB) {
          ++higherCount;
        }
      }
    }
  }

  std::printf("radiosityrender: box_off has %ld black pixels; %ld read higher with the pass; %ld pixel(s) read "
              "lower with the pass (want 0)\n",
              blackCount, higherCount, lowerViolations);
  check(blackCount >= 50, "box: radiosity_box_off.rib leaves at least 50 pixels black in every channel");
  check(higherCount == blackCount, "box: every one of those pixels reads higher in at least one channel with the "
                                   "pass");
  check(lowerViolations == 0, "box: no pixel reads lower in any channel with the pass");

  // ---- check 3: two renders of radiosity_box.rib are byte-identical ----
  // The RIB's own Display line always names "radiosity_box.tif", so the
  // first render's own file is copied aside before the second overwrites
  // it.
  std::vector<char> const firstBytes = readWholeFile("radiosity_box.tif");
  Rendered const onAgain = renderFixture(gman, ribDir, "radiosity_box.rib", "radiosity_box.tif");
  check(onAgain.result.exitStatus == 0, "box: the second radiosity_box.rib render exits 0");
  std::vector<char> const secondBytes = readWholeFile("radiosity_box.tif");
  check(!firstBytes.empty() && firstBytes == secondBytes, "box: two renders of radiosity_box.rib are byte-identical");
}

// ---- check 4: the lone-sphere scene matches its own pass-free twin ----
void checkSphere(std::string const& gman, std::string const& ribDir) {
  Rendered const on = renderFixture(gman, ribDir, "radiosity_sphere.rib", "radiosity_sphere.tif");
  Rendered const off = renderFixture(gman, ribDir, "radiosity_sphere_off.rib", "radiosity_sphere_off.tif");

  check(on.result.exitStatus == 0, "sphere: radiosity_sphere.rib exits 0");
  check(off.result.exitStatus == 0, "sphere: radiosity_sphere_off.rib exits 0");
  check(printsNoRadiosityWarning(on.result.output), "sphere: radiosity_sphere.rib prints no radiosity warning");
  check(filesByteIdentical("radiosity_sphere.tif", "radiosity_sphere_off.tif"),
        "sphere: radiosity_sphere.rib matches radiosity_sphere_off.rib pixel for pixel");

  check(on.image.ok, "sphere: the pass render's TIFF reads back");
  if (!on.image.ok) {
    return;
  }
  long nonzero = 0;
  for (uint32_t y = 0; y < on.image.height; ++y) {
    for (uint32_t x = 0; x < on.image.width; ++x) {
      uint32_t const p = on.image.at(x, y);
      if (TIFFGetR(p) != 0 || TIFFGetG(p) != 0 || TIFFGetB(p) != 0) {
        ++nonzero;
      }
    }
  }
  check(nonzero > 0, "sphere: at least one pixel is nonzero");
}

} // namespace

int main(int argc, char* argv[]) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s <gman-binary> <tests/rib-dir>\n", argv[0]);
    return 2;
  }
  std::string const gman = argv[1];
  std::string const ribDir = argv[2];

  checkBox(gman, ribDir);
  checkSphere(gman, ribDir);

  return checkSummary("The radiosity pass brightens a closed box's own shadowed side without ever dimming a "
                      "pixel, two renders of that scene are byte-identical, and a lone sphere renders exactly as "
                      "it does with no pass at all");
}
