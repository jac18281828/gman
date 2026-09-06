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
 * Step 3/5: tests/rib/shaders.rib -- matte, plastic and metal spheres
 * under identical lighting. tests/lighting_test.cpp already proves each
 * shader's own math in isolation (testMetalKaResponse,
 * testMetalSpecularHighlight and the terminator/golden checks against
 * lights.rib's plastic sphere); this scene's own job is the regression
 * pin on all three side by side, so a shader silently regressing to
 * another's output (e.g. metal picking up a diffuse term) shows up here
 * even if it does not move either isolated test's own assertions.
 *
 * Two checks beyond the render itself: three separate silhouettes (a
 * shader that throws or a shape that fails to tessellate drops this),
 * and a golden-image comparison via the shared harness.
 */

#include <sys/wait.h>
#include <tiffio.h>

#include <cstdio>
#include <cstdlib>
#include <string>

#include "check.h"
#include "goldenimage.h"

namespace {

struct Result {
  int exitStatus;
  std::string output;
};

Result runGman(const std::string &gman, const std::string &rib) {
  const std::string command = "\"" + gman + "\" \"" + rib + "\" 2>&1";
  std::FILE *pipe = popen(command.c_str(), "r");
  Result result{-1, ""};
  if (pipe == nullptr) {
    return result;
  }
  char buffer[512];
  while (std::fgets(buffer, sizeof buffer, pipe) != nullptr) {
    result.output += buffer;
  }
  const int status = pclose(pipe);
  result.exitStatus = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
  return result;
}

int countSilhouetteRuns(const GmanImage &img, uint32_t y) {
  if (!img.ok) {
    return -1;
  }
  const uint32_t bg = img.at(0, 0);
  auto differsFromBackground = [&](uint32_t p) {
    return std::abs(int(TIFFGetR(p)) - int(TIFFGetR(bg))) > 8 ||
           std::abs(int(TIFFGetG(p)) - int(TIFFGetG(bg))) > 8 ||
           std::abs(int(TIFFGetB(p)) - int(TIFFGetB(bg))) > 8;
  };
  int runs = 0;
  bool inRun = false;
  for (uint32_t x = 0; x < img.width; ++x) {
    bool differs = differsFromBackground(img.at(x, y));
    if (differs && !inRun) {
      ++runs;
    }
    inRun = differs;
  }
  return runs;
}

}  // namespace

int main(int argc, char *argv[]) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s <gman-binary> <tests/rib-dir>\n", argv[0]);
    return 2;
  }
  const std::string gman = argv[1];
  const std::string ribDir = argv[2];

  const std::string rib = ribDir + "/shaders.rib";
  Result r = runGman(gman, rib);

  check(r.exitStatus == 0, "shaders.rib renders");

  GmanImage img = readGmanTIFF("shaders.tif");
  check(img.ok, "shaders.rib: TIFF read back");
  if (img.ok) {
    int runs = countSilhouetteRuns(img, img.height / 2);
    check(runs == 3,
          "shaders.rib: three separate silhouettes (matte, plastic, "
          "metal) cross the centre scanline (found " +
              std::to_string(runs) + ")");
  }

  checkGoldenImage("shaders.tif", ribDir + "/shaders_golden.tif", 24, 0.01,
                   "shaders_diff.tif");

  return checkSummary("shaders holds");
}
