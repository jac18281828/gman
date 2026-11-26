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
 * transform. An area emitter on a shape whose placement is singular, built by
 * hand since gman::emitters skips it, answers weight 0, pdf 0 and zeroed
 * fields from every query. A polygon mesh is one area emitter: enumerated
 * beside a world's other emitters, with the power its area gives, drawn by
 * face in proportion to area, answering a solid-angle density that integrates
 * to 1, agreeing between sample and lightSolidAnglePdf, emitting on one side
 * only and weighting the light choice finitely. Also proves appearanceOf's own
 * exclusion: an illuminated area light never reaches Appearance::lights.
 */

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <memory>
#include <numbers>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "check.h"
#include "gmanattributes.h"
#include "gmanemitter.h"
#include "gmaninvertiblematrix.h"
#include "gmanlightsourcemgr.h"
#include "gmanlinearworldmanager.h"
#include "gmanlog.h"
#include "gmanmath.h"
#include "gmanmatrix4.h"
#include "gmanparameterlist.h"
#include "gmanpoint.h"
#include "gmanraydisk.h"
#include "gmanraypolygon.h"
#include "gmanraypolygonmesh.h"
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
constexpr std::uint32_t kPanelDraws = 1u << 16;

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

std::size_t countOccurrences(std::string const& text, std::string const& needle) {
  std::size_t occurrences = 0;
  for (std::size_t pos = text.find(needle); pos != std::string::npos; pos = text.find(needle, pos + 1)) {
    ++occurrences;
  }
  return occurrences;
}

// gman::emitters skips an area-light sphere or disk whose placement is
// singular, counting each in its one warning.
void testSingularEnumeration() {
  std::string const logPath = "emitter_singular_enumeration.log";
  std::remove(logPath.c_str());
  setLogFile(logPath.c_str());
  setScreenOutput(false);

  GMANMatrix4 flat;
  flat.scale(1.0f, 1.0f, 0.0f);
  GMANTransform const singular = makeTransform(flat);

  GMANLight const regularLight(GMAN_LIGHT_AREA, GMANColor(4.0f, 4.0f, 4.0f), GMANPoint(), GMANVector());
  GMANLight const flatSphereLight(GMAN_LIGHT_AREA, GMANColor(4.0f, 4.0f, 4.0f), GMANPoint(), GMANVector());
  GMANLight const flatDiskLight(GMAN_LIGHT_AREA, GMANColor(4.0f, 4.0f, 4.0f), GMANPoint(), GMANVector());
  gman::Appearance regularAppearance;
  regularAppearance.areaLight = &regularLight;
  gman::Appearance flatSphereAppearance;
  flatSphereAppearance.areaLight = &flatSphereLight;
  gman::Appearance flatDiskAppearance;
  flatDiskAppearance.areaLight = &flatDiskLight;

  GMANLinearWorldManager world;
  const auto regular = new GMANRaySphere(2.0f, -2.0f, 2.0f, 360.0f, GMANParameterList());
  regular->setAppearance(regularAppearance);
  world.add(regular);
  const auto flatSphere = new GMANRaySphere(2.0f, -2.0f, 2.0f, 360.0f, GMANParameterList(), singular);
  flatSphere->setAppearance(flatSphereAppearance);
  world.add(flatSphere);
  const auto flatDisk = new GMANRayDisk(0.0f, 1.0f, 360.0f, GMANParameterList(), singular);
  flatDisk->setAppearance(flatDiskAppearance);
  world.add(flatDisk);

  std::vector<gman::Emitter> const list = gman::emitters(world);

  setLogFile("/dev/null");
  setScreenOutput(true);

  check(list.size() == 1 && list[0].shape == regular && list[0].light == &regularLight,
        "singular enumeration: exactly one emitter, the sphere under the identity placement");
  check(countOccurrences(readFile(logPath), "2 area-light primitive(s)") == 1,
        "singular enumeration: the warning names the singular sphere and disk, 2 area-light primitive(s), once");
}

