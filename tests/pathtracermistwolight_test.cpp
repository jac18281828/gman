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
 * Two area lights over one floor, comparable light-choice weight at the
 * floor: the mistake this guards is pChoice evaluated at the arrival on
 * the emitter instead of the departing vertex. Every other area-light
 * scene in this unit has one light, where pChoice is 1 regardless of
 * where it is read; this is the one scene where a departure-vertex
 * argument and an arrival-vertex argument to pChoice could disagree.
 */

#include <cmath>
#include <cstdio>
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
#include "gmanray.h"
#include "gmanraypolygon.h"
#include "gmanvector.h"
#include "gmanvsperspective.h"
#include "pathtracerarealightscene.h"
#include "pathtracerscene.h"
#include "ri.h"
#include "samplingstats.h"

namespace {

// Sphere 1 is the shared sphere-over-floor scene's own placement,
// unchanged. Sphere 2's own radius, Le and placement are this test's free
// choice, picked so the two spheres' light-choice weights are comparable
// at the floor (neither swamps the other) rather than for their own
// closed-form irradiance.
constexpr RtFloat kSphere1Radius = 1.0f;
constexpr RtFloat kSphere1CentreX = 10.0f;
constexpr RtFloat kSphere1CentreY = -2.0f;
constexpr RtFloat kSphere1CentreZ = 9.0f;

constexpr RtFloat kSphere2Radius = 1.5f;
constexpr RtFloat kSphere2Le = 6.0f;
constexpr RtFloat kSphere2CentreX = -9.0f;
constexpr RtFloat kSphere2CentreY = -1.0f;
constexpr RtFloat kSphere2CentreZ = 10.0f;

// The measured set for the residual/variance check is restricted to the
// floor's own half nearer sphere 2 (physical x below this bound): the
// mistake this test guards concentrates there, where sphere 2's own
// light-choice weight at the floor is largest, and averaging in the far
// half would dilute it with pixels the mistake barely touches.
constexpr double kNearSphere2XBound = -5.0;

constexpr RtInt kFloorRes = 20;
constexpr RtInt kFloorSamples = 1024;
constexpr std::size_t kMinMeasuredPixels = 100;
constexpr double kResidualFloor = 1e-4;

// A screen window biased toward sphere 2's own side of the floor (negative
// x) and toward the strip of it nearest the camera, so nearly every
// rendered pixel -- not just a fraction of a square window's own -- lands
// in the measured, near-sphere-2 set: the same total ray budget buys many
// more measured pixels than a square window would.
GMANOptions::ScreenWindowStruct biasedScreenWindow() {
  GMANOptions::ScreenWindowStruct sw;
  sw.left = -0.85f;
  sw.right = -0.75f;
  sw.bottom = -0.65f;
  sw.top = -0.20f;
  return sw;
}

// The closest distance from centre to the segment [origin, target], in
// double: 0 when centre projects inside the segment and radius exceeds
// that distance means centre's own sphere crosses the segment.
double closestApproachDistance(GMANPoint const& origin, GMANPoint const& target, GMANPoint const& centre) {
  double const ox = (double)origin.getX(), oy = (double)origin.getY(), oz = (double)origin.getZ();
  double const dx = (double)target.getX() - ox, dy = (double)target.getY() - oy, dz = (double)target.getZ() - oz;
  double const cx = (double)centre.getX() - ox, cy = (double)centre.getY() - oy, cz = (double)centre.getZ() - oz;
  double const segLenSq = dx * dx + dy * dy + dz * dz;
  double t = (segLenSq > 0.0) ? (cx * dx + cy * dy + cz * dz) / segLenSq : 0.0;
  t = (t < 0.0) ? 0.0 : ((t > 1.0) ? 1.0 : t);
  double const px = ox + t * dx - (double)centre.getX();
  double const py = oy + t * dy - (double)centre.getY();
  double const pz = oz + t * dz - (double)centre.getZ();
  return std::sqrt(px * px + py * py + pz * pz);
}

// Neither sphere occludes the other from a representative floor point, and
// neither crosses the camera's own view of it: the closest approach of
// each segment (floor point to the other sphere's centre; camera origin
// to the floor point) to the sphere not being targeted clears that
// sphere's own radius, with margin.
void checkNoOcclusion() {
  GMANPoint const origin(0.0f, 0.0f, 0.0f);
  // x = -6 sits inside the measured set (x < kNearSphere2XBound), not the
  // far half the residual/variance check never reads.
  GMANPoint const floorPoint(-6.0f, kFloorY, 9.0f);
  GMANPoint const centre1(kSphere1CentreX, kSphere1CentreY, kSphere1CentreZ);
  GMANPoint const centre2(kSphere2CentreX, kSphere2CentreY, kSphere2CentreZ);

  double const toSphere1ClearOfSphere2 = closestApproachDistance(floorPoint, centre1, centre2);
  double const toSphere2ClearOfSphere1 = closestApproachDistance(floorPoint, centre2, centre1);
  std::printf("two area lights: floor-to-sphere1 clears sphere2 by %.4f, floor-to-sphere2 clears sphere1 by %.4f\n",
              toSphere1ClearOfSphere2 - (double)kSphere2Radius, toSphere2ClearOfSphere1 - (double)kSphere1Radius);
  check(toSphere1ClearOfSphere2 > (double)kSphere2Radius,
        "two area lights: sphere 2 does not occlude the floor's own shadow ray to sphere 1");
  check(toSphere2ClearOfSphere1 > (double)kSphere1Radius,
        "two area lights: sphere 1 does not occlude the floor's own shadow ray to sphere 2");

  double const cameraToFloorClearOfSphere1 = closestApproachDistance(origin, floorPoint, centre1);
  double const cameraToFloorClearOfSphere2 = closestApproachDistance(origin, floorPoint, centre2);
  check(cameraToFloorClearOfSphere1 > (double)kSphere1Radius,
        "two area lights: sphere 1 does not cross the camera's own view of the floor");
  check(cameraToFloorClearOfSphere2 > (double)kSphere2Radius,
        "two area lights: sphere 2 does not cross the camera's own view of the floor");
}

void renderTwoLightFloor(std::unique_ptr<GMANFrameBuffer>& frameBufferOut,
                         std::unique_ptr<gman::VSPerspective>& viewingSysOut, std::size_t& droppedOut) {
  GMANOptions options;
  options.setFormat(kFloorRes, kFloorRes, 1.0f);
  options.setPixelSamples(1.0f, 1.0f);
  options.setPixelFilter(RiBoxFilter, 1.0f, 1.0f);
  options.setPathtracerSamples(kFloorSamples);
  options.setBackground(GMANColor(0.0f, 0.0f, 0.0f));

  GMANMatrix4 const identity;
  viewingSysOut.reset(
      new gman::VSPerspective(kFloorRes, kFloorRes, biasedScreenWindow(), identity, 90.0f, 0.5f, 50.0f));

  GMANPathtraceRenderer renderer;
  GMANRayPolygon* floor = buildFloor();
  gman::Appearance floorAppearance;
  floorAppearance.shader = loadShader("matte", matteParams(1.0f));
  floorAppearance.Cs = GMANColor(0.5f, 0.5f, 0.5f);
  floorAppearance.Os = GMANColor(1.0f, 1.0f, 1.0f);
  floor->setAppearance(floorAppearance);
  renderer.getWorldManager()->add(floor);

  GMANLight const areaLight1(GMAN_LIGHT_AREA, GMANColor(kAreaLe, kAreaLe, kAreaLe), GMANPoint(), GMANVector());
  addEmittingSphere(renderer, areaLight1, kSphere1Radius, kSphere1CentreX, kSphere1CentreY, kSphere1CentreZ);

  GMANLight const areaLight2(GMAN_LIGHT_AREA, GMANColor(kSphere2Le, kSphere2Le, kSphere2Le), GMANPoint(), GMANVector());
  addEmittingSphere(renderer, areaLight2, kSphere2Radius, kSphere2CentreX, kSphere2CentreY, kSphere2CentreZ);

  frameBufferOut.reset(new GMANFrameBuffer(kFloorRes, kFloorRes, options.getBackground()));
  GMANAttributes const attr;
  renderer.render(frameBufferOut.get(), viewingSysOut.get(), options, attr);
  droppedOut = renderer.droppedPathCount();
}

void testTwoAreaLightsSumToAnalytic() {
  checkNoOcclusion();

  std::unique_ptr<GMANFrameBuffer> frameBuffer;
  std::unique_ptr<gman::VSPerspective> viewingSys;
  std::size_t dropped = 0;
  renderTwoLightFloor(frameBuffer, viewingSys, dropped);
  check(dropped == 0, "two area lights: droppedPathCount() is 0");

  std::vector<double> residuals[3];
  std::vector<double> expectedByChannel[3];
  std::vector<double> actualValues[3];
  std::size_t measuredCount = 0;
  for (int py = 0; py < kFloorRes; ++py) {
    for (int px = 0; px < kFloorRes; ++px) {
      if (!pixelMeasured(*viewingSys, px, py)) {
        continue;
      }
      GMANRay const centreRay = viewingSys->cameraRay((RtFloat)px + 0.5f, (RtFloat)py + 0.5f);
      double centreX = 0.0, centreZ = 0.0;
      if (!intersectFloorPlane(centreRay, centreX, centreZ) || centreX >= kNearSphere2XBound) {
        continue;
      }
      ++measuredCount;
      GMANColor const expected1 = analyticSphereLightExpected(*viewingSys, px, py, kAreaLe, kSphere1Radius,
                                                              kSphere1CentreX, kSphere1CentreY, kSphere1CentreZ);
      GMANColor const expected2 = analyticSphereLightExpected(*viewingSys, px, py, kSphere2Le, kSphere2Radius,
                                                              kSphere2CentreX, kSphere2CentreY, kSphere2CentreZ);
      GMANColor const expected(expected1.getRed() + expected2.getRed(), expected1.getGreen() + expected2.getGreen(),
                               expected1.getBlue() + expected2.getBlue());
      GMANColor const actual = frameBuffer->getPixel(px, py);
      for (int c = 0; c < 3; ++c) {
        residuals[c].push_back(channel(actual, c) - channel(expected, c));
        expectedByChannel[c].push_back(channel(expected, c));
        actualValues[c].push_back(channel(actual, c));
      }
    }
  }
  std::printf("two area lights: %zu measured pixels near sphere 2\n", measuredCount);
  check(measuredCount >= kMinMeasuredPixels, "two area lights: at least 100 measured pixels near sphere 2");

  GmanMeanStderr const actualStat = meanStderr(actualValues[0]);
  std::printf("two area lights: floor mean (red channel) %.6f (stderr %.6f)\n", actualStat.mean,
              actualStat.stderrOfMean);

  checkResiduals(residuals, expectedByChannel, kResidualFloor, "two area lights");
}

} // namespace

int main() {
  testTwoAreaLightsSumToAnalytic();

  return checkSummary("two area lights of different power and distance sum to their analytic irradiance on the "
                      "shared floor scene");
}
