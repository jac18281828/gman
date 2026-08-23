/* SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Copyright (c) 2026 John Cairns <john@2ad.com>
 */

/*
 * Defect 4 (uat-defects prompt): CropWindow truncated from the origin
 * instead of cropping. `CropWindow 0.25 0.75 0.25 0.75` (the centre) and
 * `CropWindow 0.5 1.0 0.5 1.0` (the bottom-right quadrant) produced
 * byte-identical output -- measured bounding boxes 101-199 and 102-199 in
 * both cases -- always anchored at the full Format's top-left corner.
 *
 * GMANOptions::getRasterInfo (gmanoptions.cpp) computes the crop
 * rectangle's raster bounds correctly, and GMANRenderManImpl sizes the
 * output buffer to it, but nothing then offset rasterization by the
 * rectangle's origin: GMANViewingSystem::screenToRaster always maps into
 * the *full* Format's raster grid (correctly -- RiCropWindow selects a
 * sub-window of that grid, it does not redefine it), and
 * GMANZBufferRenderer::getVertexInfo used that full-grid position
 * directly against the cropped buffer's (width, height), silently
 * dropping anything that landed outside [0, width) x [0, height) -- true
 * for all of a non-top-left crop except whatever happened to also fall
 * within the first rows/columns of the full frame.
 *
 * Fixed in GMANZBufferRenderer: getVertexInfo now subtracts the crop
 * rectangle's own origin (GMANOptions::RasterInfo::rxmin/rymin) before
 * storing a vertex's local position. A related bug shared the same root
 * cause: the numerical-stability margin in that function's sanity check
 * was sized from the (possibly cropped) buffer's width/height, so the
 * same near-singular vertex could be accepted in one crop and rejected in
 * another; both the margin and the check it guards are now sized from the
 * full Format and run before the origin subtraction, so the sanity test
 * itself is crop-invariant.
 *
 * Proof, by bounding box (per the settled decision -- content, not exit
 * status): the reproduction's own sphere, once cropped at the centre,
 * must show a silhouette spanning most of the crop, matching the
 * uncropped render's silhouette offset by the crop's own origin -- not
 * the narrow 101-199-ish sliver defect 4 reported. Cropped at the
 * bottom-right quadrant, the same sphere's silhouette must be anchored at
 * the *crop's* top-left corner (content clipped by the crop boundary),
 * and the two crops must no longer be byte-identical.
 *
 * A pixel-exact match against the uncropped render is deliberately not
 * asserted here: this sphere's tessellation facets share edges, and the
 * z-buffer's tie-break at a shared edge (which facet's independently
 * rounded scan-conversion claims a boundary pixel) is sensitive to the
 * absolute magnitude of the coordinates involved -- a pre-existing
 * floating-point characteristic of accumulating the edge interpolation
 * with repeated `+=`, confirmed unrelated to this defect by dumping every
 * face's projected vertex position for a cropped and uncropped render of
 * the same scene: every one matched, exactly, modulo the crop's integer
 * origin. A single flat, non-adjacent-facet primitive would sidestep it,
 * but RiPolygon cannot be driven through RIB (GMANRIBParse::parsePolygon
 * hardcodes nverts=0, a separate, pre-existing, out-of-scope defect) and
 * RiPatch renders no pixels at all (SPEC.md SS8's recorded defect).
 *
 * Revert check: reverting the getVertexInfo origin subtraction alone
 * reproduces the reported symptom exactly -- both sphere crops go back to
 * byte-identical, and the centre crop's silhouette shrinks back to a
 * narrow strip anchored at the Format's own top-left, not the crop's.
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
  int width() const { return xmax - xmin; }
  int height() const { return ymax - ymin; }
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

bool readTIFFBytes(const std::string &path, std::vector<unsigned char> &out,
                    int &width, int &height) {
  TIFF *tif = TIFFOpen(path.c_str(), "r");
  if (tif == nullptr) return false;
  uint32_t w = 0, h = 0;
  TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &w);
  TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &h);
  std::vector<uint32_t> raster(size_t(w) * h);
  if (!TIFFReadRGBAImageOriented(tif, w, h, raster.data(), ORIENTATION_TOPLEFT, 0)) {
    TIFFClose(tif);
    return false;
  }
  TIFFClose(tif);
  width = int(w);
  height = int(h);
  out.resize(size_t(w) * h * 3);
  for (size_t i = 0; i < size_t(w) * h; ++i) {
    out[i * 3 + 0] = TIFFGetR(raster[i]);
    out[i * 3 + 1] = TIFFGetG(raster[i]);
    out[i * 3 + 2] = TIFFGetB(raster[i]);
  }
  return true;
}

const char *kSceneTemplate =
    "Display \"%s\" \"file\" \"rgb\"\n"
    "Format 400 400 1\n"
    "%s"
    "Projection \"perspective\" \"fov\" [45]\n"
    "WorldBegin\n"
    "Translate 0 0 5\n"
    "Sphere 1 -1 1 360\n"
    "WorldEnd\n";

} // namespace

int main(int argc, char *argv[]) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: %s <gman-binary>\n", argv[0]);
    return 2;
  }
  const std::string gman = argv[1];
  char scene[1024];

  // ---- uncropped baseline ----
  std::snprintf(scene, sizeof scene, kSceneTemplate, "full.tif", "");
  writeFile("full.rib", scene);
  check(runGman(gman, "full.rib") == 0, "uncropped scene renders");
  BBox full = findSilhouette("full.tif");
  check(full.found, "uncropped scene: a silhouette was found");

  // ---- centre crop: 0.25 0.75 0.25 0.75 -> raster [100,299]x[100,299] ----
  std::snprintf(scene, sizeof scene, kSceneTemplate, "centre.tif",
                "CropWindow 0.25 0.75 0.25 0.75\n");
  writeFile("centre.rib", scene);
  check(runGman(gman, "centre.rib") == 0, "centre-crop scene renders");
  BBox centre = findSilhouette("centre.tif");
  check(centre.found, "centre-crop scene: a silhouette was found");

  // ---- bottom-right crop: 0.5 1.0 0.5 1.0 -> raster [200,399]x[200,399] ----
  std::snprintf(scene, sizeof scene, kSceneTemplate, "bottomright.tif",
                "CropWindow 0.5 1.0 0.5 1.0\n");
  writeFile("bottomright.rib", scene);
  check(runGman(gman, "bottomright.rib") == 0, "bottom-right-crop scene renders");
  BBox br = findSilhouette("bottomright.tif");
  check(br.found, "bottom-right-crop scene: a silhouette was found");

  const int tol = 4;

  if (full.found && centre.found) {
    // The centre crop's window starts at raster (100,100), so its
    // silhouette should be the uncropped one, offset by that origin.
    check(std::abs(centre.xmin - (full.xmin - 100)) <= tol &&
              std::abs(centre.xmax - (full.xmax - 100)) <= tol &&
              std::abs(centre.ymin - (full.ymin - 100)) <= tol &&
              std::abs(centre.ymax - (full.ymax - 100)) <= tol,
          "centre crop's silhouette matches the uncropped render's, offset "
          "by the crop rectangle's own origin (100,100)");

    // The defect's own reported shape: a crop truncated to a ~99px-wide
    // sliver near raster 101-199, regardless of which crop was requested.
    // The fixed centre crop should show most of the sphere's own width
    // (194px in the uncropped render), not that sliver.
    check(centre.width() > full.width() - 2 * tol,
          "centre crop's silhouette is not truncated to the pre-fix "
          "101-199-ish sliver");
  }

  if (br.found) {
    // The bottom-right crop's window starts at raster (200,200), deep
    // inside the sphere's own [103,296]x[104,295] bounding box (measured
    // from the uncropped render) -- so the crop boundary itself, not the
    // sphere's true edge, clips this silhouette at its near corner. That
    // near corner has to be the *crop's* origin (0,0), not the full
    // Format's.
    check(br.xmin <= tol && br.ymin <= tol,
          "bottom-right crop's silhouette is anchored at the crop's own "
          "top-left corner (clipped by the crop boundary)");
  }

  // ---- the defect's own reproduction: two different crops must differ ----
  std::vector<unsigned char> centrePixels, brPixels;
  int cw = 0, ch = 0, bw = 0, bh = 0;
  check(readTIFFBytes("centre.tif", centrePixels, cw, ch),
        "centre-crop TIFF decodes");
  check(readTIFFBytes("bottomright.tif", brPixels, bw, bh),
        "bottom-right-crop TIFF decodes");
  check(centrePixels != brPixels,
        "two crops of the same scene at different origins produce "
        "different images (these were byte-identical before the fix)");

  if (failures != 0) {
    std::printf("%d assertion(s) failed\n", failures);
    return 1;
  }
  std::printf("cropwindow holds\n");
  return 0;
}
