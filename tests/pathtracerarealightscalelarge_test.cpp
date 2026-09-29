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
 * The sphere-over-floor scene, every length scaled by 1000 about the eye,
 * with an opaque shell just larger than the emitter enclosing it: every
 * segment from the emitter's own surface to the floor crosses that shell,
 * so the floor must read exactly black wherever it is measured. A margin
 * that outgrows the scene's own scale reads the emitter as unblocked
 * instead.
 */

#include <cmath>
#include <vector>

#include "check.h"
#include "gmanattributes.h"
#include "gmanframebuffer.h"
#include "gmanlightsourcemgr.h"
#include "gmanmatrix4.h"
#include "gmanoptions.h"
#include "gmanparameterlist.h"
#include "gmanpathtracerenderer.h"
#include "gmanpoint.h"
#include "gmanraypolygon.h"
#include "gmanraysphere.h"
#include "gmantransform.h"
#include "gmanvector.h"
#include "gmanvsperspective.h"
#include "pathtracerarealightscene.h"
#include "pathtracerscene.h"
#include "ri.h"
#include "samplingstats.h"

namespace {

constexpr RtFloat kScale = 1000.0f;
constexpr RtFloat kSphereRadius = 1.0f;
constexpr RtFloat kSphereCentreX = 10.0f;
constexpr RtFloat kSphereCentreY = -2.0f;
constexpr RtFloat kSphereCentreZ = 9.0f;
// The occluder's own radius, just larger than the emitter's: comfortably
// under the margin (at least 2, at scale 1) every camera ray toward the
// floor already keeps from the emitter's own centre, so it never crosses a
// camera ray to a measured pixel.
constexpr RtFloat kOccluderRadius = kSphereRadius * 1.2f;
constexpr RtInt kFloorRes = 81;
constexpr RtInt kFloorSamples = 64;
constexpr std::size_t kMinMeasuredPixels = 400;
constexpr RtFloat kNear = 0.5f;
constexpr RtFloat kFar = 50.0f;

GMANRayPolygon* buildScaledFloor(RtFloat scale) {
  std::vector<GMANPoint> const verts = {GMANPoint(kFloorXMin * scale, kFloorY * scale, kFloorZMin * scale),
                                        GMANPoint(kFloorXMin * scale, kFloorY * scale, kFloorZMax * scale),
                                        GMANPoint(kFloorXMax * scale, kFloorY * scale, kFloorZMax * scale),
                                        GMANPoint(kFloorXMax * scale, kFloorY * scale, kFloorZMin * scale)};
  return new GMANRayPolygon(verts, GMANParameterList());
}

// A measured pixel whose own camera-ray corners also clear the occluder's
// own bounding region, at the same margin tests/pathtracerarealighthit_test.cpp's
// own selection queries use: a boundary pixel is excluded rather than
// contaminating the measured set with a partial value.
bool cornersClearOccluder(gman::VSPerspective& viewingSys, int px, int py, RtFloat cx, RtFloat cy, RtFloat cz,
                          RtFloat radius) {
  for (int dy = 0; dy <= 1; ++dy) {
    for (int dx = 0; dx <= 1; ++dx) {
      GMANRay const corner = viewingSys.cameraRay((RtFloat)(px + dx), (RtFloat)(py + dy));
      GMANVector const d = corner.getDirection();
      double const ocx = -(double)cx, ocy = -(double)cy, ocz = -(double)cz;
      double const dx3 = (double)d.getX(), dy3 = (double)d.getY(), dz3 = (double)d.getZ();
      double const b = 2.0 * (dx3 * ocx + dy3 * ocy + dz3 * ocz);
      double const c = ocx * ocx + ocy * ocy + ocz * ocz - (double)radius * (double)radius;
      double const disc = b * b - 4.0 * c;
      if (disc >= 0.0) {
        double const sq = std::sqrt(disc);
        double const t0 = (-b - sq) / 2.0;
        double const t1 = (-b + sq) / 2.0;
        if (t0 > 1e-6 || t1 > 1e-6) {
          return false;
        }
      }
    }
  }
  return true;
}

void testScaledLargeOccludedFloor() {
  GMANOptions options;
  options.setFormat(kFloorRes, kFloorRes, 1.0f);
  options.setPixelSamples(1.0f, 1.0f);
  options.setPixelFilter(RiBoxFilter, 1.0f, 1.0f);
  options.setPathtracerSamples(kFloorSamples);
  options.setBackground(GMANColor(0.0f, 0.0f, 0.0f));

  GMANMatrix4 const identity;
  gman::VSPerspective viewingSys(kFloorRes, kFloorRes, squareScreenWindow(), identity, 90.0f, kNear * kScale,
                                 kFar * kScale);

  GMANPathtraceRenderer renderer;
  GMANRayPolygon* floor = buildScaledFloor(kScale);
  gman::Appearance floorAppearance;
  floorAppearance.shader = loadShader("matte", matteParams(1.0f));
  floorAppearance.Cs = GMANColor(0.5f, 0.5f, 0.5f);
  floorAppearance.Os = GMANColor(1.0f, 1.0f, 1.0f);
  floor->setAppearance(floorAppearance);
  renderer.getWorldManager()->add(floor);

  GMANMatrix4 place;
  place.trans(kSphereCentreX * kScale, kSphereCentreY * kScale, kSphereCentreZ * kScale);
  GMANTransform const transform = makeTransform(place);
  RtFloat const scaledRadius = kSphereRadius * kScale;
  const auto sphere =
      new GMANRaySphere(scaledRadius, -scaledRadius, scaledRadius, 360.0f, GMANParameterList(), transform);
  GMANLight const areaLight(GMAN_LIGHT_AREA, GMANColor(kAreaLe, kAreaLe, kAreaLe), GMANPoint(), GMANVector());
  gman::Appearance sphereAppearance;
  sphereAppearance.areaLight = &areaLight;
  sphereAppearance.Os = GMANColor(1.0f, 1.0f, 1.0f);
  sphere->setAppearance(sphereAppearance);
  renderer.getWorldManager()->add(sphere);

  // Concentric with the emitter and just larger: every segment from the
  // emitter's own surface to any point outside this shell crosses it.
  RtFloat const occluderRadius = kOccluderRadius * kScale;
  const auto occluder =
      new GMANRaySphere(occluderRadius, -occluderRadius, occluderRadius, 360.0f, GMANParameterList(), transform);
  gman::Appearance occluderAppearance;
  occluderAppearance.Cs = GMANColor(0.0f, 0.0f, 0.0f);
  occluderAppearance.Os = GMANColor(1.0f, 1.0f, 1.0f);
  occluder->setAppearance(occluderAppearance);
  renderer.getWorldManager()->add(occluder);

  GMANFrameBuffer frameBuffer(kFloorRes, kFloorRes, options.getBackground());
  GMANAttributes const attr;
  renderer.render(&frameBuffer, &viewingSys, options, attr);
  check(renderer.droppedPathCount() == 0, "scaled large occluded floor: droppedPathCount() is 0");

  std::size_t measuredCount = 0;
  bool allBlack = true;
  for (int py = 0; py < kFloorRes; ++py) {
    for (int px = 0; px < kFloorRes; ++px) {
      if (!pixelMeasured(viewingSys, px, py) ||
          !cornersClearOccluder(viewingSys, px, py, kSphereCentreX, kSphereCentreY, kSphereCentreZ, kOccluderRadius)) {
        continue;
      }
      ++measuredCount;
      GMANColor const actual = frameBuffer.getPixel(px, py);
      if (actual.getRed() != 0.0f || actual.getGreen() != 0.0f || actual.getBlue() != 0.0f) {
        allBlack = false;
      }
    }
  }
  check(measuredCount >= kMinMeasuredPixels, "scaled large occluded floor: at least 400 measured pixels");
  check(allBlack, "scaled large occluded floor: every measured pixel reads exactly black");
}

} // namespace

int main() {
  testScaledLargeOccludedFloor();

  return checkSummary("a sphere light over the floor, every length scaled by 1000 about the eye and shelled just "
                      "outside the emitter, reads exactly black on the floor");
}
