/* SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Copyright (c) 2026 John Cairns <john@2ad.com>
 */

/*
 * Phase 3, proof items 4-6: a distantlight along +x makes the brightest
 * pixel land on the +x side and the terminator fall where N.L = 0; a
 * RiSides 1 scene with reversed winding culls every face and a RiSides 2
 * scene of the same geometry does not; tests/rib/lights.rib (the goal
 * scene) matches a checked-in golden image within a per-channel tolerance.
 *
 * Revert checks (verified by actually reverting, not asserted):
 *   - Revert step 2 (createParametric's vertex-normal wiring): every
 *     vertex keeps the default zero normal, N.L is zero everywhere, and
 *     the sphere shades flat ambient -- testTerminator's brightest-pixel
 *     and terminator-position assertions both go red.
 *   - Revert step 6 (GMANZBufferRenderer::getVertexInfo back to drand48):
 *     testGoldenImage fails immediately -- confetti has no relationship to
 *     the checked-in reference.
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

struct Image {
  bool ok = false;
  uint32_t width = 0, height = 0;
  std::vector<uint32_t> raster;

  uint32_t at(uint32_t x, uint32_t y) const { return raster[y * width + x]; }
};

Image readTIFF(const std::string &path) {
  Image img;
  TIFF *tif = TIFFOpen(path.c_str(), "r");
  if (tif == nullptr) {
    return img;
  }
  TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &img.width);
  TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &img.height);
  img.raster.resize(img.width * img.height);
  img.ok = TIFFReadRGBAImageOriented(tif, img.width, img.height,
                                      img.raster.data(), ORIENTATION_TOPLEFT, 0);
  TIFFClose(tif);
  return img;
}

// ---- proof item 4: lighting, brightest pixel and the N.L=0 terminator ----
void testTerminator(const std::string &gman) {
  // Matte, no ambient: Ci is exactly Kd*(N.L) clamped at 0, so the
  // terminator (where shading crosses from lit to unlit) is exactly
  // where N.L = 0 -- a hard boundary, not a matter of degree.
  const char *rib =
      "Display \"lighting_terminator.tif\" \"file\" \"rgba\"\n"
      "Format 200 200 1\n"
      "Projection \"perspective\" \"fov\" [45]\n"
      "Translate 0 0 5\n"
      "WorldBegin\n"
      "LightSource \"distantlight\" 1 \"intensity\" [1] \"from\" [5 0 0] \"to\" [0 0 0]\n"
      "Surface \"matte\" \"Ka\" [0] \"Kd\" [1]\n"
      "Sphere 1 -1 1 360\n"
      "WorldEnd\n";
  writeFile("lighting_terminator.rib", rib);
  check(runGman(gman, "lighting_terminator.rib") == 0,
        "terminator scene renders");

  Image img = readTIFF("lighting_terminator.tif");
  check(img.ok, "terminator scene: TIFF read back");
  if (!img.ok) {
    return;
  }

  // The background (DefaultBGColor) is white, brighter than any lit
  // sample of an unlit-capable matte sphere (Ka=0 here), so "brightest
  // pixel" and "first lit pixel" both have to stay inside the sphere's
  // own silhouette -- otherwise the background itself, everywhere at the
  // frame's edges, wins trivially. Find the silhouette first, the same
  // way silhouette_test.cpp does.
  const uint32_t bg = img.at(0, 0);
  auto differsFromBackground = [&](uint32_t p) {
    return std::abs(int(TIFFGetR(p)) - int(TIFFGetR(bg))) > 8 ||
           std::abs(int(TIFFGetG(p)) - int(TIFFGetG(bg))) > 8 ||
           std::abs(int(TIFFGetB(p)) - int(TIFFGetB(bg))) > 8;
  };

  uint32_t silXmin = img.width, silXmax = 0, silYmin = img.height, silYmax = 0;
  bool silFound = false;
  for (uint32_t y = 0; y < img.height; ++y) {
    for (uint32_t x = 0; x < img.width; ++x) {
      if (differsFromBackground(img.at(x, y))) {
        silFound = true;
        silXmin = std::min(silXmin, x);
        silXmax = std::max(silXmax, x);
        silYmin = std::min(silYmin, y);
        silYmax = std::max(silYmax, y);
      }
    }
  }
  check(silFound, "terminator: a silhouette was found");
  if (!silFound) {
    return;
  }

  // Brightest pixel within the silhouette: assert it is on the +x
  // (right) half -- +x in camera space projects to the right of the
  // frame, matching "from" [5 0 0] in the scene above.
  uint32_t bestX = 0, bestY = 0;
  int bestVal = -1;
  for (uint32_t y = silYmin; y <= silYmax; ++y) {
    for (uint32_t x = silXmin; x <= silXmax; ++x) {
      uint32_t p = img.at(x, y);
      if (!differsFromBackground(p)) {
        continue;  // background showing through a corner of the bbox
      }
      int v = (int) TIFFGetR(p);
      if (v > bestVal) {
        bestVal = v;
        bestX = x;
        bestY = y;
      }
    }
  }
  check(bestVal > 200,
        "terminator: a strongly lit pixel exists within the silhouette "
        "(brightest sample channel > 200/255)");
  check(bestX > (silXmin + silXmax) / 2,
        "terminator: the brightest pixel is on the +x (light) side of "
        "the silhouette");
  (void) bestY;

  // Terminator: along the horizontal scanline through the silhouette's
  // vertical centre, walk from its left edge (unlit -- Ka=0, so
  // diffuse's max(0, N.L) clamp makes that side read exactly zero, not
  // just dark) to find where it first turns nonzero. Kd*(N.L) is
  // continuous through the terminator (a kink at zero, not a step), so
  // this has to catch the first departure from exactly 0, not an
  // arbitrary brightness threshold partway up the ramp -- the sphere is
  // centred on-axis (Translate 0 0 5, no lateral offset), so N.L=0 -- the
  // y-z plane through the sphere's centre -- projects to the
  // silhouette's own centre column, within one tessellation facet.
  // silXmin is the leftmost silhouette column over the *whole* image,
  // not necessarily this row (a circle is narrower off its own widest
  // row); background pixels at (silXmin, midY) are >0 too (white), so
  // this has to enter the silhouette on this row first, then look for
  // the lit transition from there.
  uint32_t midY = (silYmin + silYmax) / 2;
  int enteredSilhouetteX = -1;
  for (uint32_t x = silXmin; x <= silXmax; ++x) {
    if (differsFromBackground(img.at(x, midY))) {
      enteredSilhouetteX = (int) x;
      break;
    }
  }
  check(enteredSilhouetteX >= 0,
        "terminator: the silhouette's centre scanline actually crosses "
        "the silhouette");

  int firstLitX = -1;
  if (enteredSilhouetteX >= 0) {
    for (uint32_t x = (uint32_t) enteredSilhouetteX; x <= silXmax; ++x) {
      if ((int) TIFFGetR(img.at(x, midY)) > 0) {
        firstLitX = (int) x;
        break;
      }
    }
  }
  check(firstLitX >= 0,
        "terminator: a lit pixel exists on the silhouette's centre scanline");
  if (firstLitX >= 0) {
    double silCentre = (silXmin + silXmax) / 2.0;
    double tol = 10.0;  // px, 16x16 tessellation facet width at this scale
    check(std::fabs(firstLitX - silCentre) <= tol,
          "terminator: the lit/unlit boundary on the centre scanline "
          "falls at the silhouette's own centre (N.L = 0), not offset "
          "toward either side");
  }
}

// ---- proof item 5: RiSides 1 vs RiSides 2 ----
void testBackfaceCulling(const std::string &gman) {
  // Sphere 1 -1 1 360 -- a full, closed sphere -- occludes itself via the
  // z-buffer regardless of RiSides, so this needs ReverseOrientation to
  // make the difference visible: with the winding flipped, RiSides 1
  // culls every face (the whole sphere reads as "backfacing"), leaving
  // nothing but background; RiSides 2 shows the sphere either way.
  const char *sidesOneRib =
      "Display \"lighting_sides1.tif\" \"file\" \"rgba\"\n"
      "Format 100 100 1\n"
      "Projection \"perspective\" \"fov\" [45]\n"
      "Translate 0 0 5\n"
      "WorldBegin\n"
      "LightSource \"ambientlight\" 1 \"intensity\" [0.5]\n"
      "ReverseOrientation\n"
      "Sides 1\n"
      "Sphere 1 -1 1 360\n"
      "WorldEnd\n";
  writeFile("lighting_sides1.rib", sidesOneRib);
  check(runGman(gman, "lighting_sides1.rib") == 0, "Sides 1 scene renders");

  Image sides1 = readTIFF("lighting_sides1.tif");
  check(sides1.ok, "Sides 1 scene: TIFF read back");

  const char *sidesTwoRib =
      "Display \"lighting_sides2.tif\" \"file\" \"rgba\"\n"
      "Format 100 100 1\n"
      "Projection \"perspective\" \"fov\" [45]\n"
      "Translate 0 0 5\n"
      "WorldBegin\n"
      "LightSource \"ambientlight\" 1 \"intensity\" [0.5]\n"
      "ReverseOrientation\n"
      "Sides 2\n"
      "Sphere 1 -1 1 360\n"
      "WorldEnd\n";
  writeFile("lighting_sides2.rib", sidesTwoRib);
  check(runGman(gman, "lighting_sides2.rib") == 0, "Sides 2 scene renders");

  Image sides2 = readTIFF("lighting_sides2.tif");
  check(sides2.ok, "Sides 2 scene: TIFF read back");

  if (!sides1.ok || !sides2.ok) {
    return;
  }

  const uint32_t bg = sides1.at(0, 0);
  auto nonBackgroundFraction = [&](const Image &img) {
    long count = 0;
    for (uint32_t y = 0; y < img.height; ++y) {
      for (uint32_t x = 0; x < img.width; ++x) {
        uint32_t p = img.at(x, y);
        if (std::abs(int(TIFFGetR(p)) - int(TIFFGetR(bg))) > 8 ||
            std::abs(int(TIFFGetG(p)) - int(TIFFGetG(bg))) > 8 ||
            std::abs(int(TIFFGetB(p)) - int(TIFFGetB(bg))) > 8) {
          ++count;
        }
      }
    }
    return (double) count / (double) (img.width * img.height);
  };

  // A solid sphere culled to nothing but its own silhouette *outline* is
  // a real, tolerated artifact at this tessellation resolution: right at
  // the grazing silhouette edge the view-vector/normal dot product is
  // close to 0, where a coarse facet's discretization can still pass the
  // reversed test by a hair. That is a one-facet-wide ring, not a solid
  // disc -- 15% comfortably separates "basically everything culled" from
  // "the sphere still rendered."
  check(nonBackgroundFraction(sides1) < 0.15,
        "Sides 1 + ReverseOrientation: the solid interior reads as "
        "backfacing and vanishes -- at most a thin silhouette-edge "
        "artifact survives, not the sphere itself");
  check(nonBackgroundFraction(sides2) > 0.15,
        "Sides 2 + ReverseOrientation: the same geometry still renders "
        "as a solid sphere (no culling at Sides 2, regardless of winding)");
}

// ---- proof item 6: the golden image ----
void testGoldenImage(const std::string &gman, const std::string &ribDir) {
  const std::string rib = ribDir + "/lights.rib";
  check(runGman(gman, rib) == 0, "lights.rib renders");

  Image actual = readTIFF("lights.tif");
  check(actual.ok, "lights.rib: TIFF read back");
  if (!actual.ok) {
    return;
  }

  Image golden = readTIFF(ribDir + "/lights_golden.tif");
  check(golden.ok, "golden image: TIFF read back");
  if (!golden.ok) {
    return;
  }

  check(actual.width == golden.width && actual.height == golden.height,
        "golden image: dimensions match");
  if (actual.width != golden.width || actual.height != golden.height) {
    return;
  }

  // Per-channel tolerance, and a small allowance for how many pixels may
  // exceed it: the 16x16 tessellation's faceted silhouette boundary can
  // shift by a pixel between platforms/compilers at identical float
  // precision, which is a tessellation-resolution artifact this project
  // already accepts (silhouette_test.cpp's own 8% tolerance), not a
  // shading regression.
  const int channelTol = 24;
  long mismatched = 0;
  const long total = (long) actual.width * (long) actual.height;
  for (uint32_t y = 0; y < actual.height; ++y) {
    for (uint32_t x = 0; x < actual.width; ++x) {
      uint32_t a = actual.at(x, y);
      uint32_t g = golden.at(x, y);
      if (std::abs(int(TIFFGetR(a)) - int(TIFFGetR(g))) > channelTol ||
          std::abs(int(TIFFGetG(a)) - int(TIFFGetG(g))) > channelTol ||
          std::abs(int(TIFFGetB(a)) - int(TIFFGetB(g))) > channelTol) {
        ++mismatched;
      }
    }
  }
  double fraction = (double) mismatched / (double) total;
  check(fraction < 0.01,
        "golden image: fewer than 1% of pixels differ from "
        "tests/rib/lights_golden.tif by more than " +
            std::to_string(channelTol) + "/255 per channel (" +
            std::to_string(mismatched) + "/" + std::to_string(total) +
            " differed)");
}

} // namespace

int main(int argc, char *argv[]) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s <gman-binary> <rib-dir>\n", argv[0]);
    return 2;
  }
  const std::string gman = argv[1];
  const std::string ribDir = argv[2];

  testTerminator(gman);
  testBackfaceCulling(gman);
  testGoldenImage(gman, ribDir);

  if (failures != 0) {
    std::printf("%d assertion(s) failed\n", failures);
    return 1;
  }
  std::printf("lighting holds\n");
  return 0;
}
