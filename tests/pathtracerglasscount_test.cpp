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
 * An area light seen through a glass pane must count once: next-event
 * estimation's own shadow ray already carries it through the dielectric's
 * shadowTransmittance, so a BSDF-sampled bounce that refracts through the
 * same pane and lands on the light must not add it again.
 */

#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "check.h"
#include "gmanattributes.h"
#include "gmanemitter.h"
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
#include "gmansampling.h"
#include "gmanshading.h"
#include "gmantransform.h"
#include "gmanvector.h"
#include "gmanvsperspective.h"
#include "pathtracerarealightscene.h"
#include "pathtracerscene.h"
#include "ri.h"
#include "samplingstats.h"

namespace {

// The pane's own geometry, matching pathtracerarealightpane_test.cpp's:
// strictly between the floor's own x extent and the emitter's nearest
// surface point, wide enough that no ray around it reaches the emitter.
constexpr RtFloat kPaneX = 8.9f;
constexpr RtFloat kPaneHalfExtent = 50.0f;

GMANRayPolygon* buildGlassPane(RtFloat os) {
  std::vector<GMANPoint> const verts = {
      GMANPoint(kPaneX, -kPaneHalfExtent, -kPaneHalfExtent), GMANPoint(kPaneX, -kPaneHalfExtent, kPaneHalfExtent),
      GMANPoint(kPaneX, kPaneHalfExtent, kPaneHalfExtent), GMANPoint(kPaneX, kPaneHalfExtent, -kPaneHalfExtent)};
  GMANRayPolygon* pane = new GMANRayPolygon(verts, GMANParameterList());
  gman::Appearance appearance;
  appearance.shader = loadShader("glass", GMANParameterList());
  appearance.Os = GMANColor(os, os, os);
  pane->setAppearance(appearance);
  return pane;
}

// The pane's own shadowTransmittance along the direction from a known
// point toward another, found by an actual intersection against pane:
// the same quantity the shadow walk itself evaluates, read here as ground
// truth for the render check below, never re-derived by hand.
GMANColor paneTransmittanceToward(GMANRayPolygon* pane, GMANMatrix4 const& cameraToWorld, GMANPoint const& from,
                                  GMANPoint const& toward) {
  GMANVector dir(from, toward);
  RtFloat const dist = dir.magnitude();
  dir.normalize();
  GMANRay const ray(from, dir, RI_EPSILON, dist);
  GMANHit hit;
  if (!pane->intersect(ray, hit)) {
    return GMANColor(1.0f, 1.0f, 1.0f);
  }
  gman::SurfacePoint const point = gman::hitSurfacePoint(ray, hit);
  gman::BSDF const closure = gman::bsdf(pane->getAppearance(), point, cameraToWorld);
  return closure.shadowTransmittance(dir);
}

// Like pathtracerarealightscene.h's analyticSphereLightExpected, but
// multiplies each of the same 16x16 midpoints' own term by the pane's own
// shadowTransmittance along that same midpoint's direction to the
// sphere's centre, since a genuine dielectric's transmittance is not a
// single scene-wide scalar.
GMANColor analyticPaneTransmittedExpected(gman::VSPerspective& viewingSys, int px, int py, RtFloat le, RtFloat radius,
                                          RtFloat centreX, RtFloat centreY, RtFloat centreZ, GMANRayPolygon* pane,
                                          GMANMatrix4 const& cameraToWorld) {
  constexpr int kMidGrid = 16;
  constexpr RtFloat kReflectance = 0.5f;
  GMANPoint const centre(centreX, centreY, centreZ);
  double sum = 0.0;
  for (int sy = 0; sy < kMidGrid; ++sy) {
    for (int sx = 0; sx < kMidGrid; ++sx) {
      RtFloat const rx = (RtFloat)px + ((RtFloat)sx + 0.5f) / (RtFloat)kMidGrid;
      RtFloat const ry = (RtFloat)py + ((RtFloat)sy + 0.5f) / (RtFloat)kMidGrid;
      GMANRay const ray = viewingSys.cameraRay(rx, ry);
      double x, z;
      if (!intersectFloorPlane(ray, x, z)) {
        continue;
      }
      GMANPoint const floorPoint((RtFloat)x, kFloorY, (RtFloat)z);
      GMANColor const transmittance = paneTransmittanceToward(pane, cameraToWorld, floorPoint, centre);

      double const dx = x - (double)centreX;
      double const dy = (double)kFloorY - (double)centreY;
      double const dz = z - (double)centreZ;
      double const dist2 = dx * dx + dy * dy + dz * dz;
      double const dist = std::sqrt(dist2);
      double const term =
          (double)kReflectance * (double)le * (double)radius * (double)radius * std::fabs(dy) / (dist2 * dist);
      sum += term * (double)transmittance.getRed();
    }
  }
  RtFloat const value = (RtFloat)(sum / (double)(kMidGrid * kMidGrid));
  return GMANColor(value, value, value);
}

// pathtracerarealightpane_test.cpp's own scene, the pane's surface glass
// and Os 1: fully opaque coverage-wise, so the pane's own contribution to
// the shadow walk's transmittance runs entirely through its dielectric
// lobe, none through coverage pass-through.
void testGlassPaneCountsOnce() {
  constexpr RtFloat kSphereRadius = 1.0f;
  constexpr RtFloat kSphereCentreX = 10.0f;
  constexpr RtFloat kSphereCentreY = -2.0f;
  constexpr RtFloat kSphereCentreZ = 9.0f;
  constexpr RtInt kFloorRes = 81;
  constexpr RtInt kFloorSamples = 64;
  constexpr std::size_t kMinMeasuredPixels = 400;
  constexpr double kResidualFloor = 1e-4;

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

  GMANLight const areaLight(GMAN_LIGHT_AREA, GMANColor(kAreaLe, kAreaLe, kAreaLe), GMANPoint(), GMANVector());
  addEmittingSphere(renderer, areaLight, kSphereRadius, kSphereCentreX, kSphereCentreY, kSphereCentreZ);

  GMANRayPolygon* pane = buildGlassPane(1.0f);
  renderer.getWorldManager()->add(pane);

  GMANFrameBuffer frameBuffer(kFloorRes, kFloorRes, options.getBackground());
  GMANAttributes const attr;
  renderer.render(&frameBuffer, &viewingSys, options, attr);
  check(renderer.droppedPathCount() == 0, "glass pane: droppedPathCount() is 0");

  std::vector<double> residuals[3];
  std::vector<double> expectedByChannel[3];
  std::size_t measuredCount = 0;
  for (int py = 0; py < kFloorRes; ++py) {
    for (int px = 0; px < kFloorRes; ++px) {
      if (!pixelMeasured(viewingSys, px, py)) {
        continue;
      }
      ++measuredCount;
      GMANColor const expected = analyticPaneTransmittedExpected(
          viewingSys, px, py, kAreaLe, kSphereRadius, kSphereCentreX, kSphereCentreY, kSphereCentreZ, pane, identity);
      GMANColor const actual = frameBuffer.getPixel(px, py);
      for (int c = 0; c < 3; ++c) {
        residuals[c].push_back(channel(actual, c) - channel(expected, c));
        expectedByChannel[c].push_back(channel(expected, c));
      }
    }
  }
  check(measuredCount >= kMinMeasuredPixels, "glass pane: at least 400 measured pixels");
  checkResiduals(residuals, expectedByChannel, kResidualFloor, "glass pane, Os 1");
}

// A large emitter close behind a glass pane of Os 0.5, so the shadow
// walk's transmittance carries both a pass-through share and a dielectric
// share and the power heuristic's next-event weight sits well below 1.
// MIS-on and MIS-off are each, independently, unbiased, so their paired
// per-pixel residual (same seed, same N, differing only in which weight
// formula applied) must agree with 0.
constexpr RtFloat kBigSphereRadius = 6.0f;
constexpr RtFloat kBigSphereCentreX = 16.0f;
constexpr RtFloat kBigSphereCentreY = 15.0f;
constexpr RtFloat kBigSphereCentreZ = 9.0f;
constexpr RtFloat kBigAreaLe = 10.0f;
constexpr RtFloat kPairPaneOs = 0.5f;
constexpr RtInt kPairRes = 41;
constexpr std::uint32_t kPairSamples = 128u;

std::unique_ptr<GMANFrameBuffer> renderGlassPanePair(bool misEnabled) {
  GMANOptions options;
  options.setFormat(kPairRes, kPairRes, 1.0f);
  options.setPixelSamples(1.0f, 1.0f);
  options.setPixelFilter(RiBoxFilter, 1.0f, 1.0f);
  options.setPathtracerSamples((RtInt)kPairSamples);
  options.setBackground(GMANColor(0.0f, 0.0f, 0.0f));

  GMANMatrix4 const identity;
  gman::VSPerspective viewingSys(kPairRes, kPairRes, squareScreenWindow(), identity, 90.0f, 0.5f, 50.0f);

  auto renderer = std::make_unique<GMANPathtraceRenderer>();
  renderer->setMultipleImportanceSampling(misEnabled);

  GMANRayPolygon* floor = buildFloor();
  gman::Appearance floorAppearance;
  floorAppearance.shader = loadShader("matte", matteParams(1.0f));
  floorAppearance.Cs = GMANColor(0.5f, 0.5f, 0.5f);
  floorAppearance.Os = GMANColor(1.0f, 1.0f, 1.0f);
  floor->setAppearance(floorAppearance);
  renderer->getWorldManager()->add(floor);

  GMANLight const areaLight(GMAN_LIGHT_AREA, GMANColor(kBigAreaLe, kBigAreaLe, kBigAreaLe), GMANPoint(), GMANVector());
  addEmittingSphere(*renderer, areaLight, kBigSphereRadius, kBigSphereCentreX, kBigSphereCentreY, kBigSphereCentreZ);

  GMANRayPolygon* pane = buildGlassPane(kPairPaneOs);
  renderer->getWorldManager()->add(pane);

  auto frameBuffer = std::make_unique<GMANFrameBuffer>(kPairRes, kPairRes, options.getBackground());
  GMANAttributes const attr;
  renderer->render(frameBuffer.get(), &viewingSys, options, attr);
  check(renderer->droppedPathCount() == 0, "glass pane pair: droppedPathCount() is 0");
  return frameBuffer;
}

// Scene-validity guard: at the floor's own centre, next-event
// estimation's own mean weight toward the big sphere, over many random
// draws (the real estimator's own (u1, u2), never the fixed 0.5, 0.5
// preview alone), sits well below 1 -- this scene actually exercises the
// split-weight arithmetic between a pass-through share and a dielectric
// share, not a case that trivially reduces to weight 1. A failed
// assertion here is a STOP to fix the geometry, not the estimator.
void checkNextEventWeightWellBelowOne() {
  constexpr RtFloat kPi = 3.14159265358979323846f;
  constexpr std::uint32_t kDraws = 1024u;

  GMANPathtraceRenderer renderer;
  GMANLight const areaLight(GMAN_LIGHT_AREA, GMANColor(kBigAreaLe, kBigAreaLe, kBigAreaLe), GMANPoint(), GMANVector());
  addEmittingSphere(renderer, areaLight, kBigSphereRadius, kBigSphereCentreX, kBigSphereCentreY, kBigSphereCentreZ);
  std::vector<gman::Emitter> const emitters = gman::emitters(*renderer.getWorldManager());
  check(emitters.size() == 1, "glass pane pair: the validity check's own world has exactly one emitter");

  GMANPoint const floorCentre(0.0f, kFloorY, 9.0f);
  double weightSum = 0.0;
  for (std::uint32_t s = 0; s < kDraws; ++s) {
    gman::Sample2D const uv = gman::sample2D(1u, 0, 0, s, kDraws, 0u);
    gman::EmitterSample const es = gman::sample(emitters[0], floorCentre, uv.u1, uv.u2);
    if (!(es.pdf > 0.0f) || (es.Cl.getRed() == 0.0f && es.Cl.getGreen() == 0.0f && es.Cl.getBlue() == 0.0f)) {
      continue;
    }
    RtFloat const pLight = es.pdf;                    // pChoice is 1: the validity check's only light.
    RtFloat const cosTheta = std::fabs(es.wi.getY()); // the floor's own normal is (0, 1, 0).
    RtFloat const pBsdf = cosTheta / kPi;
    weightSum += (double)gman::powerHeuristic(pLight, pBsdf);
  }
  double const meanWeight = weightSum / (double)kDraws;
  std::printf("glass pane pair: mean next-event weight over %u draws is %.6f\n", kDraws, meanWeight);
  check(meanWeight < 0.7, "glass pane pair: next-event's own mean weight toward the big sphere sits well below 1");
}

void testGlassPaneMisOnOffPairing() {
  checkNextEventWeightWellBelowOne();

  GMANMatrix4 const identity;
  gman::VSPerspective viewingSys(kPairRes, kPairRes, squareScreenWindow(), identity, 90.0f, 0.5f, 50.0f);

  std::unique_ptr<GMANFrameBuffer> const misOn = renderGlassPanePair(true);
  std::unique_ptr<GMANFrameBuffer> const misOff = renderGlassPanePair(false);

  std::vector<double> paired[3];
  std::size_t measuredCount = 0;
  for (int py = 0; py < kPairRes; ++py) {
    for (int px = 0; px < kPairRes; ++px) {
      if (!pixelMeasured(viewingSys, px, py)) {
        continue;
      }
      ++measuredCount;
      GMANColor const onPixel = misOn->getPixel(px, py);
      GMANColor const offPixel = misOff->getPixel(px, py);
      for (int c = 0; c < 3; ++c) {
        paired[c].push_back(channel(onPixel, c) - channel(offPixel, c));
      }
    }
  }
  check(measuredCount >= 400, "glass pane pair: at least 400 measured pixels");

  for (int c = 0; c < 3; ++c) {
    GmanMeanStderr const stat = meanStderr(paired[c]);
    std::printf("glass pane pair channel %d: paired residual mean %.6f (%.3f sigma)\n", c, stat.mean,
                stat.stderrOfMean > 0.0 ? stat.mean / stat.stderrOfMean : 0.0);
    checkNear(stat.mean, 0.0, stat.stderrOfMean, 1e-4,
              "glass pane pair: MIS-on and MIS-off agree within 5 sigma, channel " + std::to_string(c));
  }
}

} // namespace

int main() {
  testGlassPaneCountsOnce();
  testGlassPaneMisOnOffPairing();

  return checkSummary("an area light seen through a glass pane counts once, MIS included");
}
