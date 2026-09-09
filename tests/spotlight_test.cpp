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
 * tests/rib/spotlight.rib: a spotlight on a flat disk's own axis, so
 * radius from the disk's centre maps directly to the cone's angle. Proves
 * three of the RISpec's own claims about GMAN_LIGHT_SPOT: a point inside
 * the cone is lit, a point past coneangle is not (exactly, not just
 * dimmer), and the boundary between them is a taper spanning several
 * pixels, not a single-pixel step -- the case a missing conedeltaangle
 * clamp gets wrong.
 *
 * tests/rib/spotlight_beam.rib: the same geometry at a lower intensity,
 * isolating beamdistribution's own falloff. spotlight.rib's intensity is
 * tuned for the taper assertion above and saturates almost the whole
 * taper-free zone, leaving no range to measure beamdistribution's shape
 * in that fixture; this one trades the taper band away to keep that zone
 * unsaturated instead.
 *
 * Revert check (verified by actually reverting, not asserted): removing
 * the "spotlight" branch in GMANRenderManImpl::RiLightSourceV makes the
 * light fall through to "Unknown light shader", RiLightSourceV returns
 * handle 0, and no light reaches the scene at all -- testInsideLit's own
 * brightness assertion goes red because the disk renders solid black.
 */

#include <sys/wait.h>

#include <tiffio.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "check.h"

