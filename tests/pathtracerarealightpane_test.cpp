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
 * The unscaled sphere-over-floor scene with a flat, partly opaque pane
 * between the sphere and the floor: a large blocker's own offset must not
 * carry the shadow walk past the emitter's own small target.
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
#include "gmanraybbox.h"
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

constexpr RtFloat kSphereRadius = 1.0f;
constexpr RtFloat kSphereCentreX = 10.0f;
constexpr RtFloat kSphereCentreY = -2.0f;
constexpr RtFloat kSphereCentreZ = 9.0f;
constexpr RtInt kFloorRes = 81;
constexpr RtInt kFloorSamples = 64;
constexpr std::size_t kMinMeasuredPixels = 400;
constexpr double kResidualFloor = 1e-4;
// The pane's own transmittance: opaque enough to measure, transparent
// enough that some light still crosses it.
constexpr RtFloat kPaneOs = 0.5f;
// Strictly between the floor's own x extent (at most kFloorXMax) and the
// emitter's own nearest surface point (kSphereCentreX - kSphereRadius): every
// segment from any point on the emitter's own surface to any floor point
// crosses this plane exactly once.
constexpr RtFloat kPaneX = 8.9f;
constexpr RtFloat kPaneHalfExtent = 50.0f;

GMANRayPolygon* buildPane() {
  std::vector<GMANPoint> const verts = {
      GMANPoint(kPaneX, -kPaneHalfExtent, -kPaneHalfExtent), GMANPoint(kPaneX, -kPaneHalfExtent, kPaneHalfExtent),
      GMANPoint(kPaneX, kPaneHalfExtent, kPaneHalfExtent), GMANPoint(kPaneX, kPaneHalfExtent, -kPaneHalfExtent)};
  return new GMANRayPolygon(verts, GMANParameterList());
}

void testPaneBetweenSphereAndFloor() {
  GMANOptions options;
  options.setFormat(kFloorRes, kFloorRes, 1.0f);
  options.setPixelSamples(1.0f, 1.0f);
  options.setPixelFilter(RiBoxFilter, 1.0f, 1.0f);
  options.setPathtracerSamples(kFloorSamples);
  options.setBackground(GMANColor(0.0f, 0.0f, 0.0f));

  GMANMatrix4 const identity;
  gman::VSPerspective viewingSys(kFloorRes, kFloorRes, squareScreenWindow(), identity, 90.0f, 0.5f, 50.0f);

  GMANPathtraceRenderer renderer;
  GMANRayPolygon* floor = buildFloor();
  gman::Appearance floorAppearance;
  floorAppearance.shader = loadShader("matte", matteParams(1.0f));
  floorAppearance.Cs = GMANColor(0.5f, 0.5f, 0.5f);
  floorAppearance.Os = GMANColor(1.0f, 1.0f, 1.0f);
  floor->setAppearance(floorAppearance);
  renderer.getWorldManager()->add(floor);

  GMANMatrix4 place;
  place.trans(kSphereCentreX, kSphereCentreY, kSphereCentreZ);
  GMANTransform const transform = makeTransform(place);
  const auto sphere =
      new GMANRaySphere(kSphereRadius, -kSphereRadius, kSphereRadius, 360.0f, GMANParameterList(), transform);
  GMANLight const areaLight(GMAN_LIGHT_AREA, GMANColor(kAreaLe, kAreaLe, kAreaLe), GMANPoint(), GMANVector());
  gman::Appearance sphereAppearance;
  sphereAppearance.areaLight = &areaLight;
  sphereAppearance.Os = GMANColor(1.0f, 1.0f, 1.0f);
  sphere->setAppearance(sphereAppearance);
  renderer.getWorldManager()->add(sphere);

  GMANRayPolygon* pane = buildPane();
  gman::Appearance paneAppearance;
  paneAppearance.Cs = GMANColor(0.0f, 0.0f, 0.0f);
  paneAppearance.Os = GMANColor(kPaneOs, kPaneOs, kPaneOs);
  pane->setAppearance(paneAppearance);
  renderer.getWorldManager()->add(pane);

  check(gman::primitiveMagnitude(pane->getBBox()) >= 4.0f * gman::primitiveMagnitude(sphere->getBBox()),
        "pane: primitiveMagnitude of its own bbox is at least 4x the emitter's");

  GMANFrameBuffer frameBuffer(kFloorRes, kFloorRes, options.getBackground());
  GMANAttributes const attr;
  renderer.render(&frameBuffer, &viewingSys, options, attr);
  check(renderer.droppedPathCount() == 0, "pane between sphere and floor: droppedPathCount() is 0");

  std::vector<double> residuals[3];
  std::vector<double> expectedByChannel[3];
  std::size_t measuredCount = 0;
  for (int py = 0; py < kFloorRes; ++py) {
    for (int px = 0; px < kFloorRes; ++px) {
      if (!pixelMeasured(viewingSys, px, py)) {
        continue;
      }
      ++measuredCount;
      GMANColor const unblocked = analyticSphereLightExpected(viewingSys, px, py, kAreaLe, kSphereRadius,
                                                              kSphereCentreX, kSphereCentreY, kSphereCentreZ);
      GMANColor const expected(unblocked.getRed() * kPaneOs, unblocked.getGreen() * kPaneOs,
                               unblocked.getBlue() * kPaneOs);
      GMANColor const actual = frameBuffer.getPixel(px, py);
      for (int c = 0; c < 3; ++c) {
        residuals[c].push_back(channel(actual, c) - channel(expected, c));
        expectedByChannel[c].push_back(channel(expected, c));
      }
    }
  }
  check(measuredCount >= kMinMeasuredPixels, "pane between sphere and floor: at least 400 measured pixels");
  checkResiduals(residuals, expectedByChannel, kResidualFloor, "pane between sphere and floor");
}

} // namespace

int main() {
  testPaneBetweenSphereAndFloor();

  return checkSummary("a partly opaque pane between the sphere and the floor attenuates the floor's own irradiance "
                      "by exactly its own transmittance");
}
