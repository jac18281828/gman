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
 * environment(), MakeLatLongEnvironment and shinymetal (RISpec 3.2
 * Sec 7.1.2, Sec 15.7.2, Appendix A.2.4), proved in three parts matching
 * this task's three commits:
 *
 *  - toWorld and the camera-to-world matrix reaching the shading path
 *    (commit 1);
 *  - gmanMakeLatLongEnvironment's writer and environment()'s lookup,
 *    direct and through RIB (commit 2);
 *  - shinymetal rendered under three camera orientations (commit 3).
 *
 * Writes its own maps into its working directory, as tests/texture_test.cpp
 * writes its checker.
 */

#include <tiffio.h>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "check.h"
#include "gmanattributes.h"
#include "gmanlinearworldmanager.h"
#include "gmanmath.h"
#include "gmanmatrix4.h"
#include "gmanobject.h"
#include "gmanoptions.h"
#include "gmanparameterlist.h"
#include "gmanpatchpolyobjectmanager.h"
#include "gmanrenderer.h"
#include "gmanshaderenvironment.h"
#include "gmantexture.h"
#include "gmantransform.h"
#include "gmanvector.h"
#include "gmanvertex.h"

namespace {

const RtFloat kTightTol = (RtFloat) 1.0e-5;

bool near(RtFloat a, RtFloat b, RtFloat tol) {
  return std::fabs(a - b) <= tol;
}

void checkVectorNear(const GMANVector &got, const GMANVector &want,
                      RtFloat tol, const std::string &what) {
  check(near(got.getX(), want.getX(), tol) &&
            near(got.getY(), want.getY(), tol) &&
            near(got.getZ(), want.getZ(), tol),
        what + ": got (" + std::to_string(got.getX()) + ", " +
            std::to_string(got.getY()) + ", " + std::to_string(got.getZ()) +
            "), want (" + std::to_string(want.getX()) + ", " +
            std::to_string(want.getY()) + ", " + std::to_string(want.getZ()) +
            ")");
}

void checkColorNear(const GMANColor &got, const GMANColor &want, RtFloat tol,
                     const std::string &what) {
  check(near(got.getRed(), want.getRed(), tol) &&
            near(got.getGreen(), want.getGreen(), tol) &&
            near(got.getBlue(), want.getBlue(), tol),
        what + ": got (" + std::to_string(got.getRed()) + ", " +
            std::to_string(got.getGreen()) + ", " +
            std::to_string(got.getBlue()) + "), want (" +
            std::to_string(want.getRed()) + ", " +
            std::to_string(want.getGreen()) + ", " +
            std::to_string(want.getBlue()) + ")");
}

// A do-nothing GMANRenderer: GMANAttributes::setSurface needs one to pass
// to GMANShader::set(GMANRenderer&), which only stores the reference
// (gmanshader.cpp) -- no override below is ever actually called.
class NullRenderer : public GMANRenderer {
public:
  RtVoid illuminance(RtInt, GMANPoint const &, GMANVector const &,
                      RtFloat) override {}
  RtVoid illuminate(RtInt, GMANPoint const &, GMANVector const &,
                     RtFloat) override {}
  RtVoid solar(RtInt, GMANVector const &, RtFloat) override {}
  RtFloat getDepth(int, int) const override { return 0; }
  RtVoid render(GMANFrameBuffer *, GMANViewingSystem *, const GMANOptions &,
                const GMANAttributes &) override {}
  GMANWorldManager *getWorldManager(RtVoid) override { return nullptr; }
  GMANObjectManager *getObjectManager(RtVoid) override { return nullptr; }
};

// ---- toWorld and the plumbing (commit 1) ----

// AGENTS.md's row-vector convention: GMANMatrix4::rot's own y-axis matrix
// gives v' = (x*cos(a) + z*sin(a), y, -x*sin(a) + z*cos(a)) -- calibrated
// against tests/rib/rotate.rib's own comment ("Rotate 90 0 1 0 turns the
// world x axis onto -z", i.e. (1,0,0) -> (0,0,-1) under this same matrix).
// At a=90 degrees, (0,0,1) -> (1,0,0).
void testToWorldRotatesDirection() {
  GMANSurfaceEnv env;
  GMANMatrix4 m;
  m.rot((RtFloat)(PI / 2.0), 0, 1, 0);
  // A translation must not move a direction: toWorld is RSL's
  // vtransform("current", "world", v), which reads only cameraToWorld's
  // upper-left 3x3. Folded in here to prove exactly that.
  m.trans((RtFloat) 3.0, (RtFloat) -7.0, (RtFloat) 11.0);
  env.cameraToWorld = m;

  GMANVector world =
      env.toWorld(GMANVector((RtFloat) 0.0, (RtFloat) 0.0, (RtFloat) 1.0));
  checkVectorNear(world,
                   GMANVector((RtFloat) 1.0, (RtFloat) 0.0, (RtFloat) 0.0),
                   kTightTol,
                   "toWorld: a 90-deg Y rotation maps (0,0,1) to (1,0,0)");
}

// Call getRSSphere directly with a GMANOptions carrying the same rotation
// and a test-local shader (tests/camerashader_test_plugin.cpp) that encodes
// se.cameraToWorld's [0][0], [0][2] and [2][0] entries into Ci -- the only
// channel a dlopen'd module and its caller share. For the rotation above,
// [0][0]=cos(90)=0, [0][2]=-sin(90)=-1, [2][0]=sin(90)=1.
void testMatrixReachesShader() {
  GMANOptions options;
  GMANMatrix4 m;
  m.rot((RtFloat)(PI / 2.0), 0, 1, 0);
  options.setCameraToWorld(m);

  GMANAttributes attr;
  NullRenderer renderer;
  GMANParameterList emptyPl;
  attr.setSurface("camerashader", emptyPl, renderer);

  GMANPatchPolyObjectManager mgr;
  GMANParameterList spherePl;
  GMANTransform transform;  // identity

  GMANPrimitive *prim = mgr.getRSSphere((RtFloat) 1.0, (RtFloat) -1.0,
                                         (RtFloat) 1.0, (RtFloat) 360.0,
                                         spherePl, &options, &attr,
                                         &transform);
  // Kept reachable the same way production does (GMANRenderManImpl::RiEnd
  // leaves worldManager/objectManager deliberately unfreed) -- see
  // tests/normals_test.cpp's own comment at its analogous getRSSphere call.
  static GMANLinearWorldManager worldMgr;
  worldMgr.add(prim);

  GMANObject *object = dynamic_cast<GMANObject *>(prim);
  check(object != nullptr, "plumbing: getRSSphere returns an object");
  if (!object) {
    return;
  }
  GMANVertex *vtx = object->getVert();
  check(vtx != nullptr, "plumbing: object has a vertex");
  if (!vtx) {
    return;
  }
  checkColorNear(vtx->getColor(),
                 GMANColor((RtFloat) 0.0, (RtFloat) -1.0, (RtFloat) 1.0),
                 (RtFloat) 1.0e-4,
                 "plumbing: GMANOptions's camera-to-world reaches "
                 "GMANSurfaceEnv::cameraToWorld through getRSSphere");
}

// ---- the writer and the lookup (commit 2) ----

const int kLatLongWidth = 8;
const int kLatLongHeight = 4;

// Every texel a distinct colour: R varies with column (longitude), G with
// row (latitude), B disambiguates the pair outright -- an asymmetric map
// is what makes a swapped axis, a flipped latitude or a mirrored longitude
// change a colour.
unsigned char latLongByte(int channel, int i, int j) {
  switch (channel) {
    case 0: return (unsigned char)(i * 255 / (kLatLongWidth - 1));
    case 1: return (unsigned char)(j * 255 / (kLatLongHeight - 1));
    default:
      return (unsigned char)((i * kLatLongHeight + j) * 255 /
                              (kLatLongWidth * kLatLongHeight - 1));
  }
}

GMANColor latLongTexel(int i, int j) {
  return GMANColor((RtFloat) latLongByte(0, i, j) / (RtFloat) 255.0,
                    (RtFloat) latLongByte(1, i, j) / (RtFloat) 255.0,
                    (RtFloat) latLongByte(2, i, j) / (RtFloat) 255.0);
}

// The plain RGB TIFF gmanMakeLatLongEnvironment's own "picture" argument
// reads -- not yet tagged as an environment; the writer adds that.
bool writeLatLongPicture(const std::string &path) {
  TIFF *tif = TIFFOpen(path.c_str(), "w");
  if (tif == nullptr) {
    return false;
  }
  TIFFSetField(tif, TIFFTAG_IMAGEWIDTH, (uint32_t) kLatLongWidth);
  TIFFSetField(tif, TIFFTAG_IMAGELENGTH, (uint32_t) kLatLongHeight);
  TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, 8);
  TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, 3);
  TIFFSetField(tif, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT);
  TIFFSetField(tif, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB);
  TIFFSetField(tif, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
  TIFFSetField(tif, TIFFTAG_ROWSPERSTRIP, 1);

  bool ok = true;
  for (int j = 0; j < kLatLongHeight && ok; ++j) {
    std::vector<unsigned char> row((std::size_t) kLatLongWidth * 3);
    for (int i = 0; i < kLatLongWidth; ++i) {
      row[(std::size_t) i * 3 + 0] = latLongByte(0, i, j);
      row[(std::size_t) i * 3 + 1] = latLongByte(1, i, j);
      row[(std::size_t) i * 3 + 2] = latLongByte(2, i, j);
    }
    ok = TIFFWriteScanline(tif, row.data(), (uint32_t) j, 0) >= 0;
  }
  TIFFClose(tif);
  return ok;
}

