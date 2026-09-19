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
 * `gman -r gmanraytracer` renders tests/rib/r4_raytracer.rib (lights.rib's
 * scene under its own Display) to a round, plastic-shaded sphere matching a
 * checked-in golden image, whose covered-pixel count agrees with the
 * z-buffer's own render of the same scene within 8%, and that differs from
 * the z-buffer's render by more than the golden's own mismatch tolerance --
 * silhouette and specular highlight really differ, not a renderer silently
 * falling back to the other's path.
 *
 * "Covered" is judged against each image's own corner pixel
 * (silhouette_test.cpp's own idiom), not a literal nonzero-channel test:
 * gman's DefaultBGColor is white (255,255,255) despite its own "// Black"
 * comment in libgman/gmandefaults.cpp -- tests/lighting_test.cpp's
 * testTerminator already documents this. A literal nonzero-channel test
 * would read every pixel, background included, as covered.
 *
 * Both renders write r4_raytracer.tif; each is moved aside before the next
 * overwrites it. Any r4_raytracer_raytraced.tif/r4_raytracer_zbuffer.tif left
 * by a prior run is removed first, so a failed render cannot leave this run
 * judging a stale image.
 */

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

#include <sys/wait.h>

#include <tiffio.h>

#include "check.h"
#include "goldenimage.h"

namespace {

int runGman(const std::string& command) {
  int status = std::system(command.c_str());
  return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

// A covered pixel is one that differs from its own image's corner
// (background) pixel by more than 8/255 on any channel -- see the file
// comment on why this, not a literal nonzero-channel test, is what
// "covered" has to mean here.
long coveredPixelCount(GmanImage const& img) {
  uint32_t const bg = img.at(0, 0);
  long count = 0;
  for (uint32_t y = 0; y < img.height; ++y) {
    for (uint32_t x = 0; x < img.width; ++x) {
      uint32_t const p = img.at(x, y);
      if (std::abs(int(TIFFGetR(p)) - int(TIFFGetR(bg))) > 8 || std::abs(int(TIFFGetG(p)) - int(TIFFGetG(bg))) > 8 ||
          std::abs(int(TIFFGetB(p)) - int(TIFFGetB(bg))) > 8) {
        ++count;
      }
    }
  }
  return count;
}

} // namespace

int main(int argc, char* argv[]) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s <gman-binary> <rib-dir>\n", argv[0]);
    return 2;
  }
  std::string const gman = argv[1];
  std::string const ribDir = argv[2];
  std::string const rib = ribDir + "/r4_raytracer.rib";

  // Remove any output a prior run left, so a render that fails to write
  // r4_raytracer.tif cannot leave this run's rename silently reusing that
  // prior output instead of failing.
  std::remove("r4_raytracer_raytraced.tif");
  std::remove("r4_raytracer_zbuffer.tif");

  check(runGman("\"" + gman + "\" -r gmanraytracer \"" + rib + "\" >/dev/null 2>&1") == 0,
        "r4_raytracer.rib renders under -r gmanraytracer");
  check(std::rename("r4_raytracer.tif", "r4_raytracer_raytraced.tif") == 0,
        "r4_raytracer.tif renders and renames to r4_raytracer_raytraced.tif");

  check(runGman("\"" + gman + "\" \"" + rib + "\" >/dev/null 2>&1") == 0,
        "r4_raytracer.rib renders under the default z-buffer");
  check(std::rename("r4_raytracer.tif", "r4_raytracer_zbuffer.tif") == 0,
        "r4_raytracer.tif renders and renames to r4_raytracer_zbuffer.tif");

  // 1. Golden: see tests/goldenimage.h for GOLDEN_CHANNEL_TOL/
  // GOLDEN_MAX_FRACTION's provenance. On failure, r4_raytracer_diff.tif
  // (this test's own build-tree run directory) shows which pixels differed.
  checkGoldenImage("r4_raytracer_raytraced.tif", ribDir + "/r4_raytracer_golden.tif", GOLDEN_CHANNEL_TOL,
                   GOLDEN_MAX_FRACTION, "r4_raytracer_diff.tif");

  GmanImage raytraced = readGmanTIFF("r4_raytracer_raytraced.tif");
  GmanImage zbuffer = readGmanTIFF("r4_raytracer_zbuffer.tif");
  check(raytraced.ok && zbuffer.ok, "both renders read back");
  check(raytraced.width == zbuffer.width && raytraced.height == zbuffer.height, "both renders share one Format");
  if (!raytraced.ok || !zbuffer.ok || raytraced.width != zbuffer.width || raytraced.height != zbuffer.height) {
    return checkSummary("R4's render loop");
  }

  long const raytracedCovered = coveredPixelCount(raytraced);
  long const zbufferCovered = coveredPixelCount(zbuffer);
  double const ratio = zbufferCovered > 0 ? (double)raytracedCovered / (double)zbufferCovered : 0.0;
  std::printf("covered pixels: ray-traced %ld, z-buffer %ld, ratio %g\n", raytracedCovered, zbufferCovered, ratio);

  // 2. Against the z-buffer: covered-pixel counts agree within 8% of the
  // z-buffer's own count.
  check(std::fabs((double)(raytracedCovered - zbufferCovered)) <= 0.08 * (double)zbufferCovered,
        "coverage: the ray tracer's covered-pixel count (" + std::to_string(raytracedCovered) +
            ") is within 8% of the z-buffer's own (" + std::to_string(zbufferCovered) + ")");

  // 3. Not the z-buffer: the two renders' own mismatch, at the golden's
  // tolerance, exceeds GOLDEN_MAX_FRACTION -- the round silhouette and
  // per-sample specular highlight differ from the 16-gon, Gouraud-shaded
  // one by far more than rendering noise.
  long mismatched = 0;
  long const total = (long)raytraced.width * (long)raytraced.height;
  for (uint32_t y = 0; y < raytraced.height; ++y) {
    for (uint32_t x = 0; x < raytraced.width; ++x) {
      uint32_t const a = raytraced.at(x, y);
      uint32_t const b = zbuffer.at(x, y);
      if (std::abs(int(TIFFGetR(a)) - int(TIFFGetR(b))) > GOLDEN_CHANNEL_TOL ||
          std::abs(int(TIFFGetG(a)) - int(TIFFGetG(b))) > GOLDEN_CHANNEL_TOL ||
          std::abs(int(TIFFGetB(a)) - int(TIFFGetB(b))) > GOLDEN_CHANNEL_TOL) {
        ++mismatched;
      }
    }
  }
  double const mismatchFraction = (double)mismatched / (double)total;
  std::printf("mismatch fraction vs z-buffer: %g (%ld/%ld)\n", mismatchFraction, mismatched, total);
  check(mismatchFraction > GOLDEN_MAX_FRACTION,
        "not the z-buffer: the mismatch fraction against the z-buffer's own render (" +
            std::to_string(mismatchFraction) + ") exceeds GOLDEN_MAX_FRACTION");

  return checkSummary("R4's render loop: gman -r gmanraytracer renders a round, plastic-shaded sphere");
}
