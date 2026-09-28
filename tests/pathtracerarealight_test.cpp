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
 * The path tracer's own area-light behaviour: a sphere light over the
 * shared floor scene checked against its closed-form irradiance, and the
 * emitter-hit accounting that lets a camera ray, a mirror bounce and a
 * glass transmission each read Le without double-counting what
 * next-event estimation already estimated. droppedPathCount and alpha
 * stay a smoke check over the floor scene's own render.
 */

#include <cmath>
#include <cstdio>
#include <memory>
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
#include "gmanraybbox.h"
#include "gmanrayoccluder.h"
#include "gmanraypolygon.h"
#include "gmanraysphere.h"
#include "gmantransform.h"
#include "gmanvector.h"
#include "gmanvsperspective.h"
#include "pathtracerscene.h"
#include "ri.h"
#include "samplingstats.h"

namespace {

GMANTransform makeTransform(GMANMatrix4 matrix) {
  GMANOneMatrix storage(matrix);
  return GMANTransform(storage);
}

// ---- D.1: the sphere light over the floor ----

constexpr RtFloat kAreaLe = 10.0f;
constexpr RtFloat kSphereRadius = 1.0f;
constexpr RtFloat kSphereCentreX = 10.0f;
constexpr RtFloat kSphereCentreY = -2.0f;
constexpr RtFloat kSphereCentreZ = 9.0f;
constexpr RtFloat kSphereHeight = kSphereCentreY - kFloorY; // 2: sphere centre's height above the floor
constexpr RtInt kFloorRes = 81;
constexpr RtInt kFloorSamples = 64;
constexpr std::size_t kMinMeasuredPixels = 400;
constexpr double kResidualFloor = 1e-4;

// The mean, over a 16 x 16 midpoint grid of pixel (px, py)'s cell, of the
// equivalent-point-source formula for a Lambertian sphere light: rho * Le *
// radius^2 * h / d^3, d the distance from the grid point to the sphere's
// centre. Mirrors analyticExpected's own grid technique for a delta light.
GMANColor analyticAreaLightExpected(gman::VSPerspective& viewingSys, int px, int py) {
  constexpr int kMidGrid = 16;
  constexpr RtFloat kReflectance = 0.5f;
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
      double const dx = x - (double)kSphereCentreX;
      double const dy = (double)kFloorY - (double)kSphereCentreY;
      double const dz = z - (double)kSphereCentreZ;
      double const dist2 = dx * dx + dy * dy + dz * dz;
      double const dist = std::sqrt(dist2);
      sum += (double)kReflectance * (double)kAreaLe * (double)kSphereRadius * (double)kSphereRadius *
             (double)kSphereHeight / (dist2 * dist);
    }
  }
  RtFloat const value = (RtFloat)(sum / (double)(kMidGrid * kMidGrid));
  return GMANColor(value, value, value);
}

// The floor under one emitting sphere, off to the side at a height and
// distance no floor camera ray can pass within the sphere's own radius of
// (D.1's own geometric argument): renders once, for both the residual
// check and D.3's smoke check to share.
void renderAreaLightFloor(std::unique_ptr<GMANFrameBuffer>& frameBufferOut,
                          std::unique_ptr<gman::VSPerspective>& viewingSysOut, std::size_t& droppedOut) {
  GMANOptions options;
  options.setFormat(kFloorRes, kFloorRes, 1.0f);
  options.setPixelSamples(1.0f, 1.0f);
  options.setPixelFilter(RiBoxFilter, 1.0f, 1.0f);
  options.setPathtracerSamples(kFloorSamples);
  options.setBackground(GMANColor(0.0f, 0.0f, 0.0f));

  GMANMatrix4 const identity;
  viewingSysOut.reset(
      new gman::VSPerspective(kFloorRes, kFloorRes, squareScreenWindow(), identity, 90.0f, 0.5f, 50.0f));

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
  GMANRaySphere* sphere =
      new GMANRaySphere(kSphereRadius, -kSphereRadius, kSphereRadius, 360.0f, GMANParameterList(), transform);
  GMANLight const areaLight(GMAN_LIGHT_AREA, GMANColor(kAreaLe, kAreaLe, kAreaLe), GMANPoint(), GMANVector());
  gman::Appearance sphereAppearance;
  sphereAppearance.areaLight = &areaLight;
  sphere->setAppearance(sphereAppearance);
  renderer.getWorldManager()->add(sphere);

  frameBufferOut.reset(new GMANFrameBuffer(kFloorRes, kFloorRes, options.getBackground()));
  GMANAttributes const attr;
  renderer.render(frameBufferOut.get(), viewingSysOut.get(), options, attr);
  droppedOut = renderer.droppedPathCount();
}

