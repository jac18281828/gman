/* SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Copyright (c) 2026 John Cairns <john@2ad.com>
 */

/*
 * Review finding: no existing test rendered a genuinely asymmetric partial
 * sphere's image. transforms.rib's Sphere 0.5 -0.5 0.5 360 is a full
 * sphere (zmin = -radius, zmax = radius); this renders tests/rib/
 * partial_sphere.rib, Sphere 1 -0.5 1 360 -- the exact case the phase-3
 * prompt named for GMANSphere::getLocation's zmin/zmax fix -- and checks
 * a geometric consequence only the fixed parameterization produces.
 *
 * With Sides 1 (single-sided), GMANVSPerspective::visible keeps a face
 * whose geometric normal has a positive dot product with the (eye-at-
 * origin) view vector to its centroid -- for this sphere (radius 1,
 * centred at world z=5) that works out to "keep the face where 1 + 5*z >
 * 0," i.e. z > -0.2, regardless of which of a point's two z branches
 * (+-sqrt(radius^2-r^2)) is nearer the camera. zmin=-0.5 removes the mesh
 * entirely for z in [-1,-0.5); what survives culling is not a filled
 * disk but three concentric bands along any radius from the silhouette's
 * centre: a small lit cap at the very centre (the surviving z close to
 * the pole, zmax=1, projects to r near 0), a wide background gap (most
 * of the mid-radius mesh is either absent from the shape entirely or
 * fails the z>-0.2 cull), and a thin lit ring near the true outer edge.
 * A full sphere (zmin=-radius) at the same settings has no such gap --
 * it renders a solid filled disk, verified below as the differential
 * control.
 *
 * Revert check (verified by actually reverting, not asserted): restoring
 * GMANSphere::getLocation's original bug (phimin/phimax always -PI/2/
 * PI/2, ignoring zmin/zmax) makes Sphere 1 -0.5 1 360 sweep the same full
 * latitude range as Sphere 1 -1 1 360 -- the background gap closes, and
 * testAsymmetricPartialSphere's mid-radius "still background" assertions
 * fail.
 */

#include <sys/wait.h>

#include <tiffio.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include "check.h"

namespace {

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

bool differsFromBackground(const Image &img, uint32_t x, uint32_t y) {
  uint32_t bg = img.at(0, 0);
  uint32_t p = img.at(x, y);
  return std::abs(int(TIFFGetR(p)) - int(TIFFGetR(bg))) > 8 ||
         std::abs(int(TIFFGetG(p)) - int(TIFFGetG(bg))) > 8 ||
         std::abs(int(TIFFGetB(p)) - int(TIFFGetB(bg))) > 8;
}

void testAsymmetricPartialSphere(const std::string &gman,
                                  const std::string &ribDir) {
  const std::string rib = ribDir + "/partial_sphere.rib";
  check(runGman(gman, rib) == 0,
        "partial_sphere.rib renders (default Clipping would corrupt this "
        "shape -- see AGENTS.md's near-clip precision note)");

  Image partial = readTIFF("partial_sphere.tif");
  check(partial.ok, "partial_sphere.rib: TIFF read back");
  if (!partial.ok) {
    return;
  }

  // The sphere is centred on the camera axis (Translate 0 0 5, no lateral
  // offset), so the image centre is both the silhouette's centre and the
  // projection of the sphere's own polar axis.
  uint32_t cx = partial.width / 2, cy = partial.height / 2;

  check(differsFromBackground(partial, cx, cy),
        "partial sphere: the very centre (the surviving cap near zmax's "
        "pole) is lit, not background");

  // Mid-radius: well inside the true silhouette edge (~49px at this
  // fov/format/distance, see silhouette_test.cpp's formula) and well
  // outside the small central cap -- squarely in the background gap
  // zmin=-0.5 opens up.
  check(!differsFromBackground(partial, cx + 20, cy),
        "partial sphere: a mid-radius sample (r=20px) is background -- "
        "inside the gap the missing near cap leaves");
  check(!differsFromBackground(partial, cx + 35, cy),
        "partial sphere: a mid-radius sample (r=35px) is background -- "
        "still inside the gap");

  // Near the true outer edge, the thin surviving ring should still show.
  check(differsFromBackground(partial, cx + 49, cy),
        "partial sphere: a near-edge sample (r=49px) is lit -- the thin "
        "ring the remaining mesh leaves at the true silhouette boundary");

  // Differential control: the identical scene with a full sphere
  // (zmin=-radius) must not have this gap -- it renders as a solid
  // filled disk, exactly the shape reverting the zmin/zmax fix would
  // make the partial sphere collapse into.
  const std::string fullRib =
      "Display \"partial_sphere_full_control.tif\" \"file\" \"rgba\"\n"
      "Format 200 200 1\n"
      "Projection \"perspective\" \"fov\" [45]\n"
      "Clipping 0.5 50\n"
      "Translate 0 0 5\n"
      "WorldBegin\n"
      "LightSource \"ambientlight\" 1 \"intensity\" [0.6]\n"
      "Sides 1\n"
      "Surface \"matte\" \"Ka\" [1] \"Kd\" [0]\n"
      "Sphere 1 -1 1 360\n"
      "WorldEnd\n";
  writeFile("partial_sphere_full_control.rib", fullRib);
  check(runGman(gman, "partial_sphere_full_control.rib") == 0,
        "full-sphere control scene renders");

  Image full = readTIFF("partial_sphere_full_control.tif");
  check(full.ok, "full-sphere control: TIFF read back");
  if (!full.ok) {
    return;
  }
  check(differsFromBackground(full, cx + 20, cy) &&
            differsFromBackground(full, cx + 35, cy),
        "full-sphere control: the same mid-radius samples are lit -- a "
        "full sphere has no gap, confirming the partial sphere's gap "
        "above comes from zmin=-0.5, not from Sides 1 or this scene "
        "setup generally");
}

}  // namespace

int main(int argc, char *argv[]) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s <gman-binary> <rib-dir>\n", argv[0]);
    return 2;
  }
  const std::string gman = argv[1];
  const std::string ribDir = argv[2];

  testAsymmetricPartialSphere(gman, ribDir);

  return checkSummary("partial sphere holds");
}
