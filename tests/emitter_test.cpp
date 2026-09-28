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
 * gman::emitters enumerates a world's own delta and area lights alike;
 * gman::sample and gman::samplePoint answer both kinds through one
 * interface, a delta forwarding to GMANLight::sample unchanged and an area
 * emitter sampling its own shape and placing it by its object-to-camera
 * transform. Also proves appearanceOf's own exclusion: an illuminated area
 * light never reaches Appearance::lights.
 */

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "check.h"
#include "gmanattributes.h"
#include "gmanemitter.h"
#include "gmanlightsourcemgr.h"
#include "gmanlinearworldmanager.h"
#include "gmanlog.h"
#include "gmanmath.h"
#include "gmanmatrix4.h"
#include "gmanparameterlist.h"
#include "gmanpoint.h"
#include "gmanraydisk.h"
#include "gmanraypolygon.h"
#include "gmanraysphere.h"
#include "gmansampling.h"
#include "gmanshading.h"
#include "gmantransform.h"
#include "gmanvector.h"
#include "maketransform.h"
#include "ri.h"
#include "samplingstats.h"

namespace {

constexpr std::uint32_t kSeed = 0x8d1c8e21u;
constexpr double kPi = 3.14159265358979323846;

std::string readFile(std::string const& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream contents;
  contents << in.rdbuf();
  return contents.str();
}

// Builds a world: an eligible sphere emitter, an ineligible (wrong
// shape) polygon and a degenerate (zero-radius) sphere, each tagged as an
// area light, and two ordinary spheres sharing one hand-built point light
// -- the second exercises emitters' own deduplication by pointer, since a
// single primitive could never tell a missing dedup from a correct one.
// Returns the world and every hand-built object it owns, all outliving it
// for the caller's own inspection.
struct EmitterWorld {
  GMANLinearWorldManager world;
  GMANRaySphere* sphere;
  GMANLight sphereLight{GMAN_LIGHT_AREA, GMANColor(4.0f, 4.0f, 4.0f), GMANPoint(), GMANVector()};
  GMANRayPolygon* polygon;
  GMANLight polygonLight{GMAN_LIGHT_AREA, GMANColor(4.0f, 4.0f, 4.0f), GMANPoint(), GMANVector()};
  GMANRaySphere* degenerate;
  GMANLight degenerateLight{GMAN_LIGHT_AREA, GMANColor(4.0f, 4.0f, 4.0f), GMANPoint(), GMANVector()};
  GMANRaySphere* ordinary;
  GMANRaySphere* secondOrdinary;
  GMANLight pointLight{GMAN_LIGHT_POINT, GMANColor(5.0f, 5.0f, 5.0f), GMANPoint(0.0f, 0.0f, -5.0f), GMANVector()};
};

std::unique_ptr<EmitterWorld> buildEmitterWorld() {
  auto w = std::make_unique<EmitterWorld>();

  w->sphere = new GMANRaySphere(2.0f, -2.0f, 2.0f, 360.0f, GMANParameterList());
  gman::Appearance sphereAppearance;
  sphereAppearance.areaLight = &w->sphereLight;
  w->sphere->setAppearance(sphereAppearance);
  w->world.add(w->sphere);

  std::vector<GMANPoint> const polyVerts = {GMANPoint(-1.0f, -1.0f, 10.0f), GMANPoint(1.0f, -1.0f, 10.0f),
                                            GMANPoint(1.0f, 1.0f, 10.0f), GMANPoint(-1.0f, 1.0f, 10.0f)};
  w->polygon = new GMANRayPolygon(polyVerts, GMANParameterList());
  gman::Appearance polygonAppearance;
  polygonAppearance.areaLight = &w->polygonLight;
  w->polygon->setAppearance(polygonAppearance);
  w->world.add(w->polygon);

  w->degenerate = new GMANRaySphere(0.0f, -1.0f, 1.0f, 360.0f, GMANParameterList());
  gman::Appearance degenerateAppearance;
  degenerateAppearance.areaLight = &w->degenerateLight;
  w->degenerate->setAppearance(degenerateAppearance);
  w->world.add(w->degenerate);

  w->ordinary = new GMANRaySphere(1.0f, -1.0f, 1.0f, 360.0f, GMANParameterList());
  gman::Appearance ordinaryAppearance;
  ordinaryAppearance.lights = {&w->pointLight};
  w->ordinary->setAppearance(ordinaryAppearance);
  w->world.add(w->ordinary);

  w->secondOrdinary = new GMANRaySphere(1.0f, -1.0f, 1.0f, 360.0f, GMANParameterList());
  w->secondOrdinary->setAppearance(ordinaryAppearance);
  w->world.add(w->secondOrdinary);

  return w;
}

// Enumeration, and the one warning naming both ineligible primitives.
void testEnumeration() {
  std::string const logPath = "emitter_enumeration.log";
  std::remove(logPath.c_str());
  setLogFile(logPath.c_str());
  setScreenOutput(false);

  std::unique_ptr<EmitterWorld> const w = buildEmitterWorld();
  std::vector<gman::Emitter> const list = gman::emitters(w->world);

  setLogFile("/dev/null");
  setScreenOutput(true);

  check(list.size() == 2, "enumeration: exactly two emitters, the ineligible polygon and degenerate sphere excluded "
                          "and the shared point light deduplicated");

  bool foundDelta = false, foundArea = false;
  for (gman::Emitter const& e : list) {
    if (e.shape == nullptr && e.light == &w->pointLight) {
      foundDelta = true;
    }
    if (e.shape == w->sphere && e.light == &w->sphereLight) {
      foundArea = true;
    }
  }
  check(foundDelta, "enumeration: the point light is one delta emitter");
  check(foundArea, "enumeration: the sphere is one area emitter naming its own light");

  std::string const needle = "2 area-light primitive(s)";
  std::string const log = readFile(logPath);
  std::size_t occurrences = 0;
  for (std::size_t pos = log.find(needle); pos != std::string::npos; pos = log.find(needle, pos + 1)) {
    ++occurrences;
  }
  check(occurrences == 1, "enumeration: the warning names the count, 2 area-light primitive(s), exactly once");
}

// power().
void testPower() {
  // GMANLinearWorldManager owns and deletes what it is given, so the
  // sphere here -- unlike the others below, read but never added to a
  // world -- is heap-allocated.
  const auto fullSphere = new GMANRaySphere(2.0f, -2.0f, 2.0f, 360.0f, GMANParameterList());
  GMANLight const light(GMAN_LIGHT_AREA, GMANColor(4.0f, 4.0f, 4.0f), GMANPoint(), GMANVector());
  gman::Appearance appearance;
  appearance.areaLight = &light;
  fullSphere->setAppearance(appearance);

  GMANLinearWorldManager world;
  world.add(fullSphere);
  std::vector<gman::Emitter> const list = gman::emitters(world);
  check(list.size() == 1, "power: one area emitter enumerated");

  double const area = 4.0 * kPi * 2.0 * 2.0;
  double const expectedPower = 4.0 * area * kPi;
  double const actualPower = (double)list[0].power;
  double const relPower = std::fabs(actualPower - expectedPower) / expectedPower;
  std::printf("power: closed form %.6f, mean(Le)*area()*pi %.6f (%.2e relative)\n", actualPower, expectedPower,
              relPower);
  check(relPower < 1e-4, "power: the area emitter's power is within 1e-4 relative of mean(Le)*area()*pi");

  // An independent quadrature integral of Le * cosTheta over the emitting
  // hemisphere at a fixed point, times area(), cross-checks the same
  // formula without going through it.
  constexpr std::uint32_t kDraws = 1u << 16;
  double sum = 0.0;
  for (std::uint32_t i = 0; i < kDraws; ++i) {
    gman::Sample2D const uv = gman::sample2D(kSeed, 0, 0, i, kDraws, 0u);
    GMANVector const dir = gman::uniformHemisphere(uv.u1, uv.u2);
    double const cosTheta = (double)dir.getZ();
    sum += 4.0 * cosTheta / (double)gman::uniformHemispherePdf();
  }
  double const hemisphereIntegral = sum / (double)kDraws;
  double const quadraturePower = hemisphereIntegral * area;
  double const relQuad = std::fabs(quadraturePower - actualPower) / actualPower;
  std::printf("power: quadrature %.6f vs power() %.6f (%.2e relative)\n", quadraturePower, actualPower, relQuad);
  check(relQuad < 1e-3, "power: the quadrature integral agrees with power within 1e-3 relative");

  // A delta emitter's power is exactly 0, from gman::emitters itself.
  const auto deltaSphere = new GMANRaySphere(1.0f, -1.0f, 1.0f, 360.0f, GMANParameterList());
  GMANLight const pointLight(GMAN_LIGHT_POINT, GMANColor(1.0f, 1.0f, 1.0f), GMANPoint(), GMANVector());
  gman::Appearance deltaAppearance;
  deltaAppearance.lights = {&pointLight};
  deltaSphere->setAppearance(deltaAppearance);

  GMANLinearWorldManager deltaWorld;
  deltaWorld.add(deltaSphere);
  std::vector<gman::Emitter> const deltaList = gman::emitters(deltaWorld);
  check(deltaList.size() == 1, "power: one delta emitter enumerated");
  check(deltaList[0].power == 0.0f, "power: a delta emitter's power is exactly 0");
}

// sample, delta.
void testSampleDelta() {
  GMANLight const pointLight(GMAN_LIGHT_POINT, GMANColor(5.0f, 5.0f, 5.0f), GMANPoint(0.0f, 0.0f, -5.0f), GMANVector());
  gman::Emitter const emitter{&pointLight, nullptr, 0.0f};
  GMANPoint const p(1.0f, 0.0f, 0.0f);

  GMANVector directL;
  GMANColor directCl;
  pointLight.sample(p, directL, directCl);
  directL.normalize();

  constexpr std::uint32_t kDraws = 1u << 8;
  bool allMatch = true;
  for (std::uint32_t i = 0; i < kDraws; ++i) {
    gman::Sample2D const uv = gman::sample2D(kSeed, 1, 0, i, kDraws, 0u);
    gman::EmitterSample const s = gman::sample(emitter, p, uv.u1, uv.u2);
    bool const wiMatches = std::fabs(s.wi.getX() - directL.getX()) < 1e-6 &&
                           std::fabs(s.wi.getY() - directL.getY()) < 1e-6 &&
                           std::fabs(s.wi.getZ() - directL.getZ()) < 1e-6;
    bool const clMatches = std::fabs(s.Cl.getRed() - directCl.getRed() * (RtFloat)kPi) < 1e-4 &&
                           std::fabs(s.Cl.getGreen() - directCl.getGreen() * (RtFloat)kPi) < 1e-4 &&
                           std::fabs(s.Cl.getBlue() - directCl.getBlue() * (RtFloat)kPi) < 1e-4;
    if (!wiMatches || !clMatches || s.pdf != 1.0f || !s.isDelta) {
      allMatch = false;
    }
  }
  check(allMatch, "sample delta: every draw matches light->sample scaled by pi, pdf 1, isDelta true");
}

// sample, area.
void testSampleArea() {
  GMANRaySphere sphere(2.0f, -2.0f, 2.0f, 360.0f, GMANParameterList());
  GMANLight const light(GMAN_LIGHT_AREA, GMANColor(4.0f, 4.0f, 4.0f), GMANPoint(), GMANVector());
  gman::Appearance appearance;
  appearance.areaLight = &light;
  sphere.setAppearance(appearance);
  gman::Emitter const emitter{&light, &sphere, 0.0f};

  // p outside the sphere, along -z: the sphere's own centre is at the
  // origin, radius 2, so d = 10 and N (p -> centre) is +z.
  GMANPoint const p(0.0f, 0.0f, -10.0f);
  double const d = 10.0;
  double const radius = 2.0;
  GMANVector const receiverNormal(0.0f, 0.0f, 1.0f);

  constexpr std::uint32_t kDraws = 1u << 16;
  std::vector<double> terms;
  terms.reserve(kDraws);
  bool wiWithinBoundingRegion = true;
  for (std::uint32_t i = 0; i < kDraws; ++i) {
    gman::Sample2D const uv = gman::sample2D(kSeed, 2, 0, i, kDraws, 0u);
    gman::EmitterSample const s = gman::sample(emitter, p, uv.u1, uv.u2);
    if (!(s.distance >= d - radius - 1e-4 && s.distance <= d + radius + 1e-4)) {
      wiWithinBoundingRegion = false;
    }
    double const term = (s.pdf > 0.0f)
                            ? std::fabs((double)s.wi.dot(receiverNormal)) *
                                  (double)((s.Cl.getRed() + s.Cl.getGreen() + s.Cl.getBlue()) / 3.0f) / (double)s.pdf
                            : 0.0;
    terms.push_back(term);
  }
  check(wiWithinBoundingRegion, "sample area: every draw's distance lies within the sphere's bounding region");

  GmanMeanStderr const stat = meanStderr(terms);
  double const expected = 4.0 * kPi * (radius / d) * (radius / d);
  std::printf("sample area: mean %.6f (%.3f sigma from %.6f)\n", stat.mean,
              stat.stderrOfMean > 0.0 ? (stat.mean - expected) / stat.stderrOfMean : 0.0, expected);
  checkNear(stat.mean, expected, stat.stderrOfMean, 1e-4,
            "sample area: the cosine-weighted mean matches Le*pi*(radius/d)^2 within 5 sigma");

  // p at the sphere's own centre: every sampled normal points radially
  // outward, directly away from p, so cosTheta is negative everywhere and
  // Cl is black on every draw.
  GMANPoint const centre(0.0f, 0.0f, 0.0f);
  bool allBlack = true;
  for (std::uint32_t i = 0; i < 1024; ++i) {
    gman::Sample2D const uv = gman::sample2D(kSeed, 3, 0, i, 1024u, 0u);
    gman::EmitterSample const s = gman::sample(emitter, centre, uv.u1, uv.u2);
    if (s.Cl.getRed() != 0.0f || s.Cl.getGreen() != 0.0f || s.Cl.getBlue() != 0.0f) {
      allBlack = false;
    }
  }
  check(allBlack, "sample area: a p on the non-emitting side reports Cl black on every draw");
}

// A sphere of object-space radius 1, uniformly scaled: its own irradiance
// and power both track the placed radius scale*1, not the object-space
// radius alone.
void testScaledSphere(RtFloat scale, std::uint32_t dim) {
  constexpr RtFloat kLe = 10.0f;
  GMANMatrix4 place;
  place.scale(scale, scale, scale);
  GMANTransform const transform = makeTransform(place);
  const auto sphere = new GMANRaySphere(1.0f, -1.0f, 1.0f, 360.0f, GMANParameterList(), transform);
  GMANLight const light(GMAN_LIGHT_AREA, GMANColor(kLe, kLe, kLe), GMANPoint(), GMANVector());
  gman::Appearance appearance;
  appearance.areaLight = &light;
  sphere->setAppearance(appearance);

  GMANLinearWorldManager world;
  world.add(sphere);
  std::vector<gman::Emitter> const list = gman::emitters(world);
  check(list.size() == 1, "scaled sphere: one area emitter enumerated");

  GMANPoint const p(0.0f, 0.0f, -10.0f);
  GMANVector const receiverNormal(0.0f, 0.0f, 1.0f);
  double const d = 10.0;

  constexpr std::uint32_t kDraws = 1u << 16;
  std::vector<double> terms;
  terms.reserve(kDraws);
  for (std::uint32_t i = 0; i < kDraws; ++i) {
    gman::Sample2D const uv = gman::sample2D(kSeed, dim, 0, i, kDraws, 0u);
    gman::EmitterSample const s = gman::sample(list[0], p, uv.u1, uv.u2);
    double const term = (s.pdf > 0.0f)
                            ? std::fabs((double)s.wi.dot(receiverNormal)) *
                                  (double)((s.Cl.getRed() + s.Cl.getGreen() + s.Cl.getBlue()) / 3.0f) / (double)s.pdf
                            : 0.0;
    terms.push_back(term);
  }
  GmanMeanStderr const stat = meanStderr(terms);
  double const placedRadius = (double)scale * 1.0;
  double const expected = kPi * (double)kLe * placedRadius * placedRadius / (d * d);
  std::printf("scaled sphere (x%.2f): irradiance mean %.6f (%.3f sigma from %.6f)\n", (double)scale, stat.mean,
              stat.stderrOfMean > 0.0 ? (stat.mean - expected) / stat.stderrOfMean : 0.0, expected);
  checkNear(stat.mean, expected, stat.stderrOfMean, 1e-6,
            "scaled sphere: the cosine-weighted mean matches pi*Le*(scale*radius)^2/d^2 within 5 sigma");

  double const expectedPower = (double)kPi * (double)kLe * 4.0 * kPi * placedRadius * placedRadius;
  double const relPower = std::fabs((double)list[0].power - expectedPower) / expectedPower;
  std::printf("scaled sphere (x%.2f): power %.6f, expected %.6f (%.2e relative)\n", (double)scale,
              (double)list[0].power, expectedPower, relPower);
  check(relPower < 1e-4, "scaled sphere: power is within 1e-4 relative of pi*Le*4*pi*(scale*radius)^2");
}

// A unit disk, rotated 45 degrees about y then scaled (2, 1, 1), both
// post-multiplied so the scale acts across the tilted disk in camera
// space rather than within the disk's own original plane: its own area
// Jacobian's |n'| moves off 1, and its power and samplePoint's own pdf
// both track the placed ellipse's true area, not the unit disk's own
// object-space area alone.
void testScaledDisk() {
  constexpr RtFloat kLe = 10.0f;
  GMANMatrix4 place;
  place.rot(GMANRadians(45.0f), 0.0f, 1.0f, 0.0f);
  place.scale(2.0f, 1.0f, 1.0f);
  GMANTransform const transform = makeTransform(place);
  const auto disk = new GMANRayDisk(0.0f, 1.0f, 360.0f, GMANParameterList(), transform);
  GMANLight const light(GMAN_LIGHT_AREA, GMANColor(kLe, kLe, kLe), GMANPoint(), GMANVector());
  gman::Appearance appearance;
  appearance.areaLight = &light;
  disk->setAppearance(appearance);

  GMANLinearWorldManager world;
  world.add(disk);
  std::vector<gman::Emitter> const list = gman::emitters(world);
  check(list.size() == 1, "scaled disk: one area emitter enumerated");

  // A_cam: pi times the magnitude of the cross product of the disk's own
  // placed object-space x and y axes -- the area of the ellipse the unit
  // circle spanning those axes becomes under the same linear map.
  GMANMatrix4 const& objectToCamera = disk->getObjectToCamera();
  GMANPoint const origin = gman::transformPoint(objectToCamera, GMANPoint(0.0f, 0.0f, 0.0f));
  GMANPoint const xTip = gman::transformPoint(objectToCamera, GMANPoint(1.0f, 0.0f, 0.0f));
  GMANPoint const yTip = gman::transformPoint(objectToCamera, GMANPoint(0.0f, 1.0f, 0.0f));
  GMANVector const placedX(origin, xTip);
  GMANVector const placedY(origin, yTip);
  GMANVector const crossed = placedX.cross(placedY);
  double const aCam = kPi * std::sqrt((double)crossed.dot(crossed));

  // |n'|: the disk's own fixed (0, 0, -1) normal transformed by
  // transformNormal with the inverse placement, before normalizing.
  GMANMatrix4 cameraToObject = objectToCamera;
  cameraToObject.invert();
  GMANVector nPrime = gman::transformNormal(cameraToObject, GMANVector(0.0f, 0.0f, -1.0f));
  double const nPrimeMagnitude = (double)nPrime.magnitude();
  std::printf("scaled disk: |n'| %.6f\n", nPrimeMagnitude);
  checkNear(nPrimeMagnitude, 0.791, 0.0, 0.01, "scaled disk: |n'| is close to 0.791, off 1 by the tilt and scale");

  double const expectedPower = (double)kPi * (double)kLe * aCam;
  double const relPower = std::fabs((double)list[0].power - expectedPower) / expectedPower;
  std::printf("scaled disk: power %.6f, expected %.6f (%.2e relative)\n", (double)list[0].power, expectedPower,
              relPower);
  check(relPower < 1e-4, "scaled disk: power is within 1e-4 relative of pi*Le*A_cam");

  constexpr std::uint32_t kDraws = 1u << 16;
  std::vector<double> inversePdf;
  inversePdf.reserve(kDraws);
  for (std::uint32_t i = 0; i < kDraws; ++i) {
    gman::Sample2D const uv = gman::sample2D(kSeed, 6, 0, i, kDraws, 0u);
    gman::EmitterPoint const ep = gman::samplePoint(list[0], uv.u1, uv.u2);
    inversePdf.push_back((ep.pdf > 0.0f) ? 1.0 / (double)ep.pdf : 0.0);
  }
  GmanMeanStderr const stat = meanStderr(inversePdf);
  double const relArea = std::fabs(stat.mean - aCam) / aCam;
  std::printf("scaled disk: mean(1/pdf) %.6f, A_cam %.6f (%.2e relative)\n", stat.mean, aCam, relArea);
  check(relArea < 1e-4, "scaled disk: mean(1/samplePoint pdf) is within 1e-4 relative of A_cam");
}

// gman::lightSolidAnglePdf: a sphere of radius 2 at the camera-space
// origin, rigidly placed -- the reference value and its own sidedness.
void testLightSolidAnglePdfReference() {
  GMANRaySphere sphere(2.0f, -2.0f, 2.0f, 360.0f, GMANParameterList());
  GMANLight const light(GMAN_LIGHT_AREA, GMANColor(4.0f, 4.0f, 4.0f), GMANPoint(), GMANVector());
  gman::Appearance appearance;
  appearance.areaLight = &light;
  sphere.setAppearance(appearance);
  gman::Emitter const emitter{&light, &sphere, 0.0f};

  // The near pole: hitNormal (0, 0, -1) faces p exactly, at distance 8 (p
  // to hitPoint, not p to the sphere's own centre).
  GMANPoint const p(0.0f, 0.0f, -10.0f);
  GMANPoint const hitPoint(0.0f, 0.0f, -2.0f);
  GMANNormal const hitNormal(0.0f, 0.0f, -1.0f);

  RtFloat const pdf = gman::lightSolidAnglePdf(emitter, p, hitPoint, hitNormal);
  double const expected = 4.0 / kPi;
  double const relError = std::fabs((double)pdf - expected) / expected;
  std::printf("lightSolidAnglePdf reference: %.10f, expected %.10f (%.2e relative)\n", (double)pdf, expected, relError);
  check(relError < 1e-5, "lightSolidAnglePdf: the near-pole reference value matches 4/pi within 1e-5 relative");

  GMANNormal const awayNormal(0.0f, 0.0f, 1.0f);
  RtFloat const awayPdf = gman::lightSolidAnglePdf(emitter, p, hitPoint, awayNormal);
  check(awayPdf == 0.0f, "lightSolidAnglePdf: a hitNormal facing away from p reports exactly 0");
}

// gman::lightSolidAnglePdf against sample()'s own EmitterSample::pdf, on
// the same (u1, u2) draw's point and normal, filtered to the front-facing,
// non-delta draws either technique ever counts: of kDraws draws, requires
// at least kMinFrontFacing so the check cannot pass on an empty set.
void checkLightSolidAnglePdfConsistency(gman::Emitter const& emitter, GMANPoint const& p, std::uint32_t dim,
                                        std::string const& label) {
  constexpr std::uint32_t kDraws = 1u << 12;
  constexpr std::uint32_t kMinFrontFacing = 1u << 10;
  std::uint32_t frontFacing = 0;
  bool allMatch = true;
  for (std::uint32_t i = 0; i < kDraws; ++i) {
    gman::Sample2D const uv = gman::sample2D(kSeed, dim, 0, i, kDraws, 0u);
    gman::EmitterSample const s = gman::sample(emitter, p, uv.u1, uv.u2);
    bool const black = s.Cl.getRed() == 0.0f && s.Cl.getGreen() == 0.0f && s.Cl.getBlue() == 0.0f;
    if (s.isDelta || black) {
      continue;
    }
    ++frontFacing;

    gman::EmitterPoint const ep = gman::samplePoint(emitter, uv.u1, uv.u2);
    RtFloat const pdf = gman::lightSolidAnglePdf(emitter, p, ep.point, ep.normal);
    double const relError = std::fabs((double)pdf - (double)s.pdf) / (double)s.pdf;
    if (!(relError < 1e-5)) {
      allMatch = false;
    }
  }
  std::printf("%s: %u of %u draws front-facing\n", label.c_str(), frontFacing, kDraws);
  check(frontFacing >= kMinFrontFacing, label + ": at least 2^10 of 2^12 draws land front-facing");
  check(allMatch,
        label + ": lightSolidAnglePdf matches sample()'s own pdf within 1e-5 relative on every front-facing draw");
}

void testLightSolidAnglePdfConsistency() {
  GMANPoint const p(0.0f, 0.0f, -10.0f);

  GMANRaySphere rigidSphere(2.0f, -2.0f, 2.0f, 360.0f, GMANParameterList());
  GMANLight const rigidLight(GMAN_LIGHT_AREA, GMANColor(4.0f, 4.0f, 4.0f), GMANPoint(), GMANVector());
  gman::Appearance rigidAppearance;
  rigidAppearance.areaLight = &rigidLight;
  rigidSphere.setAppearance(rigidAppearance);
  checkLightSolidAnglePdfConsistency({&rigidLight, &rigidSphere, 0.0f}, p, 10u, "lightSolidAnglePdf consistency");

  // The same sphere, scaled non-uniformly: J varies pointwise over it.
  GMANMatrix4 spherePlace;
  spherePlace.scale(2.0f, 1.0f, 1.0f);
  GMANTransform const sphereTransform = makeTransform(spherePlace);
  GMANRaySphere scaledSphere(2.0f, -2.0f, 2.0f, 360.0f, GMANParameterList(), sphereTransform);
  GMANLight const scaledSphereLight(GMAN_LIGHT_AREA, GMANColor(4.0f, 4.0f, 4.0f), GMANPoint(), GMANVector());
  gman::Appearance scaledSphereAppearance;
  scaledSphereAppearance.areaLight = &scaledSphereLight;
  scaledSphere.setAppearance(scaledSphereAppearance);
  checkLightSolidAnglePdfConsistency({&scaledSphereLight, &scaledSphere, 0.0f}, p, 11u,
                                     "lightSolidAnglePdf consistency, scaled sphere");

  // A unit disk, rotated then scaled as testScaledDisk's own placement is:
  // |n'| differs from 1.
  GMANMatrix4 diskPlace;
  diskPlace.rot(GMANRadians(45.0f), 0.0f, 1.0f, 0.0f);
  diskPlace.scale(2.0f, 1.0f, 1.0f);
  GMANTransform const diskTransform = makeTransform(diskPlace);
  GMANRayDisk disk(0.0f, 1.0f, 360.0f, GMANParameterList(), diskTransform);
  GMANLight const diskLight(GMAN_LIGHT_AREA, GMANColor(4.0f, 4.0f, 4.0f), GMANPoint(), GMANVector());
  gman::Appearance diskAppearance;
  diskAppearance.areaLight = &diskLight;
  disk.setAppearance(diskAppearance);
  checkLightSolidAnglePdfConsistency({&diskLight, &disk, 0.0f}, p, 12u, "lightSolidAnglePdf consistency, scaled disk");
}

// samplePoint, delta.
void testSamplePointDelta() {
  GMANLight const pointLight(GMAN_LIGHT_POINT, GMANColor(5.0f, 5.0f, 5.0f), GMANPoint(0.0f, 0.0f, -5.0f), GMANVector());
  gman::Emitter const emitter{&pointLight, nullptr, 0.0f};

  gman::EmitterPoint const ep = gman::samplePoint(emitter, 0.25f, 0.75f);
  check(ep.point.getX() == 0.0f && ep.point.getY() == 0.0f && ep.point.getZ() == -5.0f,
        "samplePoint delta: point is the light's own position");
  check(ep.Le.getRed() == 5.0f && ep.Le.getGreen() == 5.0f && ep.Le.getBlue() == 5.0f,
        "samplePoint delta: Le equals cl, undoubled");
  check(ep.pdf == 1.0f, "samplePoint delta: pdf is exactly 1");
  check(ep.isDelta, "samplePoint delta: isDelta is true");
}

// An illuminated area light never reaches appearance.lights, whatever else
// illuminates the same primitive.
void testAppearanceExcludesAreaLight() {
  GMANAttributes attributes;
  const auto areaLight = new GMANLight(GMAN_LIGHT_AREA, GMANColor(4.0f, 4.0f, 4.0f), GMANPoint(), GMANVector());
  RtLightHandle const areaHandle = gmanLightSourceMgr().add(areaLight);
  const auto pointLight = new GMANLight(GMAN_LIGHT_POINT, GMANColor(2.0f, 2.0f, 2.0f), GMANPoint(), GMANVector());
  RtLightHandle const pointHandle = gmanLightSourceMgr().add(pointLight);

  attributes.setIlluminate(areaHandle, RI_TRUE);
  attributes.setAreaLight(areaHandle);
  attributes.setIlluminate(pointHandle, RI_TRUE);

  gman::Appearance const appearance = gman::appearanceOf(attributes);
  check(appearance.areaLight == areaLight, "appearance: areaLight names the illuminated area light");

  bool areaInLights = false;
  for (GMANLight const* light : appearance.lights) {
    if (light == areaLight) {
      areaInLights = true;
    }
  }
  check(!areaInLights, "appearance: lights never contains the area light");
  check(appearance.lights.size() == 1 && appearance.lights[0] == pointLight,
        "appearance: lights contains only the ordinary point light");
}

} // namespace

int main() {
  testEnumeration();
  testPower();
  testSampleDelta();
  testSampleArea();
  testScaledSphere(2.0f, 4);
  testScaledSphere(0.5f, 5);
  testScaledDisk();
  testLightSolidAnglePdfReference();
  testLightSolidAnglePdfConsistency();
  testSamplePointDelta();
  testAppearanceExcludesAreaLight();

  return checkSummary("gman::emitters/sample/samplePoint answer a delta and an area emitter through one interface");
}
