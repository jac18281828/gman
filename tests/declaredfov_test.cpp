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
 * resolveViewingSystemInputs resolves "fov" through the dictionary that
 * built the projection's own parameter list (RiProjectionV's dictionary,
 * for RiWorldBegin's call). tests/rib/declaredfov.rib redeclares "fov"
 * with RiDeclare before setting it to 30; tests/rib/declaredfov90.rib is
 * the identical scene at fov 90, the renderer's own default. A dictionary
 * mismatch would miss the redeclared value, defaulting to 90 and warning
 * "FOV not set" despite the RIB setting it explicitly.
 */

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

#include "check.h"
#include "goldenimage.h"

namespace {

int runGman(const std::string& gman, const std::string& rib, const std::string& outPath) {
  const std::string command = "\"" + gman + "\" \"" + rib + "\" > \"" + outPath + "\" 2>&1";
  int status = std::system(command.c_str());
  return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

} // namespace

int main(int argc, char* argv[]) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s <gman-binary> <tests/rib-dir>\n", argv[0]);
    return 2;
  }
  const std::string gman = argv[1];
  const std::string ribDir = argv[2];

  const std::string declaredOut = "declaredfov.out";
  check(runGman(gman, ribDir + "/declaredfov.rib", declaredOut) == 0, "declaredfov.rib renders");

  std::ifstream in(declaredOut);
  std::ostringstream contents;
  contents << in.rdbuf();
  check(contents.str().find("FOV not set") == std::string::npos,
        "a declared, explicitly set fov prints no \"FOV not set\" warning");

  const std::string controlOut = "declaredfov90.out";
  check(runGman(gman, ribDir + "/declaredfov90.rib", controlOut) == 0, "declaredfov90.rib renders");

  GmanImage const declared = readGmanTIFF("declaredfov.tif");
  GmanImage const control = readGmanTIFF("declaredfov90.tif");
  check(declared.ok && control.ok, "both renders produced a readable TIFF");
  if (!declared.ok || !control.ok) {
    return checkSummary("declaredfov holds");
  }
  check(declared.width == control.width && declared.height == control.height,
        "declared (fov 30) and control (fov 90) renders share one frame size");

  long differing = 0;
  const long total = (long)declared.width * (long)declared.height;
  for (uint32_t y = 0; y < declared.height; ++y) {
    for (uint32_t x = 0; x < declared.width; ++x) {
      uint32_t const a = declared.at(x, y);
      uint32_t const b = control.at(x, y);
      if (std::abs(int(TIFFGetR(a)) - int(TIFFGetR(b))) > GOLDEN_CHANNEL_TOL ||
          std::abs(int(TIFFGetG(a)) - int(TIFFGetG(b))) > GOLDEN_CHANNEL_TOL ||
          std::abs(int(TIFFGetB(a)) - int(TIFFGetB(b))) > GOLDEN_CHANNEL_TOL) {
        ++differing;
      }
    }
  }
  const double fraction = total > 0 ? (double)differing / (double)total : 0.0;
  check(fraction > 0.05, "fov 30 renders a visibly different frame from fov 90 (" + std::to_string(differing) + "/" +
                             std::to_string(total) + " pixels differ)");

  return checkSummary("declaredfov holds");
}
