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
 * The sphere-over-floor scene, every length scaled by 0.01 about the eye: a
 * scale-independent proof that the shadow walk's own margin off the
 * emitter tracks the scene's own scale rather than one fixed value.
 */

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

constexpr RtFloat kScale = 0.01f;
constexpr RtFloat kSphereRadius = 1.0f;
constexpr RtFloat kSphereCentreX = 10.0f;
constexpr RtFloat kSphereCentreY = -2.0f;
constexpr RtFloat kSphereCentreZ = 9.0f;
constexpr RtInt kFloorRes = 81;
constexpr RtInt kFloorSamples = 64;
constexpr std::size_t kMinMeasuredPixels = 400;
constexpr double kResidualFloor = 1e-4;
constexpr RtFloat kNear = 0.5f;
constexpr RtFloat kFar = 50.0f;

GMANRayPolygon* buildScaledFloor(RtFloat scale) {
  std::vector<GMANPoint> const verts = {GMANPoint(kFloorXMin * scale, kFloorY * scale, kFloorZMin * scale),
                                        GMANPoint(kFloorXMin * scale, kFloorY * scale, kFloorZMax * scale),
                                        GMANPoint(kFloorXMax * scale, kFloorY * scale, kFloorZMax * scale),
                                        GMANPoint(kFloorXMax * scale, kFloorY * scale, kFloorZMin * scale)};
  return new GMANRayPolygon(verts, GMANParameterList());
}

void testScaledSphereLightOverFloor() {
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
  GMANRaySphere* sphere =
      new GMANRaySphere(scaledRadius, -scaledRadius, scaledRadius, 360.0f, GMANParameterList(), transform);
  GMANLight const areaLight(GMAN_LIGHT_AREA, GMANColor(kAreaLe, kAreaLe, kAreaLe), GMANPoint(), GMANVector());
  gman::Appearance sphereAppearance;
  sphereAppearance.areaLight = &areaLight;
  sphereAppearance.Os = GMANColor(1.0f, 1.0f, 1.0f);
  sphere->setAppearance(sphereAppearance);
  renderer.getWorldManager()->add(sphere);

  GMANFrameBuffer frameBuffer(kFloorRes, kFloorRes, options.getBackground());
  GMANAttributes const attr;
  renderer.render(&frameBuffer, &viewingSys, options, attr);
  check(renderer.droppedPathCount() == 0, "scaled sphere light over floor: droppedPathCount() is 0");

  // Scaling every length about the eye moves no pixel, so pixelMeasured and
  // the analytic formula's own object-space lengths -- unscaled -- still
  // answer the scaled render's own question: the formula is dimensionless
  // in length, so its value is the same at any scale.
  std::vector<double> residuals[3];
  std::vector<double> expectedByChannel[3];
  std::size_t measuredCount = 0;
  for (int py = 0; py < kFloorRes; ++py) {
    for (int px = 0; px < kFloorRes; ++px) {
      if (!pixelMeasured(viewingSys, px, py)) {
        continue;
      }
      ++measuredCount;
      GMANColor const expected = analyticSphereLightExpected(viewingSys, px, py, kAreaLe, kSphereRadius, kSphereCentreX,
                                                             kSphereCentreY, kSphereCentreZ);
      GMANColor const actual = frameBuffer.getPixel(px, py);
      for (int c = 0; c < 3; ++c) {
        residuals[c].push_back(channel(actual, c) - channel(expected, c));
        expectedByChannel[c].push_back(channel(expected, c));
      }
    }
  }
  check(measuredCount >= kMinMeasuredPixels, "scaled sphere light over floor: at least 400 measured pixels");
  checkResiduals(residuals, expectedByChannel, kResidualFloor, "scaled sphere light over floor");
}

} // namespace

int main() {
  testScaledSphereLightOverFloor();

  return checkSummary("a sphere light over the floor, every length scaled by 0.01 about the eye, still matches its "
                      "closed form");
}