void testSphereLightOverFloor() {
  std::unique_ptr<GMANFrameBuffer> frameBuffer;
  std::unique_ptr<gman::VSPerspective> viewingSys;
  std::size_t dropped = 0;
  renderAreaLightFloor(frameBuffer, viewingSys, dropped);
  check(dropped == 0, "sphere light over floor: droppedPathCount() is 0");

  std::vector<double> residuals[3];
  std::vector<double> expectedByChannel[3];
  std::size_t measuredCount = 0;
  std::vector<double> alphaValues[3];
  for (int py = 0; py < kFloorRes; ++py) {
    for (int px = 0; px < kFloorRes; ++px) {
      if (!pixelMeasured(*viewingSys, px, py)) {
        continue;
      }
      ++measuredCount;
      GMANColor const expected = analyticAreaLightExpected(*viewingSys, px, py);
      GMANColor const actual = frameBuffer->getPixel(px, py);
      GMANAlpha const alpha = frameBuffer->getAlpha(px, py);
      for (int c = 0; c < 3; ++c) {
        residuals[c].push_back(channel(actual, c) - channel(expected, c));
        expectedByChannel[c].push_back(channel(expected, c));
        alphaValues[c].push_back(channel(alpha, c));
      }
    }
  }
  check(measuredCount >= kMinMeasuredPixels, "sphere light over floor: at least 400 measured pixels");
  checkResiduals(residuals, expectedByChannel, kResidualFloor, "sphere light over floor");

  // D.3: the floor is opaque everywhere a camera ray measures it, so every
  // measured pixel's alpha stays 1, unaffected by the area-light wiring.
  for (int c = 0; c < 3; ++c) {
    GmanMeanStderr const alphaStat = meanStderr(alphaValues[c]);
    check(std::fabs(alphaStat.mean - 1.0) <= 1e-4, "sphere light over floor: alpha stays 1 (unaffected by unit)");
  }
}

// ---- D.2: emitter-hit accounting: direct, after a mirror, after glass ----

constexpr RtFloat kEmitterCentreZ = 8.0f;
constexpr RtFloat kEmitterRadius = 2.0f;
// A margin the selection queries alone use, so a boundary pixel whose four
// corners disagree on the true silhouette is excluded rather than
// contaminating either measured set with a partial value.
constexpr double kSelectionMargin = 0.05;

bool sphereHitDouble(GMANRay const& ray, double cx, double cy, double cz, double radius) {
  GMANVector const d = ray.getDirection();
  double const ocx = -cx, ocy = -cy, ocz = -cz; // ray origin is always the camera-space eye, (0, 0, 0)
  double const dx = (double)d.getX(), dy = (double)d.getY(), dz = (double)d.getZ();
  double const b = 2.0 * (dx * ocx + dy * ocy + dz * ocz);
  double const c = ocx * ocx + ocy * ocy + ocz * ocz - radius * radius;
  double const disc = b * b - 4.0 * c;
  if (disc < 0.0) {
    return false;
  }
  double const sq = std::sqrt(disc);
  double const t0 = (-b - sq) / 2.0;
  double const t1 = (-b + sq) / 2.0;
  return t0 > 1e-6 || t1 > 1e-6;
}