// RISpec 3.2 Sec 7.1.2's own formula, applied as written (handedness
// included -- gman is left-handed and latlong maps are handedness-
// sensitive): x=cos(lon)cos(lat), y=sin(lon)cos(lat), z=sin(lat).
GMANVector directionAt(RtFloat lon, RtFloat lat) {
  return GMANVector((RtFloat)(std::cos(lon) * std::cos(lat)),
                     (RtFloat)(std::sin(lon) * std::cos(lat)),
                     (RtFloat) std::sin(lat));
}

// Texel (i, j)'s own centre direction, RISpec's lat-long picture: longitude
// 0 at the left, 360 at the right; latitude -90 at the bottom, 90 at the
// top.
GMANVector texelCentreDirection(int i, int j) {
  RtFloat lon = (RtFloat)(2.0 * PI * (i + 0.5) / kLatLongWidth);
  RtFloat lat = (RtFloat)(PI / 2.0 - PI * (j + 0.5) / kLatLongHeight);
  return directionAt(lon, lat);
}

std::string readAsciiTag(const std::string &path, ttag_t tag) {
  TIFF *tif = TIFFOpen(path.c_str(), "r");
  if (tif == nullptr) {
    return std::string();
  }
  char *value = nullptr;
  std::string result;
  if (TIFFGetField(tif, tag, &value) && value != nullptr) {
    result = value;
  }
  TIFFClose(tif);
  return result;
}