namespace {

int runGman(const std::string &gman, const std::string &rib) {
  const std::string command = "\"" + gman + "\" \"" + rib + "\" >/dev/null 2>&1";
  int status = std::system(command.c_str());
  return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
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

// The disk is matte, lit only by the spotlight (no ambient term), so its
// channels track the light's own greyscale falloff directly.
int redAt(const Image &img, uint32_t x, uint32_t y) {
  return (int) TIFFGetR(img.at(x, y));
}

// The disk's silhouette on row y: background outside it, the light's own
// falloff inside. Both fixtures share this camera and disk, so both locate
// their silhouette the same way.
bool findSilhouette(const Image &img, uint32_t y, uint32_t &xmin, uint32_t &xmax) {
  const uint32_t bg = img.at(0, y);
  auto differsFromBackground = [&](uint32_t x) {
    uint32_t p = img.at(x, y);
    return std::abs(int(TIFFGetR(p)) - int(TIFFGetR(bg))) > 8 ||
           std::abs(int(TIFFGetG(p)) - int(TIFFGetG(bg))) > 8 ||
           std::abs(int(TIFFGetB(p)) - int(TIFFGetB(bg))) > 8;
  };
  xmin = img.width;
  xmax = 0;
  bool found = false;
  for (uint32_t x = 0; x < img.width; ++x) {
    if (differsFromBackground(x)) {
      found = true;
      xmin = std::min(xmin, x);
      xmax = std::max(xmax, x);
    }
  }
  return found;
}

} // namespace

int main(int argc, char *argv[]) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s <gman-binary> <rib-dir>\n", argv[0]);
    return 2;
  }
  const std::string gman = argv[1];
  const std::string rib = std::string(argv[2]) + "/spotlight.rib";

  check(runGman(gman, rib) == 0, "spotlight scene renders");

  Image img = readTIFF("spotlight.tif");
  check(img.ok, "spotlight scene: TIFF read back");
  if (!img.ok) {
    return checkSummary("spotlight holds");
  }

  const uint32_t midY = img.height / 2;
  uint32_t silXmin = 0, silXmax = 0;
  const bool silFound = findSilhouette(img, midY, silXmin, silXmax);
  check(silFound, "spotlight: the disk's silhouette was found");
  if (!silFound) {
    return checkSummary("spotlight holds");
  }

  const uint32_t silCentreX = (silXmin + silXmax) / 2;

  // Geometry shared by the taper check below, mirroring tests/rib/
  // spotlight.rib's own literals: the light's axial standoff from the
  // disk (both share the "Translate 0 0 5" ahead of WorldBegin) and the
  // cone's two angles. The silhouette's own edge is the disk's radius, at
  // thetaMax = atan(diskRadius / standoff); pixelsPerRadian converts that
  // measured span into an x-offset-per-radian for the taper's own span.
  const double diskStandoff = 5.0;
  const double diskRadius = 3.0;
  const double coneDeltaAngle = 0.08;
  const double thetaMax = std::atan(diskRadius / diskStandoff);
  const double pixelsPerRadian = double(silXmax - silCentreX) / thetaMax;

  // ---- inside the cone: lit ----
  const int centreBrightness = redAt(img, silCentreX, midY);
  check(centreBrightness > 200,
        "spotlight: the disk's centre, well inside the cone, is lit "
        "(R=" + std::to_string(centreBrightness) + ")");

  // ---- outside the cone: exactly unlit, not just dim ----
  // A few pixels in from the silhouette's own edge, past the pixel
  // filter's antialiased rim, but still well outside the cone's small
  // half-angle at this disk's radius.
  const uint32_t margin = 4;
  check(silXmax - silXmin > 2 * margin,
        "spotlight: the silhouette is wide enough to sample past its rim");
  const int outsideNear = redAt(img, silXmin + margin, midY);
  const int outsideFar = redAt(img, silXmax - margin, midY);
  check(outsideNear <= 2 && outsideFar <= 2,
        "spotlight: past the cone, the disk reads exactly unlit on both "
        "sides (R=" + std::to_string(outsideNear) + "," +
        std::to_string(outsideFar) + ")");

  // ---- the taper: a band, not a step ----
  // Walking outward from the centre, count consecutive samples strictly
  // between "unlit" and "fully lit" -- a hard cutoff with no
  // conedeltaangle taper would skip straight from >200 to <=2 in one
  // pixel; a real taper spans several.
  auto taperWidth = [&](int step) -> int {
    int width = 0;
    for (uint32_t x = silCentreX; x >= silXmin + 1 && x + 1 <= silXmax; x += step) {
      int v = redAt(img, x, midY);
      if (v > 2 && v <= 200) {
        ++width;
      } else if (v <= 2) {
        break;
      }
    }
    return width;
  };
  const int rightTaper = taperWidth(1);
  const int leftTaper = taperWidth(-1);

  // A hard cutoff (conedeltaangle ignored) still blurs across a pixel or
  // two of antialiasing, but that blur does not scale with
  // conedeltaangle; a real taper spans coneDeltaAngle * pixelsPerRadian.
  // Require most (70%) of that predicted span, not all of it -- the
  // model above is a straight-line approximation of a perspective
  // projection -- while still well clear of antialiasing-only blur.
  const double expectedTaperPx = coneDeltaAngle * pixelsPerRadian;
  const double minTaperPx = expectedTaperPx * 0.7;
  check(rightTaper >= minTaperPx && leftTaper >= minTaperPx,
        "spotlight: the cone's edge tapers over several pixels on both "
        "sides, not a single-pixel step (right=" +
        std::to_string(rightTaper) + ", left=" + std::to_string(leftTaper) +
        ", expected >= " + std::to_string(minTaperPx) + ")");

  // ---- the beam term: isolate beamdistribution from the taper and
  // inverse-square, using tests/rib/spotlight_beam.rib's own lower
  // intensity ----
  const std::string beamRib = std::string(argv[2]) + "/spotlight_beam.rib";
  check(runGman(gman, beamRib) == 0, "spotlight_beam scene renders");

  Image beamImg = readTIFF("spotlight_beam.tif");
  check(beamImg.ok, "spotlight_beam scene: TIFF read back");
  if (!beamImg.ok) {
    return checkSummary("spotlight holds");
  }

  const uint32_t beamMidY = beamImg.height / 2;
  uint32_t beamXmin = 0, beamXmax = 0;
  const bool beamSilFound = findSilhouette(beamImg, beamMidY, beamXmin, beamXmax);
  check(beamSilFound, "spotlight_beam: the disk's silhouette was found");
  if (!beamSilFound) {
    return checkSummary("spotlight holds");
  }
  const double beamCentreX = (beamXmin + beamXmax) / 2.0;

  // Same disk and cone as spotlight.rib. Unlike the taper check above, the
  // mapping from x to theta here must be exact rather than a straight-line
  // approximation: the disk is fronto-parallel, so a perspective camera
  // maps its plane linearly to x, and x is linear in tan(theta) -- not in
  // theta -- because r = standoff * tan(theta). x(theta) below inverts
  // that relation using the silhouette's own edge, at r == diskRadius, as
  // the one calibration point.
  const double beamConeAngle = 0.3;
  const double beamConeDeltaAngle = 0.08;
  const double taperFreeTheta = beamConeAngle - beamConeDeltaAngle;
  const double pixelsPerUnitR = (beamXmax - beamCentreX) / diskRadius;
  auto xOfTheta = [&](double theta) -> uint32_t {
    double r = diskStandoff * std::tan(theta);
    return (uint32_t) std::lround(beamCentreX + r * pixelsPerUnitR);
  };

  // Two off-axis points, both short of the taper band (theta <
  // coneangle - conedeltaangle, where the taper's own SmoothStep is
  // exactly 1): a quarter and nine-tenths of the way across the
  // taper-free zone. Their brightness ratio isolates
  // cos(theta)^beamdistribution from the inverse-square and matte N.L
  // terms, both of which this fixture's geometry holds fixed in the same
  // proportion as spotlight.rib's own taper check above.
  const uint32_t beamX1 = xOfTheta(0.25 * taperFreeTheta);
  const uint32_t beamX2 = xOfTheta(0.90 * taperFreeTheta);
  check(beamX1 < beamX2 && beamX2 < beamXmax,
        "spotlight_beam: both sample points land inside the taper-free "
        "zone (x1=" + std::to_string(beamX1) + ", x2=" +
        std::to_string(beamX2) + ", silXmax=" + std::to_string(beamXmax) +
        ")");

  const int beamBrightness1 = redAt(beamImg, beamX1, beamMidY);
  const int beamBrightness2 = redAt(beamImg, beamX2, beamMidY);
  check(beamBrightness1 > 50 && beamBrightness1 < 250,
        "spotlight_beam: the near sample point is lit and unsaturated "
        "(R=" + std::to_string(beamBrightness1) + ")");
  check(beamBrightness2 > 50 && beamBrightness2 < 250,
        "spotlight_beam: the far sample point is lit and unsaturated "
        "(R=" + std::to_string(beamBrightness2) + ")");

  // beamdistribution == 6 predicts a ratio near 0.85; the atten == 1.0
  // mutation (beamdistribution dropped from the falloff) predicts a
  // ratio near 0.945 from inverse-square and N.L alone. 0.90 sits with
  // real margin on both sides of that gap.
  const double beamRatio =
    double(beamBrightness2) / double(beamBrightness1);
  check(beamRatio < 0.90,
        "spotlight_beam: beamdistribution attenuates the farther "
        "taper-free point relative to the nearer one (ratio=" +
        std::to_string(beamRatio) + ")");

  return checkSummary("spotlight holds");
}
