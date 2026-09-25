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
 * GMANRaytraceRenderer::getDepth returns camera-space z of the
 * nearest hit, RI_INFINITY where none hit. A 41x41 (odd) Format at
 * PixelSamples 1 1 puts the single sample of the centre pixel exactly on
 * the view axis, so a unit sphere at camera-space z=5 pins its depth to
 * 4.0 (the near side of the sphere); a corner pixel's ray, well outside
 * the sphere's small angular radius, pins RI_INFINITY. Two overlapping
 * spheres on the axis pin the nearer one's own hit, not merely a hit.
 * An off-axis sample pins the hit point's z, which is not its ray's t.
 *
 * Built from the plugin's own sources, as raysphere_test.cpp is: the
 * viewing system is built directly, as cameraray_test.cpp does, rather
 * than through a RIB parse.
 */

#include <cmath>

#include "check.h"
#include "gmanattributes.h"
#include "gmanframebuffer.h"
#include "gmanobjectmanager.h"
#include "gmanoptions.h"
#include "gmanparameterlist.h"
#include "gmanpoint.h"
#include "gmanray.h"
#include "gmanraytracerenderer.h"
#include "gmantransform.h"
#include "gmanvector.h"
#include "gmanvsperspective.h"
#include "ri.h"

namespace {

constexpr RtInt kRes = 41;
constexpr RtFloat kTolerance = 1e-3f;

GMANTransform makeTransform(GMANMatrix4 matrix) {
  GMANOneMatrix storage(matrix);
  return GMANTransform(storage);
}

GMANOptions::ScreenWindowStruct squareScreenWindow() {
  GMANOptions::ScreenWindowStruct sw;
  sw.left = -1.0;
  sw.right = 1.0;
  sw.bottom = -1.0;
  sw.top = 1.0;
  return sw;
}

GMANPrimitive* addSphereAt(GMANRaytraceRenderer& renderer, GMANOptions& options, GMANAttributes& attr,
                           RtFloat centreZ) {
  GMANMatrix4 place;
  place.trans(0.0, 0.0, centreZ);
  GMANTransform transform = makeTransform(place);
  GMANPrimitive* sphere =
      renderer.getObjectManager()->getRSSphere(1.0, -1.0, 1.0, 360.0, GMANParameterList(), &options, &attr, &transform);
  renderer.getWorldManager()->add(sphere);
  return sphere;
}

// ---- two overlapping spheres on the view axis: the nearer wins ----
// Falsifies nearestHit's own comparison flipped from < to >: both spheres
// overlap, so either one is a hit, and only the comparison distinguishes
// which one nearestHit keeps.
void testNearerOverlappingSphereWins() {
  GMANOptions options;
  options.setFormat(kRes, kRes, 1.0);
  options.setPixelSamples(1.0, 1.0);

  GMANMatrix4 const identity;
  gman::VSPerspective viewingSys(kRes, kRes, squareScreenWindow(), identity, 90.0, 0.5, 50.0);

  GMANRaytraceRenderer renderer;
  GMANAttributes attr;
  // Near sphere occupies camera-space z in [4, 6]; far sphere [4.8, 6.8].
  // They overlap in [4.8, 6], so a farther-wins comparison still reports a
  // hit -- the wrong one -- rather than failing to hit at all.
  addSphereAt(renderer, options, attr, 5.0);
  addSphereAt(renderer, options, attr, 5.8);

  GMANFrameBuffer frameBuffer(kRes, kRes, options.getBackground());
  renderer.render(&frameBuffer, &viewingSys, options, attr);

  RtFloat const centreDepth = renderer.getDepth(kRes / 2, kRes / 2);
  check(std::fabs(centreDepth - 4.0) <= kTolerance,
        "depth: two overlapping spheres on the view axis resolve to the nearer one's own hit");
}

// ---- off-axis sample: t and camera-space z disagree ----
// Falsifies zTestAndSet's depth argument reverted from hit.t to
// hit.point.getZ(): an off-axis ray's t (distance along the ray) and its
// hit point's z differ whenever the ray's direction is not (0,0,1), unlike
// the view-axis checks above where the two coincide. The expected z is
// worked out independently, by the sphere quadratic on the same ray
// viewingSys.cameraRay hands the renderer, not by calling into the
// renderer's own intersect.
void testOffAxisDepthIsCameraSpaceZ() {
  GMANOptions options;
  options.setFormat(kRes, kRes, 1.0);
  options.setPixelSamples(1.0, 1.0);

  GMANMatrix4 const identity;
  gman::VSPerspective viewingSys(kRes, kRes, squareScreenWindow(), identity, 90.0, 0.5, 50.0);

  GMANRaytraceRenderer renderer;
  GMANAttributes attr;
  addSphereAt(renderer, options, attr, 5.0);

  GMANFrameBuffer frameBuffer(kRes, kRes, options.getBackground());
  renderer.render(&frameBuffer, &viewingSys, options, attr);

  int const px = kRes / 2 + 3;
  int const py = kRes / 2;
  GMANRay const ray = viewingSys.cameraRay((RtFloat)px + 0.5f, (RtFloat)py + 0.5f);

  GMANPoint const sphereCentre(0.0, 0.0, 5.0);
  GMANVector const originToCentre(sphereCentre, ray.getOrigin());
  RtFloat const b = 2.0f * ray.getDirection().dot(originToCentre);
  RtFloat const c = originToCentre.dot(originToCentre) - 1.0f;
  RtFloat const discriminant = b * b - 4.0f * c;
  check(discriminant > 0.0f, "off-axis depth: the chosen sample still hits the sphere");
  if (discriminant <= 0.0f) {
    return;
  }
  RtFloat const expectedT = (-b - std::sqrt(discriminant)) / 2.0f;
  RtFloat const expectedZ = ray.pointAt(expectedT).getZ();

  check(std::fabs(expectedZ - expectedT) > 10.0f * kTolerance,
        "off-axis depth: the sample geometry makes t and z genuinely differ, so this check can tell "
        "getDepth's z from its t");

  RtFloat const depth = renderer.getDepth(px, py);
  check(std::fabs(depth - expectedZ) <= kTolerance,
        "depth: an off-axis sample pins getDepth to the hit point's camera-space z, not the ray's t");
}

} // namespace

