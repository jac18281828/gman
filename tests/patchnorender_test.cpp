/* SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Copyright (c) 2026 John Cairns <john@2ad.com>
 */

/*
 * Expected failure, documented not fixed. tests/rib/patch_norender.rib
 * places a Patch and a control Sphere side by side under identical
 * lighting and shading; the Sphere renders, the Patch does not.
 *
 * SPEC.md Section 8 ("Found by running the corpus after phase 3
 * landed"): confirmed on a minimal single-Patch RIB with no archive, no
 * gzip and no near-clip precision concern -- a rendering-pipeline gap,
 * not a RIB-front-end one (all of bikeData.rib.gz's ~5,300 Patch
 * requests reach the parser and the renderer with no warnings). Out of
 * this phase's scope (Patch tessellation is not the RIB front end);
 * reported here, not fixed. This test pins the defect's current shape so
 * a future fix is a visible, deliberate change to this file, not a
 * silent one: it fails (not the whole binary) if Patch starts rendering
 * without this file being touched.
 */

#include <tiffio.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <sys/wait.h>

#include "check.h"
#include "goldenimage.h"

namespace {

int runGman(const std::string &gman, const std::string &rib) {
  const std::string command =
      "\"" + gman + "\" \"" + rib + "\" >/dev/null 2>&1";
  int status = std::system(command.c_str());
  return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

bool regionHasContent(const GmanImage &img, uint32_t x0, uint32_t x1) {
  const uint32_t bg = img.at(0, 0);
  for (uint32_t y = 0; y < img.height; ++y) {
    for (uint32_t x = x0; x < x1; ++x) {
      uint32_t p = img.at(x, y);
      if (std::abs(int(TIFFGetR(p)) - int(TIFFGetR(bg))) > 8 ||
          std::abs(int(TIFFGetG(p)) - int(TIFFGetG(bg))) > 8 ||
          std::abs(int(TIFFGetB(p)) - int(TIFFGetB(bg))) > 8) {
        return true;
      }
    }
  }
  return false;
}

}  // namespace

int main(int argc, char *argv[]) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s <gman-binary> <tests/rib-dir>\n", argv[0]);
    return 2;
  }
  const std::string gman = argv[1];
  const std::string ribDir = argv[2];

  const std::string rib = ribDir + "/patch_norender.rib";
  check(runGman(gman, rib) == 0, "patch_norender.rib renders (exit 0)");

  GmanImage img = readGmanTIFF("patch_norender.tif");
  check(img.ok, "patch_norender.rib: TIFF read back");
  if (img.ok) {
    const uint32_t mid = img.width / 2;
    check(regionHasContent(img, mid, img.width),
          "control: the right-half Sphere renders");
    check(!regionHasContent(img, 0, mid),
          "known defect (SPEC.md Section 8): the left-half Patch still "
          "rasterizes no pixels -- if this now fails, Patch tessellation "
          "has been fixed and this test (and the SPEC.md entry) should "
          "be updated, not silenced");
  }

  return checkSummary("patch-no-render defect shape holds");
}
