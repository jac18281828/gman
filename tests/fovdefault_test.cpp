/* SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Copyright (c) 2026 John Cairns <john@2ad.com>
 */

/*
 * Defect 0 (uat-defects prompt): a `Projection` with no explicit "fov" threw
 * RIE_CONSISTENCY -- GMANParameterList: TOKEN_NOT_FOUND out of
 * GMANParameterList::getPointer and wrote no file. RiWorldBegin already had
 * a null check and a fov==0.0 -> 90.0 default, correct per RISpec 3.2, but
 * getPointer threw before that code ever ran. Reproduces for
 * "orthographic" too, where fov is unused.
 *
 * Fixed at the source: GMANParameterList::getPointer now returns NULL for
 * an absent token instead of throwing, so RiWorldBegin's existing default
 * is reached. RiWorldBegin itself is untouched.
 *
 * Proof: a no-fov perspective scene renders, and its silhouette matches the
 * hand-derived geometry for fov=90 -- both an explicit "fov" [90] control
 * scene and the analytic asin(r/d) formula silhouette_test.cpp already
 * establishes. A no-fov orthographic scene also has to reach exit 0.
 *
 * Revert check: restoring the throw in GMANParameterList::getPointer makes
 * every assertion below that depends on "renders" go red -- no file is ever
 * written.
 */

#include <sys/wait.h>

#include <tiffio.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const std::string &what) {
  std::printf("%s: %s\n", ok ? "ok" : "FAIL", what.c_str());
  if (!ok) {
    ++failures;
  }
}

int runGman(const std::string &gman, const std::string &rib) {
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
  double halfWidth() const { return (xmax - xmin) / 2.0; }
  double halfHeight() const { return (ymax - ymin) / 2.0; }
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

  std::vector<uint32_t> raster(width * height);
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

  // ---- perspective, no "fov" at all ----
  const char *noFovRib =
      "Display \"nofov.tif\" \"file\" \"rgba\"\n"
      "Format 200 200 1\n"
      "Projection \"perspective\"\n"
      "Translate 0 0 5\n"
      "WorldBegin\n"
      "Sphere 1 -1 1 360\n"
      "WorldEnd\n";
  writeFile("nofov.rib", noFovRib);
  check(runGman(gman, "nofov.rib") == 0, "no-fov perspective scene renders");

  BBox noFov = findSilhouette("nofov.tif");
  check(noFov.found, "no-fov scene: a silhouette was found");

  // ---- perspective, "fov" [90] explicit control ----
  const char *fov90Rib =
      "Display \"fov90.tif\" \"file\" \"rgba\"\n"
      "Format 200 200 1\n"
      "Projection \"perspective\" \"fov\" [90]\n"
      "Translate 0 0 5\n"
      "WorldBegin\n"
      "Sphere 1 -1 1 360\n"
      "WorldEnd\n";
  writeFile("fov90.rib", fov90Rib);
  check(runGman(gman, "fov90.rib") == 0, "explicit fov=90 scene renders");

  BBox fov90 = findSilhouette("fov90.tif");
  check(fov90.found, "fov=90 scene: a silhouette was found");

  const double tol = 4.0; // both scenes should default/resolve identically
  check(std::fabs(noFov.centerX() - fov90.centerX()) <= tol &&
            std::fabs(noFov.centerY() - fov90.centerY()) <= tol,
        "no-fov and explicit fov=90 silhouettes share a centre");
  check(std::fabs(noFov.halfWidth() - fov90.halfWidth()) <= tol &&
            std::fabs(noFov.halfHeight() - fov90.halfHeight()) <= tol,
        "no-fov and explicit fov=90 silhouettes share an extent");

  // RISpec 3.2's default: asin(1/5) half-angle, tan(45deg) = 1.
  const double tanAngularRadius = std::tan(std::asin(0.2));
  const double expectedHalfExtent = tanAngularRadius * 100.0; // 200px / 2
  check(std::fabs(noFov.halfWidth() - expectedHalfExtent) <= 16.0,
        "no-fov silhouette half-width matches the fov=90 hand-derivation");

  // ---- orthographic, no "fov" (unused, but must not throw either) ----
  const char *orthoNoFovRib =
      "Display \"ortho_nofov.tif\" \"file\" \"rgba\"\n"
      "Format 200 200 1\n"
      "Projection \"orthographic\"\n"
      "Translate 0 0 5\n"
      "WorldBegin\n"
      "Sphere 1 -1 1 360\n"
      "WorldEnd\n";
  writeFile("ortho_nofov.rib", orthoNoFovRib);
  check(runGman(gman, "ortho_nofov.rib") == 0,
        "no-fov orthographic scene renders (fov unused but still looked up)");

  if (failures != 0) {
    std::printf("%d assertion(s) failed\n", failures);
    return 1;
  }
  std::printf("fov defaulting holds\n");
  return 0;
}