// D.2's direct case.
constexpr RtInt kDirectRes = 41;
constexpr RtInt kDirectSamples = 4;

bool cornersAllHitEmitter(gman::VSPerspective& viewingSys, int px, int py) {
  for (int dy = 0; dy <= 1; ++dy) {
    for (int dx = 0; dx <= 1; ++dx) {
      GMANRay const corner = viewingSys.cameraRay((RtFloat)(px + dx), (RtFloat)(py + dy));
      if (!sphereHitDouble(corner, 0.0, 0.0, (double)kEmitterCentreZ,
                           (double)kEmitterRadius * (1.0 - kSelectionMargin))) {
        return false;
      }
    }
  }
  return true;
}

bool cornersAllMissEmitter(gman::VSPerspective& viewingSys, int px, int py) {
  for (int dy = 0; dy <= 1; ++dy) {
    for (int dx = 0; dx <= 1; ++dx) {
      GMANRay const corner = viewingSys.cameraRay((RtFloat)(px + dx), (RtFloat)(py + dy));
      if (sphereHitDouble(corner, 0.0, 0.0, (double)kEmitterCentreZ,
                          (double)kEmitterRadius * (1.0 + kSelectionMargin))) {
        return false;
      }
    }
  }
  return true;
}

GMANRaySphere* addEmitterSphere(GMANPathtraceRenderer& renderer, GMANLight const& areaLight) {
  GMANMatrix4 place;
  place.trans(0.0, 0.0, kEmitterCentreZ);
  GMANTransform const transform = makeTransform(place);
  GMANRaySphere* sphere =
      new GMANRaySphere(kEmitterRadius, -kEmitterRadius, kEmitterRadius, 360.0f, GMANParameterList(), transform);
  gman::Appearance appearance;
  appearance.areaLight = &areaLight;
  appearance.Cs = GMANColor(0.0f, 0.0f, 0.0f);
  appearance.Os = GMANColor(1.0f, 1.0f, 1.0f);
  sphere->setAppearance(appearance);
  renderer.getWorldManager()->add(sphere);
  return sphere;
}

void testDirectHit() {
  GMANOptions options;
  options.setFormat(kDirectRes, kDirectRes, 1.0f);
  options.setPixelSamples(1.0f, 1.0f);
  options.setPixelFilter(RiBoxFilter, 1.0f, 1.0f);
  options.setPathtracerSamples(kDirectSamples);
  options.setBackground(GMANColor(0.0f, 0.0f, 0.0f));

  GMANMatrix4 const identity;
  gman::VSPerspective viewingSys(kDirectRes, kDirectRes, squareScreenWindow(), identity, 90.0f, 0.5f, 50.0f);

  GMANPathtraceRenderer renderer;
  GMANLight const areaLight(GMAN_LIGHT_AREA, GMANColor(kAreaLe, kAreaLe, kAreaLe), GMANPoint(), GMANVector());
  addEmitterSphere(renderer, areaLight);

  GMANFrameBuffer frameBuffer(kDirectRes, kDirectRes, options.getBackground());
  GMANAttributes const attr;
  renderer.render(&frameBuffer, &viewingSys, options, attr);
  check(renderer.droppedPathCount() == 0, "direct hit: droppedPathCount() is 0");

  std::size_t onCount = 0, offCount = 0;
  bool onExact = true, offExact = true;
  for (int py = 0; py < kDirectRes; ++py) {
    for (int px = 0; px < kDirectRes; ++px) {
      GMANColor const actual = frameBuffer.getPixel(px, py);
      if (cornersAllHitEmitter(viewingSys, px, py)) {
        ++onCount;
        if (std::fabs(actual.getRed() - kAreaLe) > 1e-4 || std::fabs(actual.getGreen() - kAreaLe) > 1e-4 ||
            std::fabs(actual.getBlue() - kAreaLe) > 1e-4) {
          onExact = false;
        }
      } else if (cornersAllMissEmitter(viewingSys, px, py)) {
        ++offCount;
        if (actual.getRed() != 0.0f || actual.getGreen() != 0.0f || actual.getBlue() != 0.0f) {
          offExact = false;
        }
      }
    }
  }
  check(onCount >= 4, "direct hit: at least 4 pixels squarely on the emitter");
  check(offCount >= 4, "direct hit: at least 4 pixels squarely off the emitter");
  check(onExact, "direct hit: every on-emitter pixel reads Le exactly");
  check(offExact, "direct hit: every off-emitter pixel reads black exactly");
}

