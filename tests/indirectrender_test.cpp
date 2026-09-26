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
 * `gman -r gmanraytracer` renders tests/rib/indirectseam_*.rib: Option
 * "render" "string indirect" naming constantindirect brightens a matte
 * sphere's own ambient-lit colour by exactly that module's own constant, a
 * missing pass costs nothing, clearing the name undoes it, and the pass
 * reaches a mirror's own reflected hit too.
 *
 * kAmbientIntensity (a, tests/rib/indirectseam_*.rib's own LightSource
 * intensity) and kConstantIndirect (c, constantindirectconstants.h) are
 * chosen so 255 * a and 255 * (a + c) each carry a fractional part in
 * [0.2, 0.3]: inside that window a float rounding error of a few parts in
 * 1e7 cannot cross an integer boundary, so gman::narrowedByte's own
 * truncation (libgman/gmanoutputnarrow.h truncates rather than rounds)
 * lands on the same integer this test computes by hand.
 */

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

#include <sys/wait.h>

#include <tiffio.h>

#include "check.h"
#include "constantindirectconstants.h"
#include "goldenimage.h"

namespace {

// The RIB fixtures' own LightSource "ambientlight" intensity: matches
// tests/rib/indirectseam_*.rib literally.
constexpr float kAmbientIntensity = 0.3931372549019608f;

// The exact bytes a lit, ambient-only pixel narrows to without and with
// the pass bound -- gman::narrowedByte's own truncation, matched here by
// hand.
const int kForegroundByte = static_cast<int>(std::floor(255.0f * kAmbientIntensity));
const int kForegroundWithIndirectByte = static_cast<int>(std::floor(255.0f * (kAmbientIntensity + kConstantIndirect)));

// round(255 * Kr * kConstantIndirect) with Kr = 1: the mirror fixture's
// own reflected-pixel brightening. Within 1 per channel for antialiasing
// and float rounding at the reflected ray's own hit.
constexpr int kMirrorDeltaByte = 13;
constexpr int kMirrorDeltaTol = 1;

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
};

Rendered renderFixture(std::string const& gman, std::string const& ribDir, std::string const& ribName,
                       std::string const& tifName) {
  std::remove(tifName.c_str());
  Rendered rendered;
  rendered.result = runGman(gman, "gmanraytracer", ribDir + "/" + ribName);
  rendered.image = readGmanTIFF(tifName);
  return rendered;
}

bool pixelIdentical(GmanImage const& a, GmanImage const& b, uint32_t x, uint32_t y) {
  uint32_t const pa = a.at(x, y);
  uint32_t const pb = b.at(x, y);
  return TIFFGetR(pa) == TIFFGetR(pb) && TIFFGetG(pa) == TIFFGetG(pb) && TIFFGetB(pa) == TIFFGetB(pb);
}

// Every pixel matches exactly, all three colour channels.
void checkPixelIdentical(GmanImage const& actual, GmanImage const& control, std::string const& tag) {
  check(actual.ok && control.ok, tag + ": both images read back");
  check(actual.width == control.width && actual.height == control.height, tag + ": dimensions match");
  if (!actual.ok || !control.ok || actual.width != control.width || actual.height != control.height) {
    return;
  }
  long mismatched = 0;
  for (uint32_t y = 0; y < actual.height; ++y) {
    for (uint32_t x = 0; x < actual.width; ++x) {
      if (!pixelIdentical(actual, control, x, y)) {
        ++mismatched;
      }
    }
  }
  check(mismatched == 0, tag + ": every pixel matches pixel for pixel (" + std::to_string(mismatched) + " differed)");
}

// The single-sphere family: none, constantindirect, nosuchpass, naming
// constantindirect then clearing it.
void checkSphereFamily(std::string const& gman, std::string const& ribDir) {
  Rendered const none = renderFixture(gman, ribDir, "indirectseam_none.rib", "indirectseam_none.tif");
  Rendered const constant = renderFixture(gman, ribDir, "indirectseam_constant.rib", "indirectseam_constant.tif");
  Rendered const nosuchpass = renderFixture(gman, ribDir, "indirectseam_nosuchpass.rib", "indirectseam_nosuchpass.tif");
  Rendered const cleared = renderFixture(gman, ribDir, "indirectseam_cleared.rib", "indirectseam_cleared.tif");

  check(none.result.exitStatus == 0, "sphere family: indirectseam_none.rib exits 0");
  check(constant.result.exitStatus == 0, "sphere family: indirectseam_constant.rib exits 0");
  check(nosuchpass.result.exitStatus == 0, "sphere family: indirectseam_nosuchpass.rib exits 0");
  check(cleared.result.exitStatus == 0, "sphere family: indirectseam_cleared.rib exits 0");
  check(none.image.ok && constant.image.ok && nosuchpass.image.ok && cleared.image.ok,
        "sphere family: every render's TIFF reads back");
  if (!none.image.ok || !constant.image.ok) {
    return;
  }

  uint32_t const background = none.image.at(0, 0);
  long foregroundCount = 0;
  long backgroundCount = 0;
  long otherCount = 0;
  long violations = 0;

  for (uint32_t y = 0; y < none.image.height; ++y) {
    for (uint32_t x = 0; x < none.image.width; ++x) {
      uint32_t const bare = none.image.at(x, y);
      uint32_t const bound = constant.image.at(x, y);
      int const bareR = TIFFGetR(bare), bareG = TIFFGetG(bare), bareB = TIFFGetB(bare);
      int const boundR = TIFFGetR(bound), boundG = TIFFGetG(bound), boundB = TIFFGetB(bound);

      if (bare == background) {
        ++backgroundCount;
        if (bound != bare) {
          ++violations;
        }
      } else if (bareR == kForegroundByte && bareG == kForegroundByte && bareB == kForegroundByte) {
        ++foregroundCount;
        if (boundR != kForegroundWithIndirectByte || boundG != kForegroundWithIndirectByte ||
            boundB != kForegroundWithIndirectByte) {
          ++violations;
        }
      } else {
        ++otherCount;
        if (boundR < bareR || boundG < bareG || boundB < bareB) {
          ++violations;
        }
      }
    }
  }

  std::printf("sphere family: %ld foreground, %ld background, %ld other pixels\n", foregroundCount, backgroundCount,
              otherCount);
  check(foregroundCount > 0, "sphere family: at least one foreground pixel exists");
  check(backgroundCount > 0, "sphere family: at least one background pixel exists");
  check(otherCount > 0, "sphere family: at least one other (antialiased edge) pixel exists");
  check(violations == 0, "sphere family: every pixel's category holds (" + std::to_string(violations) + " violated)");

  checkPixelIdentical(nosuchpass.image, none.image, "sphere family: nosuchpass matches the baseline");
  checkPixelIdentical(cleared.image, none.image, "sphere family: cleared matches the baseline");

  check(nosuchpass.result.output.find("libnosuchpass.so") != std::string::npos,
        "sphere family: nosuchpass's own run names libnosuchpass.so");
}