bool isZero(GMANVector const& v) { return v.getX() == 0.0f && v.getY() == 0.0f && v.getZ() == 0.0f; }

bool isZero(GMANPoint const& p) { return p.getX() == 0.0f && p.getY() == 0.0f && p.getZ() == 0.0f; }

bool isBlack(GMANColor const& c) { return c.getRed() == 0.0f && c.getGreen() == 0.0f && c.getBlue() == 0.0f; }

// An area emitter whose shape's placement is singular answers weight 0, pdf
// 0 and zeroed fields, whatever the receiving point.
void checkSingularEmitter(gman::Emitter const& emitter, std::string const& label, std::uint32_t dim) {
  GMANPoint const closeBy(0.0f, 0.0f, -3.0f);
  GMANPoint const distant(40.0f, -25.0f, -900.0f);

  check(gman::lightChoiceWeight(emitter, closeBy) == 0.0f, label + ": lightChoiceWeight near the shape is exactly 0");
  check(gman::lightChoiceWeight(emitter, distant) == 0.0f,
        label + ": lightChoiceWeight distant from the shape is exactly 0");

  constexpr std::uint32_t kDraws = 1u << 4;
  bool sampleZeroed = true;
  bool pointZeroed = true;
  for (GMANPoint const& receiver : {closeBy, distant}) {
    for (std::uint32_t i = 0; i < kDraws; ++i) {
      gman::Sample2D const uv = gman::sample2D(kSeed, dim, 0, i, kDraws, 0u);
      gman::EmitterSample const s = gman::sample(emitter, receiver, uv.u1, uv.u2);
      if (!isZero(s.wi) || s.distance != 0.0f || !isZero(s.shadowTarget) || !isBlack(s.Cl) || s.pdf != 0.0f ||
          s.isDelta) {
        sampleZeroed = false;
      }
      gman::EmitterPoint const ep = gman::samplePoint(emitter, uv.u1, uv.u2);
      if (!isZero(ep.point) || !isZero(ep.normal) || !isBlack(ep.Le) || ep.pdf != 0.0f || ep.isDelta) {
        pointZeroed = false;
      }
    }
  }
  check(sampleZeroed, label + ": sample returns a zero wi, distance and shadowTarget, black Cl, pdf 0, not delta");
  check(pointZeroed, label + ": samplePoint returns a zero point and normal, black Le, pdf 0, not delta");

  GMANPoint const hitPoint(0.0f, 0.0f, -2.0f);
  GMANNormal const facing(0.0f, 0.0f, -1.0f);
  check(gman::lightSolidAnglePdf(emitter, closeBy, hitPoint, facing) == 0.0f,
        label + ": lightSolidAnglePdf toward a front-facing hit is exactly 0");
}