// D.2's after-a-mirror case.
constexpr RtFloat kMirrorCentreX = 4.7f;
constexpr RtFloat kMirrorCentreZ = 1.0f;
constexpr RtFloat kMirrorRadius = 3.3f;
constexpr RtInt kMirrorRes = 81;
constexpr RtInt kMirrorSamples = 64;
constexpr std::size_t kMinMirrorPixels = 8;
constexpr double kMirrorTolerance = 0.01; // 1% relative
// cos(angle from the surface normal) a mirror or emitter hit must clear to
// count: excludes a grazing hit near either sphere's own silhouette, where a
// pixel's four corners can straddle the true edge, without the distortion a
// concentric-radius shrink would introduce into the reflected direction
// itself.
constexpr RtFloat kGrazingCosine = (RtFloat)0.3;

GMANVector reflectDirection(GMANVector const& incoming, GMANVector const& normal) {
  return incoming - normal * ((RtFloat)2.0 * incoming.dot(normal));
}

// Whether cameraRay reflects, through mirror's own real intersection and a
// plain reflect formula, onto emitter, clear of a grazing hit on either
// sphere.
bool reflectsOntoEmitter(GMANRaySphere const& mirror, GMANRaySphere const& emitter, GMANRay const& cameraRay) {
  GMANHit mirrorHit;
  if (!mirror.intersect(cameraRay, mirrorHit)) {
    return false;
  }
  if (-cameraRay.getDirection().dot(mirrorHit.normal) < kGrazingCosine) {
    return false;
  }
  GMANVector const reflected = reflectDirection(cameraRay.getDirection(), mirrorHit.normal);
  GMANPoint const origin =
      gman::offsetOrigin(mirrorHit.point, mirrorHit.normal, reflected, gman::primitiveMagnitude(mirror.getBBox()));
  GMANRay const secondary(origin, reflected);
  GMANHit emitterHit;
  if (!emitter.intersect(secondary, emitterHit)) {
    return false;
  }
  return -reflected.dot(emitterHit.normal) >= kGrazingCosine;
}

bool cornersAllReflectOntoEmitter(gman::VSPerspective& viewingSys, GMANRaySphere const& selectionMirror,
                                  GMANRaySphere const& selectionEmitter, int px, int py) {
  for (int dy = 0; dy <= 1; ++dy) {
    for (int dx = 0; dx <= 1; ++dx) {
      GMANRay const corner = viewingSys.cameraRay((RtFloat)(px + dx), (RtFloat)(py + dy));
      if (!reflectsOntoEmitter(selectionMirror, selectionEmitter, corner)) {
        return false;
      }
    }
  }
  return true;
}