// A token RiOptionV never declares, alongside indirect, must not reach
// parameter-list construction or change the render.
void checkExtraTokenIgnored(std::string const& gman, std::string const& ribDir) {
  Rendered const constant = renderFixture(gman, ribDir, "indirectseam_constant.rib", "indirectseam_constant.tif");
  Rendered const extraToken = renderFixture(gman, ribDir, "indirectseam_extratoken.rib", "indirectseam_extratoken.tif");

  check(constant.result.exitStatus == 0, "extra token: indirectseam_constant.rib exits 0");
  check(extraToken.result.exitStatus == 0, "extra token: indirectseam_extratoken.rib exits 0");
  check(extraToken.result.output.find("RIE_BADTOKEN") == std::string::npos,
        "extra token: an undeclared token beside indirect prints no RIE_BADTOKEN line");
  checkPixelIdentical(extraToken.image, constant.image, "extra token: renders as the constant-pass variant does");
}

// The mirror family: none, constantindirect. Proof the pass reaches a
// mirror's own reflected hit, not only a primary one.
void checkMirrorFamily(std::string const& gman, std::string const& ribDir) {
  Rendered const bare = renderFixture(gman, ribDir, "indirectseam_mirror_none.rib", "indirectseam_mirror_none.tif");
  Rendered const bound =
      renderFixture(gman, ribDir, "indirectseam_mirror_constant.rib", "indirectseam_mirror_constant.tif");

  check(bare.result.exitStatus == 0, "mirror family: indirectseam_mirror_none.rib exits 0");
  check(bound.result.exitStatus == 0, "mirror family: indirectseam_mirror_constant.rib exits 0");
  check(bare.image.ok && bound.image.ok, "mirror family: both renders' TIFFs read back");
  if (!bare.image.ok || !bound.image.ok) {
    return;
  }

  uint32_t const background = bare.image.at(0, 0);
  long reflectionCount = 0;
  long backgroundCount = 0;
  long otherCount = 0;
  long violations = 0;

  for (uint32_t y = 0; y < bare.image.height; ++y) {
    for (uint32_t x = 0; x < bare.image.width; ++x) {
      uint32_t const bareP = bare.image.at(x, y);
      uint32_t const boundP = bound.image.at(x, y);
      int const bareR = TIFFGetR(bareP), bareG = TIFFGetG(bareP), bareB = TIFFGetB(bareP);
      int const boundR = TIFFGetR(boundP), boundG = TIFFGetG(boundP), boundB = TIFFGetB(boundP);
      int const deltaR = boundR - bareR, deltaG = boundG - bareG, deltaB = boundB - bareB;

      if (bareP == background) {
        ++backgroundCount;
        if (boundP != bareP) {
          ++violations;
        }
      } else if (std::abs(deltaR - kMirrorDeltaByte) <= kMirrorDeltaTol &&
                 std::abs(deltaG - kMirrorDeltaByte) <= kMirrorDeltaTol &&
                 std::abs(deltaB - kMirrorDeltaByte) <= kMirrorDeltaTol) {
        ++reflectionCount;
      } else {
        ++otherCount;
        if (deltaR < 0 || deltaG < 0 || deltaB < 0) {
          ++violations;
        }
      }
    }
  }

  std::printf("mirror family: %ld reflection, %ld background, %ld other pixels\n", reflectionCount, backgroundCount,
              otherCount);
  check(reflectionCount > 0, "mirror family: at least one reflection-interior pixel exists");
  check(backgroundCount > 0, "mirror family: at least one background pixel exists");
  check(otherCount > 0, "mirror family: at least one other (antialiased edge) pixel exists");
  check(violations == 0, "mirror family: every pixel's category holds (" + std::to_string(violations) + " violated)");
}

} // namespace

int main(int argc, char* argv[]) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s <gman-binary> <tests/rib-dir>\n", argv[0]);
    return 2;
  }
  std::string const gman = argv[1];
  std::string const ribDir = argv[2];

  checkSphereFamily(gman, ribDir);
  checkExtraTokenIgnored(gman, ribDir);
  checkMirrorFamily(gman, ribDir);

  return checkSummary("Option \"render\" \"string indirect\": a bound pass brightens ambient-lit pixels by exactly "
                      "its own constant, a missing pass changes nothing, clearing the name undoes it, an undeclared "
                      "token beside it changes nothing, and the pass reaches a mirror's own reflected hit");
}
