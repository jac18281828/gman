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
 * gmanpatchpolyobjectmanager.cpp's diceCountFor: n =
 * clamp(ceil(L / sqrt(ShadingRate)), 1, 16), L the longest of an
 * ear-clipped triangle's three raster-space edges, dicing into n*n faces.
 * This suite calls GMANPatchPolyObjectManager::getRSPolygon directly
 * (white-box, mirroring polygon_test.cpp's own checkTriangleCount) with a
 * three-vertex "P" -- one ear-clipped triangle, so its face count is
 * exactly n*n -- and a GMANOptions carrying a known perspective
 * projection (fov 60, never 90, so a stray hardcoded 90 fails loudly) and
 * resolution.
 *
 * Every triangle is a right isoceles triangle at a fixed camera-space
 * depth (z=10), legs along x and y, so its longest edge is always the
 * hypotenuse and the projection is exactly affine across it (constant
 * depth): raster L = (xres / (right-left)) * (1 / tan(fov/2)) * (leg *
 * sqrt(2)) / z. Each case picks leg so L lands at its own target pixel
 * count, verified against this same formula, independent of
 * diceCountFor -- never the production function that computes n.
 */

#include <cmath>
#include <string>
#include <vector>

#include "check.h"
#include "gmanattributes.h"
#include "gmandictionary.h"
#include "gmanobject.h"
#include "gmanoptions.h"
#include "gmanparameterlist.h"
#include "gmanpatchpolyobjectmanager.h"
#include "gmanprimitives.h"
#include "gmantransform.h"

namespace {

const RtFloat kFov = 60.0f; // never 90: a stray hardcoded default fails loudly
const RtInt kXres = 200;
const RtInt kYres = 200;
const RtFloat kDepth = 10.0f;

// A perspective GMANOptions at kFov, format kXres x kYres (default screen
// window, +/-1 both axes at aspect 1) unless overridden.
GMANOptions makeOptions(bool nonDefaultScreenWindow) {
  GMANOptions options;
  options.setFormat(kXres, kYres, 1);

  GMANDictionary dictionary;
  RtToken tokens[1] = {RI_FOV};
  RtFloat fovValue = kFov;
  RtPointer parms[1] = {&fovValue};
  GMANParameterList pl(dictionary, 1, tokens, parms);
  options.setProjection("perspective", pl);

  if (nonDefaultScreenWindow) {
    // Halves the default screen window (+/-1 to +/-0.5) without touching
    // the raster resolution GMANOptions::getRasterInfo derives from
    // format/FrameAspectRatio alone: this doubles the raster-space scale
    // below, so a caller that ignored ScreenWindow (fell back to the
    // default) would compute half of every raster edge this suite checks.
    options.setScreenWindow(-0.5f, 0.5f, -0.5f, 0.5f);
  }
  return options;
}

// This suite's own raster-space scale (px per world unit at kDepth),
// independent of diceCountFor: (xres / windowWidth) * (1 / tan(fov/2)) /
// depth. windowWidth is 2 at the default screen window, 1 when halved.
double rasterScale(bool nonDefaultScreenWindow) {
  const double windowWidth = nonDefaultScreenWindow ? 1.0 : 2.0;
  const double invT = 1.0 / std::tan((kFov / 2.0) * M_PI / 180.0);
  return (kXres / windowWidth) * invT / kDepth;
}

// The world-space leg length of a right isoceles triangle (legs along x,
// y, apex at the origin) whose hypotenuse projects to targetPixels at
// kDepth, under this suite's own rasterScale.
RtFloat legFor(double targetPixels, bool nonDefaultScreenWindow) {
  return (RtFloat)(targetPixels / (rasterScale(nonDefaultScreenWindow) * std::sqrt(2.0)));
}

// A right isoceles triangle (apex at object origin, legs along x and y)
// at camera-space depth z, sides p[1]z and p[2]z: (0,0,z), (leg,0,z),
// (0,leg,z), unless behindEyeCorner names one corner (0, 1 or 2) to move
// to camera-space z=-1 instead -- diceCountFor's own fallback trigger.
GMANPrimitive* runTriangle(RtFloat leg, RtFloat z, const GMANOptions& options, const GMANAttributes& attr,
                           int behindEyeCorner = -1) {
  RtFloat zs[3] = {z, z, z};
  if (behindEyeCorner >= 0) {
    zs[behindEyeCorner] = -1.0f;
  }
  std::vector<RtFloat> p = {0, 0, zs[0], leg, 0, zs[1], 0, leg, zs[2]};

  GMANDictionary dictionary;
  RtToken tokens[1] = {RI_P};
  RtPointer parms[1] = {p.data()};
  GMANParameterList pl(dictionary, 1, tokens, parms, /*vertex=*/3, /*varying=*/3, /*uniform=*/1);
  GMANTransform transform;
  GMANPatchPolyObjectManager mgr;
  GMANOptions optionsCopy = options;
  GMANAttributes attrCopy = attr;
  return mgr.getRSPolygon(3, pl, &optionsCopy, &attrCopy, &transform);
}

int countFaces(GMANObject* object) {
  if (object == nullptr || object->getBody() == nullptr) {
    return 0;
  }
  GMANSurface* surface = object->getBody()->getSurface();
  int count = 0;
  for (GMANFace* face = surface ? surface->getFace() : nullptr; face != nullptr; face = face->getNext()) {
    ++count;
  }
  return count;
}

void checkFaces(const std::string& label, GMANPrimitive* prim, int expected) {
  GMANObject* object = dynamic_cast<GMANObject*>(prim);
  check(object != nullptr, label + ": getRSPolygon returns an object");
  if (object == nullptr) {
    delete prim;
    return;
  }
  const int faces = countFaces(object);
  check(faces == expected, label + ": expected " + std::to_string(expected) + " faces, got " + std::to_string(faces));
  delete prim;
}

} // namespace

int main() {
  GMANOptions options = makeOptions(/*nonDefaultScreenWindow=*/false);
  GMANAttributes defaultAttr; // ShadingRate = 1

  const RtFloat legSmall = legFor(1.5, false); // would dice below 16 under normal fallback rules
  const RtFloat leg7_5 = legFor(7.5, false);

  // 1.5px longest edge: n = ceil(1.5 / 1) = 2, 4 faces.
  checkFaces("1.5px longest edge, n=2", runTriangle(legSmall, kDepth, options, defaultAttr), 4);

  // 7.5px longest edge: n = ceil(7.5 / 1) = 8, 64 faces.
  checkFaces("7.5px longest edge, n=8", runTriangle(leg7_5, kDepth, options, defaultAttr), 64);

  // Past 16px: capped at n=16, 256 faces.
  checkFaces("20px longest edge, capped at n=16", runTriangle(legFor(20.0, false), kDepth, options, defaultAttr), 256);

  // The same 7.5px triangle under ShadingRate 4: n = ceil(7.5 / sqrt(4)) =
  // ceil(3.75) = 4, 16 faces.
  GMANAttributes shadingRate4;
  shadingRate4.setShadingRate(4.0f);
  checkFaces("7.5px longest edge, ShadingRate 4, n=4", runTriangle(leg7_5, kDepth, options, shadingRate4), 16);

  // The 1.5px triangle, one corner behind the eye: the perspective
  // position fallback, 256 faces despite what its raster size alone would
  // dice to.
  checkFaces("small triangle, corner behind the eye, n=16",
             runTriangle(legSmall, kDepth, options, defaultAttr, /*behindEyeCorner=*/2), 256);

  // The 1.5px triangle under ShadingRate 0 and -1: the non-positive
  // ShadingRate fallback, 256 faces each.
  GMANAttributes shadingRate0;
  shadingRate0.setShadingRate(0.0f);
  checkFaces("small triangle, ShadingRate 0, n=16", runTriangle(legSmall, kDepth, options, shadingRate0), 256);

  GMANAttributes shadingRateNeg1;
  shadingRateNeg1.setShadingRate(-1.0f);
  checkFaces("small triangle, ShadingRate -1, n=16", runTriangle(legSmall, kDepth, options, shadingRateNeg1), 256);

  // A non-default ScreenWindow, halving the window without touching the
  // raster resolution GMANOptions derives from format alone: doubles this
  // suite's own raster scale, so a triangle whose default-window
  // hypotenuse would be 20px (comfortably capped either way) proves the
  // window is actually read, not a fov/format-only shortcut, by landing
  // at exactly the ceil(10)=10-per-edge count instead of staying at the
  // cap.
  GMANOptions windowedOptions = makeOptions(/*nonDefaultScreenWindow=*/true);
  const RtFloat legWindowed = legFor(10.0, true);
  checkFaces("10px longest edge under a halved ScreenWindow, n=10",
             runTriangle(legWindowed, kDepth, windowedOptions, defaultAttr), 100);

  // A finite longest edge past INT_MAX pixels (a huge but legitimate
  // world-space triangle, not a degenerate or infinite one): the clamp to
  // [1, 16] has to happen before the ceil()'d ratio ever becomes an
  // RtInt, or converting an out-of-range double to int is undefined
  // behaviour (caught under UBSan; this platform's own conversion
  // happens to saturate, so the count alone does not falsify the old
  // code -- the sanitizer does).
  const RtFloat legHuge = legFor(3.0e9, false);
  checkFaces("longest edge past INT_MAX pixels, still finite, capped at n=16",
             runTriangle(legHuge, kDepth, options, defaultAttr), 256);

  return checkSummary("polygondicecount holds");
}