void testMirrorBounce() {
  GMANOptions options;
  options.setFormat(kMirrorRes, kMirrorRes, 1.0f);
  options.setPixelSamples(1.0f, 1.0f);
  options.setPixelFilter(RiBoxFilter, 1.0f, 1.0f);
  options.setPathtracerSamples(kMirrorSamples);
  options.setBackground(GMANColor(0.0f, 0.0f, 0.0f));

  GMANMatrix4 const identity;
  gman::VSPerspective viewingSys(kMirrorRes, kMirrorRes, squareScreenWindow(), identity, 90.0f, 0.5f, 50.0f);

  GMANPathtraceRenderer renderer;
  GMANLight const areaLight(GMAN_LIGHT_AREA, GMANColor(kAreaLe, kAreaLe, kAreaLe), GMANPoint(), GMANVector());
  GMANRaySphere* emitter = addEmitterSphere(renderer, areaLight);

  GMANMatrix4 mirrorPlace;
  mirrorPlace.trans(kMirrorCentreX, 0.0, kMirrorCentreZ);
  GMANTransform const mirrorTransform = makeTransform(mirrorPlace);
  GMANRaySphere* mirror =
      new GMANRaySphere(kMirrorRadius, -kMirrorRadius, kMirrorRadius, 360.0f, GMANParameterList(), mirrorTransform);
  gman::Appearance mirrorAppearance;
  mirrorAppearance.shader = loadShader("mirror", GMANParameterList());
  mirrorAppearance.Cs = GMANColor(1.0f, 1.0f, 1.0f);
  mirrorAppearance.Os = GMANColor(1.0f, 1.0f, 1.0f);
  mirror->setAppearance(mirrorAppearance);
  renderer.getWorldManager()->add(mirror);

  GMANFrameBuffer frameBuffer(kMirrorRes, kMirrorRes, options.getBackground());
  GMANAttributes const attr;
  renderer.render(&frameBuffer, &viewingSys, options, attr);
  check(renderer.droppedPathCount() == 0, "mirror bounce: droppedPathCount() is 0");

  // mirror and emitter are the very primitives the render just traced: the
  // selection query reuses their own real intersection, with no separate,
  // distorting copy of either sphere.
  std::vector<double> reflectedValues[3];
  std::size_t reflectedCount = 0;
  for (int py = 0; py < kMirrorRes; ++py) {
    for (int px = 0; px < kMirrorRes; ++px) {
      if (!cornersAllReflectOntoEmitter(viewingSys, *mirror, *emitter, px, py)) {
        continue;
      }
      ++reflectedCount;
      GMANColor const actual = frameBuffer.getPixel(px, py);
      reflectedValues[0].push_back(actual.getRed());
      reflectedValues[1].push_back(actual.getGreen());
      reflectedValues[2].push_back(actual.getBlue());
    }
  }
  check(reflectedCount >= kMinMirrorPixels, "mirror bounce: enough pixels reflect the emitter into view");
  std::printf("mirror bounce: %zu pixel(s) measured\n", reflectedCount);

  char const* const channelName[3] = {"red", "green", "blue"};
  for (int c = 0; c < 3; ++c) {
    GmanMeanStderr const stat = meanStderr(reflectedValues[c]);
    std::printf("mirror bounce %s: mean %.6f, expected %.6f\n", channelName[c], stat.mean, (double)kAreaLe);
    checkNear(stat.mean, (double)kAreaLe, stat.stderrOfMean, kMirrorTolerance * (double)kAreaLe,
              std::string("mirror bounce: the ") + channelName[c] + " channel's mean is within 5 sigma of Le");
  }
}

// D.2's after-glass case.
constexpr RtInt kGlassRes = 81;
constexpr RtInt kGlassSamples = 64;
constexpr double kGlassConeRadians = 0.05;
constexpr double kFresnelNormal = 0.04; // exact Fresnel at normal incidence, index 1 -> 1.5
constexpr double kGlassTolerance = 0.01;
constexpr RtFloat kPaneZNear = 4.0f;
constexpr RtFloat kPaneZFar = 4.02f;
constexpr RtFloat kPaneHalfExtent = 0.5f;

std::vector<GMANPoint> paneAt(RtFloat z, bool reversedWinding) {
  std::vector<GMANPoint> verts = {
      GMANPoint(-kPaneHalfExtent, -kPaneHalfExtent, z), GMANPoint(-kPaneHalfExtent, kPaneHalfExtent, z),
      GMANPoint(kPaneHalfExtent, kPaneHalfExtent, z), GMANPoint(kPaneHalfExtent, -kPaneHalfExtent, z)};
  if (reversedWinding) {
    std::reverse(verts.begin(), verts.end());
  }
  return verts;
}