int main() {
  GMANOptions options;
  options.setFormat(kRes, kRes, 1.0);
  options.setPixelSamples(1.0, 1.0);

  GMANOptions::ScreenWindowStruct sw;
  sw.left = -1.0;
  sw.right = 1.0;
  sw.bottom = -1.0;
  sw.top = 1.0;

  GMANMatrix4 const identity;
  gman::VSPerspective viewingSys(kRes, kRes, sw, identity, 90.0, 0.5, 50.0);

  GMANRaytraceRenderer renderer;

  GMANMatrix4 place;
  place.trans(0.0, 0.0, 5.0);
  GMANTransform transform = makeTransform(place);

  GMANAttributes attr;
  GMANPrimitive* sphere =
      renderer.getObjectManager()->getRSSphere(1.0, -1.0, 1.0, 360.0, GMANParameterList(), &options, &attr, &transform);
  renderer.getWorldManager()->add(sphere);

  GMANFrameBuffer frameBuffer(kRes, kRes, options.getBackground());
  renderer.render(&frameBuffer, &viewingSys, options, attr);

  RtFloat const centreDepth = renderer.getDepth(kRes / 2, kRes / 2);
  check(std::fabs(centreDepth - 4.0) <= kTolerance, "depth: the centre sample pins getDepth to 4.0");

  RtFloat const cornerDepth = renderer.getDepth(0, 0);
  check(cornerDepth == RI_INFINITY, "depth: a corner ray missing the sphere pins getDepth to RI_INFINITY");

  testNearerOverlappingSphereWins();
  testOffAxisDepthIsCameraSpaceZ();

  return checkSummary("GMANRaytraceRenderer::getDepth pins camera-space z at a hit and RI_INFINITY at a miss");
}
