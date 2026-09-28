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
#include <sstream>
#include <string>
#include <vector>

#include "check.h"
#include "gmanattributes.h"
#include "gmanemitter.h"
#include "gmanlightsourcemgr.h"
#include "gmanlinearworldmanager.h"
#include "gmanlog.h"
#include "gmanparameterlist.h"
#include "gmanpoint.h"
#include "gmanraypolygon.h"
#include "gmanraysphere.h"
#include "gmansampling.h"
#include "gmanshading.h"
#include "gmanvector.h"
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

// Builds the C.1 world: an eligible sphere emitter, an ineligible (wrong
// shape) polygon tagged as an area light, and an ordinary sphere lit by a
// hand-built point light. Returns the world and every hand-built object it
// owns, all outliving it for the caller's own inspection.
struct EmitterWorld {
  GMANLinearWorldManager world;
  GMANRaySphere* sphere;
  GMANLight sphereLight{GMAN_LIGHT_AREA, GMANColor(4.0f, 4.0f, 4.0f), GMANPoint(), GMANVector()};
  GMANRayPolygon* polygon;
  GMANLight polygonLight{GMAN_LIGHT_AREA, GMANColor(4.0f, 4.0f, 4.0f), GMANPoint(), GMANVector()};
  GMANRaySphere* ordinary;
  GMANLight pointLight{GMAN_LIGHT_POINT, GMANColor(5.0f, 5.0f, 5.0f), GMANPoint(0.0f, 0.0f, -5.0f), GMANVector()};
};

EmitterWorld* buildEmitterWorld() {
  EmitterWorld* w = new EmitterWorld();

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

  w->ordinary = new GMANRaySphere(1.0f, -1.0f, 1.0f, 360.0f, GMANParameterList());
  gman::Appearance ordinaryAppearance;
  ordinaryAppearance.lights = {&w->pointLight};
  w->ordinary->setAppearance(ordinaryAppearance);
  w->world.add(w->ordinary);

  return w;
}

// C.1: enumeration, and the one warning naming the one ineligible
// primitive.
void testEnumeration() {
  std::string const logPath = "emitter_enumeration.log";
  std::remove(logPath.c_str());
  setLogFile(logPath.c_str());
  setScreenOutput(false);

  EmitterWorld* w = buildEmitterWorld();
  std::vector<gman::Emitter> const list = gman::emitters(w->world);

  setLogFile("/dev/null");
  setScreenOutput(true);

  check(list.size() == 2, "enumeration: exactly two emitters, the ineligible polygon excluded");

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

  std::string const log = readFile(logPath);
  check(log.find("1") != std::string::npos, "enumeration: the warning names the count, 1");
}

// C.2: power().
void testPower() {
  // GMANLinearWorldManager owns and deletes what it is given, so the
  // sphere here -- unlike the others below, read but never added to a
  // world -- is heap-allocated.
  GMANRaySphere* fullSphere = new GMANRaySphere(2.0f, -2.0f, 2.0f, 360.0f, GMANParameterList());
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
  double const relQuad = std::fabs(quadraturePower - expectedPower) / expectedPower;
  std::printf("power: quadrature %.6f vs closed form %.6f (%.2e relative)\n", quadraturePower, expectedPower, relQuad);
  check(relQuad < 1e-3, "power: the quadrature integral agrees with power within 1e-3 relative");

  // The delta emitter's own power is exactly 0.
  GMANLight const pointLight(GMAN_LIGHT_POINT, GMANColor(1.0f, 1.0f, 1.0f), GMANPoint(), GMANVector());
  gman::Emitter const deltaEmitter{&pointLight, nullptr, 0.0f};
  check(deltaEmitter.power == 0.0f, "power: a delta emitter's power is exactly 0");
}

// C.3: sample, delta.
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

// C.4: sample, area.
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

// C.5: samplePoint, delta.
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

// A.5 (moved here: Appearance::areaLight is a member this unit's first
// commit could not yet compile against): an illuminated area light never
// reaches appearance.lights, whatever else illuminates the same primitive.
void testAppearanceExcludesAreaLight() {
  GMANAttributes attributes;
  GMANLight* areaLight = new GMANLight(GMAN_LIGHT_AREA, GMANColor(4.0f, 4.0f, 4.0f), GMANPoint(), GMANVector());
  RtLightHandle const areaHandle = gmanLightSourceMgr().add(areaLight);
  GMANLight* pointLight = new GMANLight(GMAN_LIGHT_POINT, GMANColor(2.0f, 2.0f, 2.0f), GMANPoint(), GMANVector());
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
  testSamplePointDelta();
  testAppearanceExcludesAreaLight();

  return checkSummary("gman::emitters/sample/samplePoint answer a delta and an area emitter through one interface");
}