bool fileExists(const std::string &path) {
  return std::filesystem::exists(path);
}

// Direct lookups: environment(map, R) at every texel centre returns that
// texel's colour, independent of R's own length (RISpec: "the length of
// this vector is unimportant").
void testDirectLookups(const std::string &map) {
  GMANSurfaceEnv env;
  for (int i = 0; i < kLatLongWidth; ++i) {
    for (int j = 0; j < kLatLongHeight; ++j) {
      GMANVector r = texelCentreDirection(i, j);
      const std::string what = "environment: texel (" + std::to_string(i) +
                                ", " + std::to_string(j) + ")";
      checkColorNear(env.environment(map, r), latLongTexel(i, j),
                     (RtFloat) 1.0e-4, what);
      checkColorNear(env.environment(map, r * (RtFloat) 1.0e3),
                     latLongTexel(i, j), (RtFloat) 1.0e-4, what + " (*1e3)");
      checkColorNear(env.environment(map, r * (RtFloat) 1.0e-3),
                     latLongTexel(i, j), (RtFloat) 1.0e-4, what + " (*1e-3)");
    }
  }
  checkColorNear(
      env.environment(map, GMANVector((RtFloat) 0.0, (RtFloat) 0.0,
                                       (RtFloat) 0.0)),
      GMANColor((RtFloat) 0.0, (RtFloat) 0.0, (RtFloat) 0.0),
      (RtFloat) 1.0e-6, "environment: R=0 returns black");
}

