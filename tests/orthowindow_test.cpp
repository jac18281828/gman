/* SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Copyright (c) 2026 John Cairns <john@2ad.com>
 */

/*
 * Defect 5 (uat-defects prompt): orthographic ignored RiScreenWindow and
 * hard-clipped at camera-space x = +-1. GMANPolygonClipper's six planes
 * test x_clip against +-w_clip, the correct NDC test under prjPersp,
 * where invT (from fov) calibrates x_clip and w_clip=z_camera carries no
 * screen-window dependence at all -- RiScreenWindow is applied exactly
 * once, downstream, in GMANViewingSystem::screenToRaster. prjOrtho has no
 * fov-equivalent natural bound to calibrate x_clip/y_clip against, and
 * left w_clip at a constant 1, so the same six planes degenerated into a
 * hardcoded -1 <= x_camera <= 1 box independent of ScreenWindow.
 *
 * Fixed in GMANPolygonClipper::clip: when the active viewing system's
 * projection matrix is orthographic (detected the same way prjPersp and
 * prjOrtho already differ -- mtrx[3][3]==0.0 only under prjPersp, where w
 * carries z), the four side planes are rebuilt from the screen window
 * (GMANViewingSystem::getScreenWindow, new) each call instead of the
 * fixed camera-space unit box. This was the only viable fix: scaling
 * prjOrtho's x,y by the screen window instead (this prompt's other
 * suggested shape) double-applies it, since screenToRaster already scales
 * by the same screen window afterward -- confirmed by direct measurement
 * (a scene with ScreenWindow -2 2 -2 2 came out a quarter the expected
 * area, not the expected half, when tried).
 *
 * This defect and the near-clip precision defect (SPEC.md SS8) are
 * unrelated: this one is a projection/clip-space mismatch (RiScreenWindow
 * dropped entirely), the other is a float-cancellation defect in
 * GMANClipEdge::isInside's back-clip-plane test. Fixing this one does not
 * touch GMANClipEdge or GMANMatrix4::prjPersp/prjOrtho's near/far terms,
 * so it does not fix the near-clip defect; every scene below still pairs
 * an explicit Clipping with a non-default near, the near-clip defect's
 * own documented workaround, to keep that separate defect out of these
 * results.
 *
 * Proof: the prompt's own reproduction, at an analytically computed
 * raster position -- not a pixel-exact match (this sphere's tessellation
 * facets share edges; see cropwindow_test.cpp's note on the same
 * renderer's edge-tie-break float sensitivity, orthogonal to this
 * defect) but a bounding box, which is exactly what the settled decision
 * calls out as an acceptable content assertion.
 *
 * Revert check: reverting GMANPolygonClipper::clip's orthographic branch
 * (falling back to the fixed camera-space unit box for every projection)
 * makes every assertion below that depends on content beyond x_camera=-1
 * or y_camera=1.1 go red -- back to the reported hard clip.
 */

#include <sys/wait.h>

#include <tiffio.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include "check.h"

namespace {

// `output` is removed before the run. Every content assertion below would
// otherwise be satisfied by an image an earlier run left behind: a build
// directory is reused across ctest invocations, and a reverted fix that
// throws before opening the display leaves the previous good file in place.
// tests/baseline_test.cpp removes its target for the same reason.
int runGman(const std::string &gman, const std::string &rib,
            const std::string &output) {
  std::remove(output.c_str());
  const std::string command = "\"" + gman + "\" \"" + rib + "\" >/dev/null 2>&1";
  int status = std::system(command.c_str());
  return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

void writeFile(const std::string &path, const std::string &contents) {
  std::ofstream out(path);
  out << contents;
}

struct BBox {
  bool found = false;
  int xmin = 0, xmax = 0, ymin = 0, ymax = 0;
  double centerX() const { return (xmin + xmax) / 2.0; }
  double centerY() const { return (ymin + ymax) / 2.0; }
};

// Same bounding-box-by-difference-from-corner reading as silhouette_test.cpp.
BBox findSilhouette(const std::string &path) {
  BBox box;
  TIFF *tif = TIFFOpen(path.c_str(), "r");
  if (tif == nullptr) {
    return box;
  }

  uint32_t width = 0, height = 0;
  TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &width);
  TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &height);

  std::vector<uint32_t> raster(size_t(width) * height);
  if (!TIFFReadRGBAImageOriented(tif, width, height, raster.data(),
                                  ORIENTATION_TOPLEFT, 0)) {
    TIFFClose(tif);
    return box;
  }

  const uint32_t bg = raster[0];
  const int tol = 8; // per-channel tolerance out of 255

  auto differs = [&](uint32_t p) {
    int dr = std::abs(int(TIFFGetR(p)) - int(TIFFGetR(bg)));
    int dg = std::abs(int(TIFFGetG(p)) - int(TIFFGetG(bg)));
    int db = std::abs(int(TIFFGetB(p)) - int(TIFFGetB(bg)));
    return dr > tol || dg > tol || db > tol;
  };