bool cornersWithinAxisCone(gman::VSPerspective& viewingSys, int px, int py) {
  double const cosMax = std::cos(kGlassConeRadians);
  for (int dy = 0; dy <= 1; ++dy) {
    for (int dx = 0; dx <= 1; ++dx) {
      GMANRay const corner = viewingSys.cameraRay((RtFloat)(px + dx), (RtFloat)(py + dy));
      if ((double)corner.getDirection().getZ() < cosMax) {
        return false;
      }
    }
  }
  return true;
}

void testGlassTransmission() {
  GMANOptions options;
  options.setFormat(kGlassRes, kGlassRes, 1.0f);
  options.setPixelSamples(1.0f, 1.0f);
  options.setPixelFilter(RiBoxFilter, 1.0f, 1.0f);
  options.setPathtracerSamples(kGlassSamples);
  options.setBackground(GMANColor(0.0f, 0.0f, 0.0f));

  GMANMatrix4 const identity;
  gman::VSPerspective viewingSys(kGlassRes, kGlassRes, squareScreenWindow(), identity, 90.0f, 0.5f, 50.0f);

  GMANPathtraceRenderer renderer;
  GMANLight const areaLight(GMAN_LIGHT_AREA, GMANColor(kAreaLe, kAreaLe, kAreaLe), GMANPoint(), GMANVector());
  addEmitterSphere(renderer, areaLight);

  gman::Appearance glassAppearance;
  glassAppearance.shader = loadShader("glass", GMANParameterList());
  glassAppearance.Os = GMANColor(1.0f, 1.0f, 1.0f);

  GMANRayPolygon* near = new GMANRayPolygon(paneAt(kPaneZNear, false), GMANParameterList());
  near->setAppearance(glassAppearance);
  renderer.getWorldManager()->add(near);
  GMANRayPolygon* far = new GMANRayPolygon(paneAt(kPaneZFar, true), GMANParameterList());
  far->setAppearance(glassAppearance);
  renderer.getWorldManager()->add(far);

  GMANFrameBuffer frameBuffer(kGlassRes, kGlassRes, options.getBackground());
  GMANAttributes const attr;
  renderer.render(&frameBuffer, &viewingSys, options, attr);
  check(renderer.droppedPathCount() == 0, "glass transmission: droppedPathCount() is 0");

  std::vector<double> transmittedValues[3];
  std::size_t transmittedCount = 0;
  for (int py = 0; py < kGlassRes; ++py) {
    for (int px = 0; px < kGlassRes; ++px) {
      if (!cornersWithinAxisCone(viewingSys, px, py)) {
        continue;
      }
      ++transmittedCount;
      GMANColor const actual = frameBuffer.getPixel(px, py);
      transmittedValues[0].push_back(actual.getRed());
      transmittedValues[1].push_back(actual.getGreen());
      transmittedValues[2].push_back(actual.getBlue());
    }
  }
  check(transmittedCount >= 1, "glass transmission: at least one near-normal pixel measured");
  std::printf("glass transmission: %zu pixel(s) measured\n", transmittedCount);

  double const expected = (double)kAreaLe * (1.0 - kFresnelNormal) * (1.0 - kFresnelNormal);
  char const* const channelName[3] = {"red", "green", "blue"};
  for (int c = 0; c < 3; ++c) {
    GmanMeanStderr const stat = meanStderr(transmittedValues[c]);
    std::printf("glass transmission %s: mean %.6f, expected %.6f\n", channelName[c], stat.mean, expected);
    checkNear(stat.mean, expected, stat.stderrOfMean, kGlassTolerance * expected,
              std::string("glass transmission: the ") + channelName[c] +
                  " channel's mean is within 5 sigma of Le*(1-F)^2");
  }
}

} // namespace

int main() {
  testSphereLightOverFloor();
  testDirectHit();
  testMirrorBounce();
  testGlassTransmission();

  return checkSummary("the path tracer's own area-light behaviour: a sphere light over the floor matches its "
                      "closed form, and emitter-hit accounting covers a direct, mirrored and transmitted view of "
                      "the same emitter alike");
}