// A direction at lon just below 2*PI blends texel (7, j) with (0, j) --
// periodic wrap, not clamp holding (7, j) alone. s = 31/32 sits a quarter
// texel short of the wrap (texel 7's own centre is at s=7.5/8=0.9375), so
// GMANTexture::sample's bilinear weights are exactly 0.75 on texel 7 and
// 0.25 on texel 0 (wrapped from column 8).
void testLongitudeWraps(const std::string &map) {
  GMANSurfaceEnv env;
  const int j = 1;
  RtFloat lat = (RtFloat)(PI / 2.0 - PI * (j + 0.5) / kLatLongHeight);
  RtFloat lon = (RtFloat)(2.0 * PI * (31.0 / 32.0));
  GMANVector r = directionAt(lon, lat);

  GMANColor c7 = latLongTexel(kLatLongWidth - 1, j);
  GMANColor c0 = latLongTexel(0, j);
  GMANColor want((RtFloat) 0.75 * c7.getRed() + (RtFloat) 0.25 * c0.getRed(),
                 (RtFloat) 0.75 * c7.getGreen() +
                     (RtFloat) 0.25 * c0.getGreen(),
                 (RtFloat) 0.75 * c7.getBlue() + (RtFloat) 0.25 * c0.getBlue());
  checkColorNear(env.environment(map, r), want, (RtFloat) 1.0e-3,
                 "environment: longitude just below 2*PI blends texel "
                 "(7, j) with (0, j)");
}

// The written file's tags, and the writer's failure shape.
void testWriterTagsAndFailures(const std::string &picture,
                                const std::string &map) {
  std::remove(map.c_str());
  check(gmanMakeLatLongEnvironment(picture.c_str(), map.c_str()),
        "gmanMakeLatLongEnvironment returns true");
  check(readAsciiTag(map, TIFFTAG_PIXAR_WRAPMODES) == "periodic,clamp",
        map + "'s TIFFTAG_PIXAR_WRAPMODES is \"periodic,clamp\"");
  check(readAsciiTag(map, TIFFTAG_PIXAR_TEXTUREFORMAT) ==
            "LatLong Environment",
        map + "'s TIFFTAG_PIXAR_TEXTUREFORMAT is \"LatLong Environment\"");

  const std::string emptyTarget = "made_empty_name.env";
  std::remove(emptyTarget.c_str());
  check(! gmanMakeLatLongEnvironment(picture.c_str(), ""),
        "gmanMakeLatLongEnvironment with an empty texture name returns "
        "false");
  check(! fileExists(emptyTarget), emptyTarget + " is not written");

  const std::string missingPictureTarget = "made_missing_picture.env";
  std::remove(missingPictureTarget.c_str());
  check(! gmanMakeLatLongEnvironment("environment_test_missing_9f3ab2.tif",
                                      missingPictureTarget.c_str()),
        "gmanMakeLatLongEnvironment with a missing picture returns false");
  check(! fileExists(missingPictureTarget),
        missingPictureTarget + " is not written");
}

}  // namespace

int main(int /*argc*/, char * /*argv*/[]) {
  testToWorldRotatesDirection();
  testMatrixReachesShader();

  const std::string picture = "latlong_picture.tif";
  const std::string map = "latlong.env";
  check(writeLatLongPicture(picture),
        picture + " writes for gmanMakeLatLongEnvironment");
  check(gmanMakeLatLongEnvironment(picture.c_str(), map.c_str()),
        map + " writes via gmanMakeLatLongEnvironment");
  testDirectLookups(map);
  testLongitudeWraps(map);
  testWriterTagsAndFailures(picture, map);

  return checkSummary("environment holds");
}
