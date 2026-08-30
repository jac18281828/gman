/* SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Copyright (c) 2026 John Cairns <john@2ad.com>
 */

/*
 * Step 3/5: tests/rib/screenwindow.rib combines RiScreenWindow (an
 * asymmetric window, shifting the visible frustum off-centre) with
 * RiCropWindow (rendering only a horizontal sub-band of that shifted
 * result) under perspective, plus an unrecognized RiFrameAspectRatio
 * ahead of both to prove the unrecognized-request path does not desync
 * the parse (AGENTS.md's "desync convention"). Neither existing fixture
 * (orthowindow.rib, orthographic only; cropwindow, CropWindow alone)
 * covers the combination.
 *
 * Two checks: the output image is sized to the CropWindow rectangle (200
 * of the full 400px width, all 300px of height -- confirms the crop
 * itself still applies with a non-default ScreenWindow in effect), and a
 * golden-image comparison via the shared harness.
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

}  // namespace

int main(int argc, char *argv[]) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s <gman-binary> <tests/rib-dir>\n", argv[0]);
    return 2;
  }
  const std::string gman = argv[1];
  const std::string ribDir = argv[2];

  const std::string rib = ribDir + "/screenwindow.rib";
  check(runGman(gman, rib) == 0, "screenwindow.rib renders");

  GmanImage img = readGmanTIFF("screenwindow.tif");
  check(img.ok, "screenwindow.rib: TIFF read back");
  if (img.ok) {
    check(img.width == 200 && img.height == 300,
          "screenwindow.rib: output sized to the CropWindow rectangle "
          "(200x300 of the full 400x300 Format), got " +
              std::to_string(img.width) + "x" + std::to_string(img.height));
  }

  checkGoldenImage("screenwindow.tif", ribDir + "/screenwindow_golden.tif", 24,
                   0.01, "screenwindow_diff.tif");

  return checkSummary("screenwindow holds");
}