void testSingularEmitters() {
  GMANMatrix4 flat;
  flat.scale(1.0f, 1.0f, 0.0f);
  GMANTransform const singular = makeTransform(flat);

  GMANLight const sphereLight(GMAN_LIGHT_AREA, GMANColor(4.0f, 4.0f, 4.0f), GMANPoint(), GMANVector());
  GMANRaySphere sphere(2.0f, -2.0f, 2.0f, 360.0f, GMANParameterList(), singular);
  check(!sphere.getObjectToCamera().has_value(), "singular emitters: the sphere's placement is empty");
  checkSingularEmitter({&sphereLight, &sphere, 1.0f}, "singular sphere", 13u);

  GMANLight const diskLight(GMAN_LIGHT_AREA, GMANColor(4.0f, 4.0f, 4.0f), GMANPoint(), GMANVector());
  GMANRayDisk disk(0.0f, 1.0f, 360.0f, GMANParameterList(), singular);
  check(!disk.getObjectToCamera().has_value(), "singular emitters: the disk's placement is empty");
  checkSingularEmitter({&diskLight, &disk, 1.0f}, "singular disk", 14u);
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

  double const area = 4.0 * std::numbers::pi * 2.0 * 2.0;
  double const expectedPower = 4.0 * area * std::numbers::pi;
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
    bool const clMatches = std::fabs(s.Cl.getRed() - directCl.getRed() * std::numbers::pi_v<RtFloat>) < 1e-4 &&
                           std::fabs(s.Cl.getGreen() - directCl.getGreen() * std::numbers::pi_v<RtFloat>) < 1e-4 &&
                           std::fabs(s.Cl.getBlue() - directCl.getBlue() * std::numbers::pi_v<RtFloat>) < 1e-4;
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
  double const expected = 4.0 * std::numbers::pi * (radius / d) * (radius / d);
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
  double const expected = std::numbers::pi * (double)kLe * placedRadius * placedRadius / (d * d);
  std::printf("scaled sphere (x%.2f): irradiance mean %.6f (%.3f sigma from %.6f)\n", (double)scale, stat.mean,
              stat.stderrOfMean > 0.0 ? (stat.mean - expected) / stat.stderrOfMean : 0.0, expected);
  checkNear(stat.mean, expected, stat.stderrOfMean, 1e-6,
            "scaled sphere: the cosine-weighted mean matches pi*Le*(scale*radius)^2/d^2 within 5 sigma");

  double const expectedPower =
      (double)std::numbers::pi * (double)kLe * 4.0 * std::numbers::pi * placedRadius * placedRadius;
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
  std::optional<gman::InvertibleMatrix> const& placement = disk->getObjectToCamera();
  check(placement.has_value(), "scaled disk: the placement inverts");
  if (!placement) {
    return;
  }
  gman::InvertibleMatrix const& objectToCamera = *placement;
  GMANPoint const origin = gman::transformPoint(objectToCamera, GMANPoint(0.0f, 0.0f, 0.0f));
  GMANPoint const xTip = gman::transformPoint(objectToCamera, GMANPoint(1.0f, 0.0f, 0.0f));
  GMANPoint const yTip = gman::transformPoint(objectToCamera, GMANPoint(0.0f, 1.0f, 0.0f));
  GMANVector const placedX(origin, xTip);
  GMANVector const placedY(origin, yTip);
  GMANVector const crossed = placedX.cross(placedY);
  double const aCam = std::numbers::pi * std::sqrt((double)crossed.dot(crossed));

  // |n'|: the disk's own fixed (0, 0, -1) normal transformed by
  // transformNormal by the placement, before normalizing.
  GMANVector nPrime = gman::transformNormal(objectToCamera, GMANVector(0.0f, 0.0f, -1.0f));
  double const nPrimeMagnitude = (double)nPrime.magnitude();
  std::printf("scaled disk: |n'| %.6f\n", nPrimeMagnitude);
  checkNear(nPrimeMagnitude, 0.791, 0.0, 0.01, "scaled disk: |n'| is close to 0.791, off 1 by the tilt and scale");

  double const expectedPower = (double)std::numbers::pi * (double)kLe * aCam;
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
  double const expected = 4.0 / std::numbers::pi;
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

// A mesh built as getRSPolygonMesh builds one: the appearance sits on the mesh
// and on each face.
std::unique_ptr<GMANRayPolygonMesh> buildTaggedMesh(std::vector<std::vector<GMANPoint>> const& loops,
                                                    GMANLight const& light) {
  gman::Appearance appearance;
  appearance.areaLight = &light;
  std::vector<std::unique_ptr<GMANRayPolygon>> faces;
  for (std::vector<GMANPoint> const& loop : loops) {
    std::vector<std::pair<RtFloat, RtFloat>> const texCoords(loop.size(), std::pair<RtFloat, RtFloat>(0.0f, 0.0f));
    auto face = std::make_unique<GMANRayPolygon>(loop, std::vector<std::vector<GMANPoint>>{}, texCoords);
    face->setAppearance(appearance);
    faces.push_back(std::move(face));
  }
  auto mesh = std::make_unique<GMANRayPolygonMesh>(std::move(faces));
  mesh->setAppearance(appearance);
  return mesh;
}

// The rectangle [x0, x1] x [z0, z1] at height y, wound (x0, y, z0) (x0, y, z1)
// (x1, y, z1) (x1, y, z0), so its plane normal is +y.
std::vector<GMANPoint> upRectangle(RtFloat x0, RtFloat x1, RtFloat z0, RtFloat z1, RtFloat y) {
  return {GMANPoint(x0, y, z0), GMANPoint(x0, y, z1), GMANPoint(x1, y, z1), GMANPoint(x1, y, z0)};
}

// upRectangle wound the other way: its plane normal is -y.
std::vector<GMANPoint> downRectangle(RtFloat x0, RtFloat x1, RtFloat z0, RtFloat z1, RtFloat y) {
  std::vector<GMANPoint> loop = upRectangle(x0, x1, z0, z1, y);
  std::ranges::reverse(loop);
  return loop;
}

// Two faces side by side, their areas standing 1 : 3, the larger at x > 0.
std::vector<std::vector<GMANPoint>> unevenPanel(RtFloat y) {
  return {upRectangle(-2.0f, -1.0f, -1.0f, 0.0f, y), upRectangle(1.0f, 2.0f, -1.0f, 2.0f, y)};
}

// A one-mesh world, the mesh tagged as one area light of radiance (2, 4, 6).
// The world owns the mesh.
struct PanelWorld {
  GMANLight light{GMAN_LIGHT_AREA, GMANColor(2.0f, 4.0f, 6.0f), GMANPoint(), GMANVector()};
  GMANLinearWorldManager world;
  GMANRayPolygonMesh const* mesh = nullptr;

  explicit PanelWorld(std::vector<std::vector<GMANPoint>> const& loops) {
    auto owned = buildTaggedMesh(loops, light);
    mesh = owned.get();
    world.add(owned.release());
  }
};

// The one emitter gman::emitters enumerates for w, empty, and a failed check,
// when it enumerates any other count.
std::optional<gman::Emitter> panelEmitter(PanelWorld& w, std::string const& label) {
  std::vector<gman::Emitter> const list = gman::emitters(w.world);
  check(list.size() == 1 && list[0].shape == w.mesh && list[0].light == &w.light,
        label + ": the mesh is one area emitter naming its own light");
  if (list.size() != 1) {
    return std::nullopt;
  }
  return list[0];
}

// A tagged two-face mesh beside a world's other emitters is one area emitter,
// and the warning still names only the bare polygon and degenerate sphere.
void testPolygonEnumeration() {
  std::string const logPath = "emitter_polygon_enumeration.log";
  std::remove(logPath.c_str());
  setLogFile(logPath.c_str());
  setScreenOutput(false);

  GMANLight const meshLight(GMAN_LIGHT_AREA, GMANColor(4.0f, 4.0f, 4.0f), GMANPoint(), GMANVector());
  std::unique_ptr<EmitterWorld> const w = buildEmitterWorld();
  std::unique_ptr<GMANRayPolygonMesh> mesh = buildTaggedMesh(unevenPanel(0.0f), meshLight);
  GMANRayPolygonMesh const* const meshShape = mesh.get();
  w->world.add(mesh.release());
  std::vector<gman::Emitter> const list = gman::emitters(w->world);

  setLogFile("/dev/null");
  setScreenOutput(true);

  check(list.size() == 3, "polygon enumeration: the point light, the sphere and the mesh are three emitters");
  std::size_t meshEmitters = 0;
  for (gman::Emitter const& e : list) {
    if (e.shape == meshShape && e.light == &meshLight) {
      ++meshEmitters;
    }
  }
  check(meshEmitters == 1, "polygon enumeration: the two-face mesh is one area emitter naming its own light");
  check(countOccurrences(readFile(logPath), "2 area-light primitive(s)") == 1,
        "polygon enumeration: the warning still counts only the bare polygon and degenerate sphere");
}

void testPolygonPower() {
  PanelWorld w(unevenPanel(0.0f));
  std::optional<gman::Emitter> const emitter = panelEmitter(w, "polygon power");
  if (!emitter) {
    return;
  }
  double const expected = std::numbers::pi * 4.0 * 4.0;
  double const relative = std::fabs((double)emitter->power - expected) / expected;
  std::printf("polygon power: %.6f, pi * mean(Le) * A %.6f (%.2e relative)\n", (double)emitter->power, expected,
              relative);
  check(relative < 1e-5, "polygon power: pi * mean(Le) * A for radiance (2, 4, 6) and area 4 within 1e-5 relative");
}

void testPolygonFaceChoice() {
  PanelWorld w(unevenPanel(0.0f));
  std::optional<gman::Emitter> const emitter = panelEmitter(w, "polygon face choice");
  if (!emitter) {
    return;
  }
  std::uint32_t onSmaller = 0;
  for (std::uint32_t i = 0; i < kPanelDraws; ++i) {
    gman::Sample2D const uv = gman::sample2D(kSeed, 15, 0, i, kPanelDraws, 0u);
    if (gman::samplePoint(*emitter, uv.u1, uv.u2).point.getX() < 0.0f) {
      ++onSmaller;
    }
  }
  double const share = (double)onSmaller / (double)kPanelDraws;
  double const sigma = std::sqrt(0.25 * 0.75 / (double)kPanelDraws);
  std::printf("polygon face choice: the smaller face holds %.6f of the draws, expected 0.25\n", share);
  checkNear(share, 0.25, sigma, 0.0, "polygon face choice: the face of area 1 in a mesh of areas 1 : 3 takes 1/4");
}

// The pdf over the solid angle a panel subtends integrates to 1: a midpoint
// quadrature of lightSolidAnglePdf * cos / d^2 over the panel's own area.
void testPolygonPdfIntegratesToOne() {
  PanelWorld w({downRectangle(-1.0f, 1.0f, -1.0f, 1.0f, 2.0f)});
  std::optional<gman::Emitter> const emitter = panelEmitter(w, "polygon pdf integral");
  if (!emitter) {
    return;
  }
  GMANPoint const receiver(0.3f, 0.0f, -0.2f);
  GMANNormal const panelNormal(0.0f, -1.0f, 0.0f);
  constexpr int kGrid = 128;
  double const cell = 2.0 / (double)kGrid;
  double total = 0.0;
  for (int row = 0; row < kGrid; ++row) {
    for (int column = 0; column < kGrid; ++column) {
      double const x = -1.0 + ((double)column + 0.5) * cell;
      double const z = -1.0 + ((double)row + 0.5) * cell;
      GMANPoint const hit((RtFloat)x, 2.0f, (RtFloat)z);
      double const pdf = gman::lightSolidAnglePdf(*emitter, receiver, hit, panelNormal);
      double const dx = x - (double)receiver.getX();
      double const dy = 2.0 - (double)receiver.getY();
      double const dz = z - (double)receiver.getZ();
      double const distanceSquared = dx * dx + dy * dy + dz * dz;
      double const cosTheta = dy / std::sqrt(distanceSquared);
      total += pdf * cosTheta / distanceSquared * cell * cell;
    }
  }
  std::printf("polygon pdf integral: %.6f, expected 1\n", total);
  check(std::fabs(total - 1.0) < 1e-3,
        "polygon pdf integral: lightSolidAnglePdf integrates to 1 over the panel within 1e-3");
}

void testPolygonPdfConsistency() {
  PanelWorld w({downRectangle(-2.0f, -1.0f, -1.0f, 0.0f, 2.0f), downRectangle(1.0f, 2.0f, -1.0f, 2.0f, 2.0f)});
  std::optional<gman::Emitter> const emitter = panelEmitter(w, "polygon lightSolidAnglePdf consistency");
  if (!emitter) {
    return;
  }
  checkLightSolidAnglePdfConsistency(*emitter, GMANPoint(0.3f, 0.0f, -0.2f), 16u,
                                     "polygon lightSolidAnglePdf consistency");
}

bool colorsEqual(GMANColor const& a, GMANColor const& b) {
  return a.getRed() == b.getRed() && a.getGreen() == b.getGreen() && a.getBlue() == b.getBlue();
}

// A polygon lights the side its plane normal faces and no other: sample
// reports Le there and black behind, and lightSolidAnglePdf is 0 behind.
void testPolygonOneSided() {
  PanelWorld w({downRectangle(-1.0f, 1.0f, -1.0f, 1.0f, 2.0f)});
  std::optional<gman::Emitter> const emitter = panelEmitter(w, "polygon one-sided");
  if (!emitter) {
    return;
  }
  GMANPoint const lit(0.3f, 0.0f, -0.2f);
  GMANPoint const dark(0.3f, 4.0f, -0.2f);
  GMANColor const radiance = w.light.getCl();
  GMANColor const black(0.0f, 0.0f, 0.0f);

  constexpr std::uint32_t kDraws = 1u << 8;
  bool litSideLit = true;
  bool litSidePdfPositive = true;
  bool darkSideBlack = true;
  bool darkSidePdfZero = true;
  for (std::uint32_t i = 0; i < kDraws; ++i) {
    gman::Sample2D const uv = gman::sample2D(kSeed, 17, 0, i, kDraws, 0u);
    litSideLit = litSideLit && colorsEqual(gman::sample(*emitter, lit, uv.u1, uv.u2).Cl, radiance);
    darkSideBlack = darkSideBlack && colorsEqual(gman::sample(*emitter, dark, uv.u1, uv.u2).Cl, black);
    gman::EmitterPoint const ep = gman::samplePoint(*emitter, uv.u1, uv.u2);
    litSidePdfPositive = litSidePdfPositive && gman::lightSolidAnglePdf(*emitter, lit, ep.point, ep.normal) > 0.0f;
    darkSidePdfZero = darkSidePdfZero && gman::lightSolidAnglePdf(*emitter, dark, ep.point, ep.normal) == 0.0f;
  }
  check(litSideLit, "polygon one-sided: from the emitting side every draw's Cl is Le");
  check(litSidePdfPositive, "polygon one-sided: from the emitting side lightSolidAnglePdf is positive");
  check(darkSideBlack, "polygon one-sided: from the far side every draw's Cl is black");
  check(darkSidePdfZero, "polygon one-sided: from the far side lightSolidAnglePdf is 0");
}

void testPolygonLightChoiceWeight() {
  PanelWorld w(unevenPanel(2.0f));
  std::optional<gman::Emitter> const emitter = panelEmitter(w, "polygon light choice");
  if (!emitter) {
    return;
  }
  RtFloat const beside = gman::lightChoiceWeight(*emitter, GMANPoint(5.0f, 2.0f, 0.0f));
  RtFloat const distant = gman::lightChoiceWeight(*emitter, GMANPoint(1000.0f, -500.0f, 3000.0f));
  check(std::isfinite(beside) && beside > 0.0f,
        "polygon light choice: the weight beside the panel is positive and finite");
  check(std::isfinite(distant) && distant > 0.0f,
        "polygon light choice: the weight far from the panel is positive and finite");
  check(beside > distant, "polygon light choice: the weight falls with distance");
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
  testSingularEnumeration();
  testSingularEmitters();
  testPower();
  testSampleDelta();
  testSampleArea();
  testScaledSphere(2.0f, 4);
  testScaledSphere(0.5f, 5);
  testScaledDisk();
  testLightSolidAnglePdfReference();
  testLightSolidAnglePdfConsistency();
  testPolygonEnumeration();
  testPolygonPower();
  testPolygonFaceChoice();
  testPolygonPdfIntegratesToOne();
  testPolygonPdfConsistency();
  testPolygonOneSided();
  testPolygonLightChoiceWeight();
  testSamplePointDelta();
  testAppearanceExcludesAreaLight();

  return checkSummary("gman::emitters/sample/samplePoint answer a delta and an area emitter through one interface");
}
