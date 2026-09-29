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
 * The path tracer's own emitter-hit accounting: a camera ray, a mirror
 * bounce and a glass transmission each read Le on an area light without
 * double-counting what next-event estimation already estimated.
 */

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "check.h"
#include "gmanattributes.h"
#include "gmanframebuffer.h"
#include "gmanlightsourcemgr.h"
#include "gmanmath.h"
#include "gmanmatrix4.h"
#include "gmanoptions.h"
#include "gmanparameterlist.h"
#include "gmanpathtracerenderer.h"
#include "gmanpoint.h"
#include "gmanray.h"
#include "gmanraybbox.h"
#include "gmanraydisk.h"
#include "gmanrayoccluder.h"
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

// The direct case.
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
  const auto sphere =
      new GMANRaySphere(kEmitterRadius, -kEmitterRadius, kEmitterRadius, 360.0f, GMANParameterList(), transform);
  gman::Appearance appearance;
  appearance.areaLight = &areaLight;
  appearance.Cs = GMANColor(0.0f, 0.0f, 0.0f);
  appearance.Os = GMANColor(1.0f, 1.0f, 1.0f);
  sphere->setAppearance(appearance);
  renderer.getWorldManager()->add(sphere);
  return sphere;
}

// A disk beside the emitter sphere, rotated 180 degrees about the x-axis so
// its own fixed (0, 0, -1) object-space normal places at (0, 0, 1) in camera
// space -- away from the camera, its own back turned toward every camera
// ray that reaches it -- while staying in the same camera-space z == height
// plane a bare translation alone would have.
constexpr RtFloat kBackDiskCentreX = 6.0f;
constexpr RtFloat kBackDiskRadius = 1.0f;

bool diskHitDouble(GMANRay const& ray, double cx, double planeZ, double radius) {
  GMANVector const d = ray.getDirection();
  double const dz = (double)d.getZ();
  if (dz == 0.0) {
    return false;
  }
  double const t = planeZ / dz; // ray origin is always the camera-space eye, (0, 0, 0)
  if (t <= 0.0) {
    return false;
  }
  double const x = t * (double)d.getX() - cx;
  double const y = t * (double)d.getY();
  return x * x + y * y <= radius * radius;
}

bool cornersAllOnBackDisk(gman::VSPerspective& viewingSys, int px, int py) {
  for (int dy = 0; dy <= 1; ++dy) {
    for (int dx = 0; dx <= 1; ++dx) {
      GMANRay const corner = viewingSys.cameraRay((RtFloat)(px + dx), (RtFloat)(py + dy));
      if (!diskHitDouble(corner, (double)kBackDiskCentreX, (double)kEmitterCentreZ,
                         (double)kBackDiskRadius * (1.0 - kSelectionMargin))) {
        return false;
      }
    }
  }
  return true;
}

GMANRayDisk* addBackFacingDisk(GMANPathtraceRenderer& renderer, GMANLight const& areaLight) {
  GMANMatrix4 place;
  place.rot(GMANRadians(180.0f), 1.0f, 0.0f, 0.0f);
  place.trans(kBackDiskCentreX, 0.0f, kEmitterCentreZ);
  GMANTransform const transform = makeTransform(place);
  const auto disk = new GMANRayDisk(0.0f, kBackDiskRadius, 360.0f, GMANParameterList(), transform);
  gman::Appearance appearance;
  appearance.areaLight = &areaLight;
  appearance.Cs = GMANColor(0.0f, 0.0f, 0.0f);
  appearance.Os = GMANColor(1.0f, 1.0f, 1.0f);
  disk->setAppearance(appearance);
  renderer.getWorldManager()->add(disk);
  return disk;
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
  GMANLight const backDiskLight(GMAN_LIGHT_AREA, GMANColor(kAreaLe, kAreaLe, kAreaLe), GMANPoint(), GMANVector());
  addBackFacingDisk(renderer, backDiskLight);

  GMANFrameBuffer frameBuffer(kDirectRes, kDirectRes, options.getBackground());
  GMANAttributes const attr;
  renderer.render(&frameBuffer, &viewingSys, options, attr);
  check(renderer.droppedPathCount() == 0, "direct hit: droppedPathCount() is 0");

  std::size_t onCount = 0, offCount = 0, backCount = 0;
  bool onExact = true, offExact = true, backExact = true;
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
      if (cornersAllOnBackDisk(viewingSys, px, py)) {
        ++backCount;
        if (actual.getRed() != 0.0f || actual.getGreen() != 0.0f || actual.getBlue() != 0.0f) {
          backExact = false;
        }
      }
    }
  }
  check(onCount >= 4, "direct hit: at least 4 pixels squarely on the emitter");
  check(offCount >= 4, "direct hit: at least 4 pixels squarely off the emitter");
  check(onExact, "direct hit: every on-emitter pixel reads Le exactly");
  check(offExact, "direct hit: every off-emitter pixel reads black exactly");
  check(backCount >= 4, "direct hit: at least 4 pixels squarely on the back-facing disk");
  check(backExact, "direct hit: every pixel on the back-facing disk reads black exactly");
}

// The after-a-mirror case.
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
  const auto mirror =
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

// The after-glass case.
constexpr RtInt kGlassRes = 81;
constexpr RtInt kGlassSamples = 64;
constexpr double kGlassConeRadians = 0.12;
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

  const auto near = new GMANRayPolygon(paneAt(kPaneZNear, false), GMANParameterList());
  near->setAppearance(glassAppearance);
  renderer.getWorldManager()->add(near);
  const auto far = new GMANRayPolygon(paneAt(kPaneZFar, true), GMANParameterList());
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

  // The pane's own internal reflections sum to Le*(1-F)/(1+F): each of the
  // infinitely many internal bounces between the near and far surface
  // contributes Le*(1-F)^2*F^(2n), a geometric series in F^2 that sums to
  // (1-F)^2/(1-F^2) = (1-F)/(1+F), the index-squared term cancelling across
  // the slab.
  double const expected = (double)kAreaLe * (1.0 - kFresnelNormal) / (1.0 + kFresnelNormal);
  char const* const channelName[3] = {"red", "green", "blue"};
  for (int c = 0; c < 3; ++c) {
    GmanMeanStderr const stat = meanStderr(transmittedValues[c]);
    std::printf("glass transmission %s: mean %.6f, expected %.6f\n", channelName[c], stat.mean, expected);
    checkNear(stat.mean, expected, stat.stderrOfMean, kGlassTolerance * expected,
              std::string("glass transmission: the ") + channelName[c] +
                  " channel's mean is within 5 sigma of Le*(1-F)/(1+F)");
  }
}

} // namespace

int main() {
  testDirectHit();
  testMirrorBounce();
  testGlassTransmission();

  return checkSummary("emitter-hit accounting covers a direct, mirrored and transmitted view of the same emitter "
                      "alike");
}