  for (uint32_t y = 0; y < height; ++y) {
    for (uint32_t x = 0; x < width; ++x) {
      if (differs(raster[y * width + x])) {
        if (!box.found) {
          box.found = true;
          box.xmin = box.xmax = int(x);
          box.ymin = box.ymax = int(y);
        } else {
          box.xmin = std::min(box.xmin, int(x));
          box.xmax = std::max(box.xmax, int(x));
          box.ymin = std::min(box.ymin, int(y));
          box.ymax = std::max(box.ymax, int(y));
        }
      }
    }
  }

  TIFFClose(tif);
  return box;
}

} // namespace

int main(int argc, char *argv[]) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: %s <gman-binary>\n", argv[0]);
    return 2;
  }
  const std::string gman = argv[1];
  const double tol = 16.0; // matches silhouette_test.cpp's own tolerance

  // ---- the prompt's own reproduction ----
  // ScreenWindow -3 3 -1.5 1.5, Format 400x200, sphere r=1 at
  // Translate -1.5 0 5 -> camera-space x in [-2.5,-0.5], y in [-1,1].
  // raster.x = 400*(x+3)/6, raster.y = 200 - 200*(y+1.5)/3:
  //   x=-2.5 -> 33.33, x=-0.5 -> 166.67 (width ~133.3)
  //   y=-1   -> 166.67, y=1   -> 33.33  (height ~133.3)
  const char *repro =
      "Display \"repro.tif\" \"file\" \"rgb\"\n"
      "Format 400 200 1\n"
      "Projection \"orthographic\"\n"
      "ScreenWindow -3 3 -1.5 1.5\n"
      "Clipping 0.5 100\n"
      "WorldBegin\n"
      "Translate -1.5 0 5\n"
      "Sphere 1 -1 1 360\n"
      "WorldEnd\n";
  writeFile("repro.rib", repro);
  check(runGman(gman, "repro.rib", "repro.tif") == 0, "the reproduction scene renders");

  BBox b = findSilhouette("repro.tif");
  check(b.found, "the reproduction scene: a silhouette was found");
  if (b.found) {
    check(std::abs(b.centerX() - 100.0) <= tol,
          "silhouette centre-x matches the analytically computed raster "
          "position (100)");
    check(std::abs(b.centerY() - 100.0) <= tol,
          "silhouette centre-y matches the analytically computed raster "
          "position (100)");
    check(b.xmin < 133,
          "silhouette extends left of raster x=133 -- the pre-fix hard "
          "clip at camera-space x=-1 -- proving content beyond that line "
          "is no longer discarded");
  }

  // ---- a sphere the pre-fix hard clip made vanish entirely ----
  // Translate 0 1.3 5 puts the whole sphere (y camera-space [0.3,2.3])
  // beyond the old y=+-1.1-ish vanishing point; the wide ScreenWindow's
  // top=1.5 still keeps part of it in frame.
  const char *vanished =
      "Display \"vanished.tif\" \"file\" \"rgb\"\n"
      "Format 400 200 1\n"
      "Projection \"orthographic\"\n"
      "ScreenWindow -3 3 -1.5 1.5\n"
      "Clipping 0.5 100\n"
      "WorldBegin\n"
      "Translate 0 1.3 5\n"
      "Sphere 1 -1 1 360\n"
      "WorldEnd\n";
  writeFile("vanished.rib", vanished);
  check(runGman(gman, "vanished.rib", "vanished.tif") == 0, "the off-centre scene renders");
  BBox v = findSilhouette("vanished.tif");
  check(v.found,
        "a sphere beyond the pre-fix vanishing point still has a "
        "silhouette (used to vanish entirely)");

  // ---- differential control: the default (unit) ScreenWindow still
  // clips at camera-space x=+-1, so this is not a "clip nothing" fix ----
  const char *unit =
      "Display \"unit.tif\" \"file\" \"rgb\"\n"
      "Format 400 200 1\n"
      "Projection \"orthographic\"\n"
      "Clipping 0.5 100\n"
      "WorldBegin\n"
      "Translate -1.5 0 5\n"
      "Sphere 1 -1 1 360\n"
      "WorldEnd\n";
  writeFile("unit.rib", unit);
  check(runGman(gman, "unit.rib", "unit.tif") == 0, "the default-window control scene renders");
  BBox u = findSilhouette("unit.tif");
  // The same sphere (camera-space x in [-2.5,-0.5]) under the *default*
  // screen window (aspect-derived, here [-2,2]x[-1,1] for a 2:1 Format)
  // is clipped at x_camera=-2, inside the sphere itself -- its
  // silhouette has to touch the frame's own left edge (clipped there),
  // unlike the reproduction's silhouette above, which sits well clear of
  // both edges because ScreenWindow -3 3 is wide enough not to clip this
  // sphere at all. Different ScreenWindows now genuinely produce
  // different clipping, not the same hardcoded box either way.
  if (u.found) {
    check(u.xmin <= 4,
          "under the (narrower) default screen window the same sphere is "
          "clipped at the frame's left edge -- honoring ScreenWindow is "
          "not the same as clipping nothing");
  }

  return checkSummary("orthographic screen window holds");
}
