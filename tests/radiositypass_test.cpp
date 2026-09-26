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
 * GMANRadiosityPass against a reference built from the same public
 * functions it uses internally: a right-triangle floor with a small hole
 * and a square wall meeting one of its legs, plus a sphere, lit by one
 * point light in front of the wall.
 */

#include <cmath>
#include <cstdio>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "check.h"
#include "gmancolor.h"
#include "gmanerror.h"
#include "gmanlightsourcemgr.h"
#include "gmanlinearworldmanager.h"
#include "gmanlog.h"
#include "gmanmath.h"
#include "gmanmatrix4.h"
#include "gmanoptions.h"
#include "gmanparameterlist.h"
#include "gmanpoint.h"
#include "gmanpolygon.h"
#include "gmanradiositymesh.h"
#include "gmanradiositypass.h"
#include "gmanradiositysolver.h"
#include "gmanray.h"
#include "gmanraybbox.h"
#include "gmanraybvh.h"
#include "gmanraycylinder.h"
#include "gmanrayinterface.h"
#include "gmanrayoccluder.h"
#include "gmanraypolygon.h"
#include "gmanraysphere.h"
#include "gmanshaderenvironment.h"
#include "gmanshading.h"
#include "gmansurfaceshader.h"
#include "gmantransform.h"
#include "gmanvector.h"
#include "ri.h"

namespace {

// ---- The main fixture's own geometry ----

// The floor's two legs' shared length, and the wall's own side (it shares
// a leg, so its side equals the leg it shares).
constexpr RtFloat kLegLength = 8.0f;

// The element size under test: chosen so the world's own bbox-derived
// default (kLegLength / 8 = 1.0) differs from it, proving prepare() reads
// the option rather than always falling back to that default.
constexpr RtFloat kSizeUnderTest = 2.0f;

// The hole cell (i = 1, j = 0 at kSizeUnderTest's own step of 2): spans
// [2, 4] x [0, 2], centred at (3, 1), a cell whose far corner (4, 2) sits
// well inside the triangle's own hypotenuse (x + y = 8).
constexpr RtFloat kHoleCentreS = 3.0f;
constexpr RtFloat kHoleCentreT = 1.0f;
constexpr RtFloat kHoleHalfWidth = 0.1f; // under the cell's own side (2) / 8 = 0.25

// The corner-variance cell (i = 0, j = 0): spans [0, 2] x [0, 2], no hole.
constexpr RtFloat kCornerCellS = 1.0f;
constexpr RtFloat kCornerCellT = 1.0f;
constexpr RtFloat kCornerCellHalf = 1.0f;

constexpr RtFloat kWallAlbedo = 0.6f;
constexpr RtFloat kSphereAlbedo = 0.4f;
constexpr RtFloat kFloorAlbedoBase = 0.3f;
constexpr RtFloat kFloorAlbedoSlopeS = 0.04f;
constexpr RtFloat kFloorAlbedoSlopeT = 0.03f;

constexpr RtFloat kSphereCentreX = 5.0f;
constexpr RtFloat kSphereCentreY = 2.0f;
constexpr RtFloat kSphereCentreZ = 2.0f;
constexpr RtFloat kSphereRadius = 0.6f;

constexpr RtFloat kLightX = 4.0f;
constexpr RtFloat kLightY = 3.0f;
constexpr RtFloat kLightZ = 4.0f;
constexpr RtFloat kLightIntensity = 8.0f;

// The comparison's own tolerance: relative, with an absolute floor for a
// channel near 0.
constexpr double kRelativeTol = 1e-6;
constexpr double kAbsoluteFloor = 1e-9;

// ---- B.9's own fixtures ----

constexpr RtFloat kCapElementSize = 0.5f;
constexpr RtFloat kThinPolygonLength = 200.0f;
constexpr RtFloat kThinPolygonWidth = 0.01f;
constexpr RtFloat kWideCylinderRadius = 25.0f;
constexpr RtFloat kWideCylinderHeight = 0.5f;

bool isBlack(GMANColor const& c) { return c.getRed() == 0.0f && c.getGreen() == 0.0f && c.getBlue() == 0.0f; }

bool colorExactly(GMANColor const& a, GMANColor const& b) {
  return a.getRed() == b.getRed() && a.getGreen() == b.getGreen() && a.getBlue() == b.getBlue();
}

double relativeDeviation(double actual, double expected) {
  double const denom = GMANMax(std::fabs(expected), kAbsoluteFloor / kRelativeTol);
  return std::fabs(actual - expected) / denom;
}

bool colorWithinTolerance(GMANColor const& actual, GMANColor const& expected, double& worstOut) {
  double const dr = relativeDeviation(actual.getRed(), expected.getRed());
  double const dg = relativeDeviation(actual.getGreen(), expected.getGreen());
  double const db = relativeDeviation(actual.getBlue(), expected.getBlue());
  worstOut = GMANMax(worstOut, GMANMax(dr, GMANMax(dg, db)));
  bool const okR = std::fabs((double)actual.getRed() - (double)expected.getRed()) <=
                   GMANMax(kRelativeTol * std::fabs((double)expected.getRed()), kAbsoluteFloor);
  bool const okG = std::fabs((double)actual.getGreen() - (double)expected.getGreen()) <=
                   GMANMax(kRelativeTol * std::fabs((double)expected.getGreen()), kAbsoluteFloor);
  bool const okB = std::fabs((double)actual.getBlue() - (double)expected.getBlue()) <=
                   GMANMax(kRelativeTol * std::fabs((double)expected.getBlue()), kAbsoluteFloor);
  return okR && okG && okB;
}

GMANTransform identityTransform() {
  GMANMatrix4 matrix;
  GMANOneMatrix storage(matrix);
  return GMANTransform(storage);
}

GMANTransform translationTransform(RtFloat x, RtFloat y, RtFloat z) {
  GMANMatrix4 matrix;
  matrix.trans(x, y, z);
  GMANOneMatrix storage(matrix);
  return GMANTransform(storage);
}

std::shared_ptr<GMANSurfaceShader const> asAppearanceShader(GMANSurfaceShader const& shader) {
  return std::shared_ptr<GMANSurfaceShader const>(&shader, [](GMANSurfaceShader const*) {});
}

std::string readFile(std::string const& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream contents;
  contents << in.rdbuf();
  return contents.str();
}

std::size_t countOccurrences(std::string const& haystack, std::string const& needle) {
  std::size_t count = 0;
  std::size_t pos = 0;
  while ((pos = haystack.find(needle, pos)) != std::string::npos) {
    ++count;
    pos += needle.size();
  }
  return count;
}

// ---- In-test shaders ----

// Varies with (s, t) so a hit's own albedo depends on where it lands, in
// [kFloorAlbedoBase, kFloorAlbedoBase + slopeS * kLegLength + slopeT *
// kLegLength] over the floor's own extent -- safely inside [0, 1].
class FloorAlbedoShader : public GMANSurfaceShader {
public:
  GMANColor computeCi(GMANSurfaceEnv const&) const override { return GMANColor(0.0f, 0.0f, 0.0f); }
  GMANColor computeOi(GMANSurfaceEnv const& se) const override { return se.Os; }
  GMANColor albedo(GMANSurfaceEnv const& se) const override {
    RtFloat const rho = kFloorAlbedoBase + kFloorAlbedoSlopeS * se.s + kFloorAlbedoSlopeT * se.t;
    return GMANColor(rho, rho, rho);
  }
};

class ConstantAlbedoShader : public GMANSurfaceShader {
public:
  explicit ConstantAlbedoShader(RtFloat rho) : rho_(rho) {}
  GMANColor computeCi(GMANSurfaceEnv const&) const override { return GMANColor(0.0f, 0.0f, 0.0f); }
  GMANColor computeOi(GMANSurfaceEnv const& se) const override { return se.Os; }
  GMANColor albedo(GMANSurfaceEnv const&) const override { return GMANColor(rho_, rho_, rho_); }

private:
  RtFloat rho_;
};

// B.7: answers 1.5, outside [0, 1] -- a base bsdf() clamps, but an
// override answering albedo() directly cannot be clamped by that base.
class OutOfRangeAlbedoShader : public GMANSurfaceShader {
public:
  GMANColor computeCi(GMANSurfaceEnv const&) const override { return GMANColor(0.0f, 0.0f, 0.0f); }
  GMANColor computeOi(GMANSurfaceEnv const& se) const override { return se.Os; }
  GMANColor albedo(GMANSurfaceEnv const&) const override { return GMANColor(1.5f, 1.5f, 1.5f); }
};

GMANLight makePointLight(GMANPoint const& position, RtFloat intensity) {
  return GMANLight(GMAN_LIGHT_POINT, GMANColor(intensity, intensity, intensity), position, GMANVector());
}

// ---- The main fixture: floor (holed right triangle) + wall (square,
// sharing the floor's own X leg) + sphere, one point light in front of
// the wall (Y > 0). ----

struct MainFixture {
  GMANLinearWorldManager world;
  GMANRayBVH bvh;
  GMANRayOccluder occluder;
  GMANRayPolygon* floor = nullptr;
  GMANRayPolygon* wall = nullptr;
  GMANRaySphere* sphere = nullptr;

  MainFixture() : occluder(bvh) {}
};

// sphereShader lets B.7 swap in an out-of-range answer for the sphere
// alone, keeping the floor and wall shaders in range.
std::unique_ptr<MainFixture> buildMainFixture(FloorAlbedoShader const& floorShader,
                                              ConstantAlbedoShader const& wallShader,
                                              GMANSurfaceShader const& sphereShader, GMANLight const& light) {
  auto fx = std::make_unique<MainFixture>();

  std::vector<GMANPoint> const floorOuter = {GMANPoint(0.0f, 0.0f, 0.0f), GMANPoint(kLegLength, 0.0f, 0.0f),
                                             GMANPoint(0.0f, kLegLength, 0.0f)};
  std::vector<GMANPoint> const hole = {
      GMANPoint(kHoleCentreS - kHoleHalfWidth, kHoleCentreT - kHoleHalfWidth, 0.0f),
      GMANPoint(kHoleCentreS + kHoleHalfWidth, kHoleCentreT - kHoleHalfWidth, 0.0f),
      GMANPoint(kHoleCentreS + kHoleHalfWidth, kHoleCentreT + kHoleHalfWidth, 0.0f),
      GMANPoint(kHoleCentreS - kHoleHalfWidth, kHoleCentreT + kHoleHalfWidth, 0.0f),
  };
  // Real "st" (s = x, t = y): plain "P" left every hit's (s, t) at (0, 0)
  // (GMANRayPolygon's own default), so the floor's albedo never varied by
  // position at all, and the hole cell's own reflectance lookup could
  // never be told apart from a wrong one landing on (0, 0) instead.
  RtFloat floorP[] = {0.0f, 0.0f, 0.0f, kLegLength, 0.0f, 0.0f, 0.0f, kLegLength, 0.0f};
  RtFloat floorSt[] = {0.0f, 0.0f, kLegLength, 0.0f, 0.0f, kLegLength};
  RtToken floorTokens[] = {RI_P, RI_ST};
  RtPointer floorParms[] = {(RtPointer)floorP, (RtPointer)floorSt};
  GMANParameterList const floorPl(gman::standardDictionary(), 2, floorTokens, floorParms, 3, 3, 1, 3);
  fx->floor = new GMANRayPolygon(floorOuter, {hole}, floorPl);
  gman::Appearance floorAppearance;
  floorAppearance.shader = asAppearanceShader(floorShader);
  floorAppearance.Os = GMANColor(1.0f, 1.0f, 1.0f);
  floorAppearance.lights = {&light};
  fx->floor->setAppearance(floorAppearance);
  fx->world.add(fx->floor);

  // Front normal +Y (verified by construction order): shares the floor's
  // own (0, 0, 0)-(kLegLength, 0, 0) leg, rising to Z = kLegLength.
  std::vector<GMANPoint> const wallOuter = {GMANPoint(0.0f, 0.0f, 0.0f), GMANPoint(0.0f, 0.0f, kLegLength),
                                            GMANPoint(kLegLength, 0.0f, kLegLength), GMANPoint(kLegLength, 0.0f, 0.0f)};
  fx->wall = new GMANRayPolygon(wallOuter, GMANParameterList());
  gman::Appearance wallAppearance;
  wallAppearance.shader = asAppearanceShader(wallShader);
  wallAppearance.Os = GMANColor(1.0f, 1.0f, 1.0f);
  wallAppearance.lights = {&light};
  fx->wall->setAppearance(wallAppearance);
  fx->world.add(fx->wall);

  fx->sphere = new GMANRaySphere(kSphereRadius, -kSphereRadius, kSphereRadius, 360.0f, GMANParameterList(),
                                 translationTransform(kSphereCentreX, kSphereCentreY, kSphereCentreZ));
  gman::Appearance sphereAppearance;
  sphereAppearance.shader = asAppearanceShader(sphereShader);
  sphereAppearance.Os = GMANColor(1.0f, 1.0f, 1.0f);
  sphereAppearance.lights = {&light};
  fx->sphere->setAppearance(sphereAppearance);
  fx->world.add(fx->sphere);

  fx->bvh.build(fx->world);
  return fx;
}

// ---- The reference: the same tiered reflectance lookup and bilinear
// corner sum GMANRadiosityPass itself computes, built from the same
// public functions it calls, tracking which tier each element's own
// reflectance came from. ----

struct TierCounts {
  std::size_t receiver = 0;
  std::size_t sample = 0;
  std::size_t fallback = 0;
};

gman::SurfacePoint referenceElementSurfacePoint(GMANRadiosityMesh const& mesh, std::size_t element, TierCounts& tiers) {
  GMANRayInterface const* primitive = mesh.getElementPrimitive(element);
  gman::RadiosityReceiver const receiver = gman::radiosityReceiver(mesh, element);

  GMANRay const receiverRay(gman::offsetOrigin(receiver.P, receiver.N, receiver.N, receiver.surfaceMagnitude),
                            -receiver.N);
  GMANHit hit;
  if (primitive->intersect(receiverRay, hit)) {
    ++tiers.receiver;
    return gman::hitSurfacePoint(receiverRay, hit);
  }

  RtFloat const area = gman::radiosityElementArea(mesh, element, GMANRadiositySolver::kDefaultSamples);
  std::vector<gman::RadiositySample> const samples =
      gman::placeRadiositySamples(mesh, element, area, GMANRadiositySolver::kDefaultSamples);
  for (gman::RadiositySample const& sample : samples) {
    GMANRay const sampleRay(gman::offsetOrigin(sample.P, sample.N, sample.N, receiver.surfaceMagnitude), -sample.N);
    if (primitive->intersect(sampleRay, hit)) {
      ++tiers.sample;
      return gman::hitSurfacePoint(sampleRay, hit);
    }
  }

  ++tiers.fallback;
  GMANHit fallback;
  fallback.point = receiver.P;
  fallback.normal = receiver.N;
  fallback.primitive = primitive;
  return gman::hitSurfacePoint(receiverRay, fallback);
}

std::vector<GMANColor> referenceReflectance(GMANRadiosityMesh const& mesh, TierCounts& tiers) {
  std::vector<GMANColor> reflectance(mesh.getElementCount());
  GMANMatrix4 const identity;
  for (std::size_t element = 0; element < reflectance.size(); ++element) {
    gman::Appearance const& appearance = mesh.getElementPrimitive(element)->getAppearance();
    gman::SurfacePoint const point = referenceElementSurfacePoint(mesh, element, tiers);
    reflectance[element] = gman::albedo(appearance, point, identity);
  }
  return reflectance;
}

struct Reference {
  GMANRadiosityMesh mesh;
  GMANRadiositySolution solution;
  TierCounts tiers;
};

Reference buildReference(GMANWorldManager& world, GMANRayOccluder const& occluder, RtFloat elementSize) {
  Reference ref;
  ref.mesh.build(world, elementSize);
  std::vector<GMANColor> const reflectance = referenceReflectance(ref.mesh, ref.tiers);
  ref.solution = GMANRadiositySolver().solve(ref.mesh, occluder, reflectance);
  return ref;
}

GMANColor referenceIrradiance(Reference const& ref, GMANHit const& hit, GMANVector const& I) {
  GMANRadiosityLocation location;
  if (!ref.mesh.locate(hit, location)) {
    return GMANColor(0.0f, 0.0f, 0.0f);
  }
  RtFloat const facing = ref.mesh.getElement(location.element).normal.dot(I);
  GMANRadiositySide const side = facing <= (RtFloat)0 ? GMANRadiositySide::front : GMANRadiositySide::back;
  GMANColor sum;
  for (std::size_t k = 0; k < location.corners.size(); ++k) {
    GMANColor term = ref.solution.getNodeIndirect(location.corners[k], side);
    term.scale(location.weights[k]);
    sum += term;
  }
  return sum;
}

// ---- Ray grids over each surface ----

std::vector<GMANRay> floorGridRays() {
  std::vector<GMANRay> rays;
  for (double x = 0.3; x < (double)kLegLength; x += 0.5) {
    for (double y = 0.3; y < (double)kLegLength; y += 0.5) {
      if (x + y >= (double)kLegLength - 0.3) {
        continue;
      }
      rays.emplace_back(GMANPoint((RtFloat)x, (RtFloat)y, 12.0f), GMANVector(0.0f, 0.0f, -1.0f));
    }
  }
  return rays;
}

std::vector<GMANRay> wallGridRays(bool front) {
  std::vector<GMANRay> rays;
  RtFloat const y = front ? 6.0f : -6.0f;
  RtFloat const dy = front ? -1.0f : 1.0f;
  for (double x = 0.3; x < (double)kLegLength; x += 0.7) {
    for (double z = 0.3; z < (double)kLegLength; z += 0.7) {
      rays.emplace_back(GMANPoint((RtFloat)x, y, (RtFloat)z), GMANVector(0.0f, dy, 0.0f));
    }
  }
  return rays;
}

std::vector<GMANRay> sphereGridRays() {
  std::vector<GMANRay> rays;
  GMANPoint const centre(kSphereCentreX, kSphereCentreY, kSphereCentreZ);
  for (int thetaDeg = 0; thetaDeg < 360; thetaDeg += 30) {
    for (int phiDeg = 30; phiDeg < 180; phiDeg += 60) {
      double const theta = thetaDeg * PI / 180.0;
      double const phi = phiDeg * PI / 180.0;
      GMANVector const dir((RtFloat)(std::sin(phi) * std::cos(theta)), (RtFloat)(std::sin(phi) * std::sin(theta)),
                           (RtFloat)std::cos(phi));
      GMANPoint const origin(centre.getX() - dir.getX() * 5.0f, centre.getY() - dir.getY() * 5.0f,
                             centre.getZ() - dir.getZ() * 5.0f);
      rays.emplace_back(origin, dir);
    }
  }
  return rays;
}

// Runs every ray in rays through fixture's own BVH, comparing pass's and
// the reference's own irradiance at each real hit. Updates worstDeviation,
// sawNonzero and mismatches; returns the number of hits compared.
std::size_t compareOverRays(std::vector<GMANRay> const& rays, MainFixture const& fixture, GMANRadiosityPass const& pass,
                            Reference const& ref, double& worstDeviation, bool& sawNonzero, long& mismatches) {
  std::size_t compared = 0;
  for (GMANRay const& ray : rays) {
    GMANHit hit;
    GMANRayInterface const* hitPrimitive = nullptr;
    if (!fixture.bvh.nearestHit(ray, hit, hitPrimitive)) {
      continue;
    }
    ++compared;
    GMANColor const actual = pass.irradiance(hit, ray.getDirection());
    GMANColor const expected = referenceIrradiance(ref, hit, ray.getDirection());
    if (!isBlack(actual) || !isBlack(expected)) {
      sawNonzero = true;
    }
    if (!colorWithinTolerance(actual, expected, worstDeviation)) {
      ++mismatches;
    }
  }
  return compared;
}

std::vector<GMANRay> allGridRays() {
  std::vector<GMANRay> rays = floorGridRays();
  std::vector<GMANRay> const frontWall = wallGridRays(true);
  std::vector<GMANRay> const backWall = wallGridRays(false);
  std::vector<GMANRay> const sphereRays = sphereGridRays();
  rays.insert(rays.end(), frontWall.begin(), frontWall.end());
  rays.insert(rays.end(), backWall.begin(), backWall.end());
  rays.insert(rays.end(), sphereRays.begin(), sphereRays.end());
  return rays;
}

// The element whose centre lies within tol of point -- used to find the
// hole cell and the corner-variance cell by their own geometry.
std::size_t findElementNear(GMANRadiosityMesh const& mesh, GMANPoint const& point, RtFloat tol) {
  for (std::size_t e = 0; e < mesh.getElementCount(); ++e) {
    GMANVector const delta(mesh.getElement(e).centre, point);
    if (std::fabs(delta.getX()) <= tol && std::fabs(delta.getY()) <= tol && std::fabs(delta.getZ()) <= tol) {
      return e;
    }
  }
  return mesh.getElementCount();
}

// ---- check 1: before prepare(), every hit answers black ----
void testBeforePrepareIsBlack(MainFixture const& fixture) {
  GMANRadiosityPass pass;
  GMANHit hit;
  hit.point = GMANPoint(1.0f, 1.0f, 0.0f);
  hit.normal = GMANVector(0.0f, 0.0f, 1.0f);
  hit.primitive = fixture.floor;
  check(isBlack(pass.irradiance(hit, GMANVector(0.0f, 0.0f, -1.0f))), "before prepare(): irradiance() answers black");
}

// ---- check 2: at the size under test, every answer equals the reference
// within tolerance ----
void testSizeSetMatchesReference(MainFixture const& fixture, GMANRadiosityPass& pass, Reference const& ref,
                                 double& outWorstDeviation) {
  double worst = 0;
  bool sawNonzero = false;
  long mismatches = 0;
  std::vector<GMANRay> const rays = allGridRays();
  std::size_t const compared = compareOverRays(rays, fixture, pass, ref, worst, sawNonzero, mismatches);

  std::printf("radiositypass: size-set comparison: %zu hits compared, worst relative deviation %.3g\n", compared,
              worst);
  std::printf("radiositypass: reflectance tiers: receiver=%zu sample=%zu fallback=%zu\n", ref.tiers.receiver,
              ref.tiers.sample, ref.tiers.fallback);

  check(compared > 20, "size set: enough hits were compared to be meaningful");
  check(mismatches == 0, "size set: every answer equals the reference within tolerance");
  check(sawNonzero, "size set: at least one answer is nonzero");
  check(ref.tiers.sample > 0, "size set: the reference took the form-factor sample tier at least once");
  outWorstDeviation = worst;
}

// ---- check: one element's four corners differ, and two hits inside it
// answer differently ----
void testCornersDifferAndInterpolate(GMANRadiosityPass const& pass, Reference const& ref, MainFixture const& fixture) {
  std::size_t const element = findElementNear(ref.mesh, GMANPoint(kCornerCellS, kCornerCellT, 0.0f), 0.05f);
  check(element < ref.mesh.getElementCount(), "corner variance: the corner-test cell was found");
  if (element >= ref.mesh.getElementCount()) {
    return;
  }

  GMANRadiosityElement const& elem = ref.mesh.getElement(element);
  GMANColor const c0 = ref.solution.getNodeIndirect(elem.corners[0], GMANRadiositySide::front);
  GMANColor const c1 = ref.solution.getNodeIndirect(elem.corners[1], GMANRadiositySide::front);
  GMANColor const c2 = ref.solution.getNodeIndirect(elem.corners[2], GMANRadiositySide::front);
  GMANColor const c3 = ref.solution.getNodeIndirect(elem.corners[3], GMANRadiositySide::front);
  bool const allEqual = colorExactly(c0, c1) && colorExactly(c1, c2) && colorExactly(c2, c3);
  check(!allEqual, "corner variance: the corner-test cell's four corners are not all equal");

  RtFloat const lo = kCornerCellS - kCornerCellHalf;
  GMANHit hitA;
  hitA.point = GMANPoint(lo + 0.2f * (2.0f * kCornerCellHalf), lo + 0.2f * (2.0f * kCornerCellHalf), 0.0f);
  hitA.normal = GMANVector(0.0f, 0.0f, 1.0f);
  hitA.primitive = fixture.floor;
  GMANHit hitB;
  hitB.point = GMANPoint(lo + 0.8f * (2.0f * kCornerCellHalf), lo + 0.8f * (2.0f * kCornerCellHalf), 0.0f);
  hitB.normal = GMANVector(0.0f, 0.0f, 1.0f);
  hitB.primitive = fixture.floor;

  GMANVector const down(0.0f, 0.0f, -1.0f);
  GMANColor const answerA = pass.irradiance(hitA, down);
  GMANColor const answerB = pass.irradiance(hitB, down);
  check(!colorExactly(answerA, answerB), "corner variance: two hits inside the cell answer differently");
}

// ---- check 3: one wall point hit from the front and from behind answers
// the front and back reference values, which differ ----
void testWallFrontAndBack(GMANRadiosityPass const& pass, Reference const& ref, MainFixture const& fixture) {
  GMANHit hit;
  hit.point = GMANPoint(kLegLength / 2, 0.0f, kLegLength / 2);
  hit.normal = GMANVector(0.0f, 1.0f, 0.0f);
  hit.primitive = fixture.wall;

  GMANVector const towardFront(0.0f, -1.0f, 0.0f); // normal . I < 0: front
  GMANVector const towardBack(0.0f, 1.0f, 0.0f);   // normal . I > 0: back

  GMANColor const passFront = pass.irradiance(hit, towardFront);
  GMANColor const passBack = pass.irradiance(hit, towardBack);
  GMANColor const refFront = referenceIrradiance(ref, hit, towardFront);
  GMANColor const refBack = referenceIrradiance(ref, hit, towardBack);

  double worst = 0;
  check(colorWithinTolerance(passFront, refFront, worst), "wall sides: the front answer matches the reference");
  check(colorWithinTolerance(passBack, refBack, worst), "wall sides: the back answer matches the reference");
  check(!colorExactly(passFront, passBack), "wall sides: the front and back answers differ");
}

// ---- check 4: with no size set, every answer equals the reference at
// the world's own bbox-derived default, which differs from the size
// under test ----
void testDefaultSizeMatchesReference(MainFixture& fixture) {
  RtFloat expectedDefault = 0;
  {
    bool any = false;
    GMANPoint boxMin, boxMax;
    for (GMANPrimitive* p = fixture.world.getFirst(); p != nullptr;) {
      auto const* rp = dynamic_cast<GMANRayInterface const*>(p);
      if (rp != nullptr) {
        GMANPoint const lo = rp->getBBox().getMin();
        GMANPoint const hi = rp->getBBox().getMax();
        RtFloat const coords[6] = {lo.getX(), lo.getY(), lo.getZ(), hi.getX(), hi.getY(), hi.getZ()};
        bool finite = true;
        for (RtFloat const c : coords) {
          if (std::fabs(c) >= RI_INFINITY) {
            finite = false;
            break;
          }
        }
        if (finite) {
          if (!any) {
            boxMin = lo;
            boxMax = hi;
            any = true;
          } else {
            boxMin = gman::pointMin(boxMin, lo);
            boxMax = gman::pointMax(boxMax, hi);
          }
        }
      }
      p = fixture.world.getNext();
    }
    if (any) {
      RtFloat const dx = boxMax.getX() - boxMin.getX();
      RtFloat const dy = boxMax.getY() - boxMin.getY();
      RtFloat const dz = boxMax.getZ() - boxMin.getZ();
      expectedDefault = GMANMax(GMANMax(dx, dy), dz) / (RtFloat)8;
    }
  }

  check(expectedDefault != kSizeUnderTest, "default size: the world's own default differs from the size under test");

  GMANOptions options; // radiosityElementSize left unset (0)
  GMANRadiosityPass pass;
  pass.prepare(fixture.world, fixture.occluder, options);

  Reference const ref = buildReference(fixture.world, fixture.occluder, expectedDefault);

  double worst = 0;
  bool sawNonzero = false;
  long mismatches = 0;
  std::vector<GMANRay> const rays = allGridRays();
  std::size_t const compared = compareOverRays(rays, fixture, pass, ref, worst, sawNonzero, mismatches);
  std::printf("radiositypass: default-size comparison: %zu hits compared, worst relative deviation %.3g, default=%f\n",
              compared, worst, (double)expectedDefault);

  check(compared > 20, "default size: enough hits were compared to be meaningful");
  check(mismatches == 0, "default size: every answer equals the reference at the default size");
}

// ---- check 5: a hit on a primitive outside the world answers black ----
void testPrimitiveOutsideWorldIsBlack(GMANRadiosityPass const& pass) {
  GMANRaySphere stray(1.0f, -1.0f, 1.0f, 360.0f, GMANParameterList(), identityTransform());
  GMANHit hit;
  hit.point = GMANPoint(0.0f, 0.0f, 1.0f);
  hit.normal = GMANVector(0.0f, 0.0f, 1.0f);
  hit.primitive = &stray;
  check(isBlack(pass.irradiance(hit, GMANVector(0.0f, 0.0f, -1.0f))),
        "outside primitive: a hit on a primitive the mesh never saw answers black");
}

// ---- check 6: prepare() changes nothing ----
void testPrepareChangesNothing(MainFixture& fixture) {
  std::vector<GMANPrimitive*> before;
  for (GMANPrimitive* p = fixture.world.getFirst(); p != nullptr; p = fixture.world.getNext()) {
    before.push_back(p);
  }

  GMANRadiosityPass pass;
  GMANOptions options;
  options.setRadiosityElementSize(kSizeUnderTest);
  pass.prepare(fixture.world, fixture.occluder, options);

  std::vector<GMANPrimitive*> after;
  for (GMANPrimitive* p = fixture.world.getFirst(); p != nullptr; p = fixture.world.getNext()) {
    after.push_back(p);
  }

  check(before == after, "prepare unchanged: getFirst/getNext yields the same primitives in the same order");

  auto const* floorRay = dynamic_cast<GMANRayInterface const*>(fixture.floor);
  check(floorRay->getAppearance().shader != nullptr && floorRay->getAppearance().Os.getRed() == 1.0f,
        "prepare unchanged: the floor's own appearance is intact");
}

// ---- check 7: an out-of-range albedo makes prepare() answer black
// everywhere, without throwing ----
void testOutOfRangeAlbedoAnswersBlack(FloorAlbedoShader const& floorShader, ConstantAlbedoShader const& wallShader,
                                      GMANLight const& light) {
  OutOfRangeAlbedoShader const badShader;
  std::unique_ptr<MainFixture> fixture = buildMainFixture(floorShader, wallShader, badShader, light);

  GMANRadiosityPass pass;
  GMANOptions options;
  options.setRadiosityElementSize(kSizeUnderTest);
  pass.prepare(fixture->world, fixture->occluder, options);

  std::vector<GMANRay> const rays = allGridRays();
  long nonBlack = 0;
  for (GMANRay const& ray : rays) {
    GMANHit hit;
    GMANRayInterface const* hitPrimitive = nullptr;
    if (!fixture->bvh.nearestHit(ray, hit, hitPrimitive)) {
      continue;
    }
    if (!isBlack(pass.irradiance(hit, ray.getDirection()))) {
      ++nonBlack;
    }
  }
  check(nonBlack == 0, "out-of-range albedo: prepare() answers black everywhere, " + std::to_string(nonBlack) +
                           " non-black hit(s) found");
}

// ---- check 8: a lone sphere lit from outside answers exactly black on
// every outside hit ----
void testLoneSphereAnswersExactlyBlack() {
  ConstantAlbedoShader const shader(0.5f);
  GMANLight const light = makePointLight(GMANPoint(0.0f, 0.0f, -6.0f), 20.0);

  GMANLinearWorldManager world;
  auto* sphere = new GMANRaySphere(1.0f, -1.0f, 1.0f, 360.0f, GMANParameterList(), identityTransform());
  gman::Appearance appearance;
  appearance.shader = asAppearanceShader(shader);
  appearance.Os = GMANColor(1.0f, 1.0f, 1.0f);
  appearance.lights = {&light};
  sphere->setAppearance(appearance);
  world.add(sphere);

  GMANRayBVH bvh;
  bvh.build(world);
  GMANRayOccluder const occluder(bvh);

  GMANRadiosityPass pass;
  GMANOptions options;
  options.setRadiosityElementSize(0.3f);
  pass.prepare(world, occluder, options);

  long checked = 0;
  for (int thetaDeg = 0; thetaDeg < 360; thetaDeg += 20) {
    for (int phiDeg = 20; phiDeg < 180; phiDeg += 40) {
      double const theta = thetaDeg * PI / 180.0;
      double const phi = phiDeg * PI / 180.0;
      GMANVector const dir((RtFloat)(std::sin(phi) * std::cos(theta)), (RtFloat)(std::sin(phi) * std::sin(theta)),
                           (RtFloat)std::cos(phi));
      GMANPoint const origin(-dir.getX() * 5.0f, -dir.getY() * 5.0f, -dir.getZ() * 5.0f);
      GMANRay const ray(origin, dir);
      GMANHit hit;
      GMANRayInterface const* hitPrimitive = nullptr;
      if (!bvh.nearestHit(ray, hit, hitPrimitive)) {
        continue;
      }
      ++checked;
      check(isBlack(pass.irradiance(hit, ray.getDirection())), "lone sphere: an outside hit answers exactly black");
    }
  }
  check(checked > 10, "lone sphere: enough hits were checked to be meaningful");
}

// ---- check 9: the mesh's two cap paths, each reached alone, each log
// exactly one warning naming elementsize; the size-set fixture logs none
// ----
void testCapWarnings(MainFixture& fixture) {
  std::string const logPath = "radiositypass_cap.log";

  // The size-set main fixture: no capped primitive, so no such warning.
  {
    std::remove(logPath.c_str());
    setLogFile(logPath.c_str());
    setScreenOutput(false);
    GMANRadiosityPass pass;
    GMANOptions options;
    options.setRadiosityElementSize(kSizeUnderTest);
    pass.prepare(fixture.world, fixture.occluder, options);
    setLogFile("/dev/null");
    setScreenOutput(true);
  }
  std::string const sizeSetLog = readFile(logPath);
  check(countOccurrences(sizeSetLog, "elementsize") == 0, "cap warnings: the size-set fixture logs no such warning");

  // World A: two thin, long polygons -- the polygon cap path alone.
  {
    GMANLinearWorldManager world;
    ConstantAlbedoShader const shader(0.5f);
    GMANLight const light = makePointLight(GMANPoint(0.0f, 0.0f, 100.0f), 0.0);
    for (RtFloat zOffset : {0.0f, 1.0f}) {
      std::vector<GMANPoint> const verts = {
          GMANPoint(0.0f, 0.0f, zOffset), GMANPoint(kThinPolygonLength, 0.0f, zOffset),
          GMANPoint(kThinPolygonLength, kThinPolygonWidth, zOffset), GMANPoint(0.0f, kThinPolygonWidth, zOffset)};
      auto* polygon = new GMANRayPolygon(verts, GMANParameterList());
      gman::Appearance appearance;
      appearance.shader = asAppearanceShader(shader);
      appearance.Os = GMANColor(1.0f, 1.0f, 1.0f);
      appearance.lights = {}; // lit by nothing
      polygon->setAppearance(appearance);
      world.add(polygon);
    }
    GMANRayBVH bvh;
    bvh.build(world);
    GMANRayOccluder const occluder(bvh);

    std::remove(logPath.c_str());
    setLogFile(logPath.c_str());
    setScreenOutput(false);
    GMANRadiosityPass pass;
    GMANOptions options;
    options.setRadiosityElementSize(kCapElementSize);
    pass.prepare(world, occluder, options);
    setLogFile("/dev/null");
    setScreenOutput(true);

    std::string const log = readFile(logPath);
    check(countOccurrences(log, "elementsize") == 1,
          "cap warnings: the thin-polygon world logs exactly one warning naming elementsize");
  }

  // World B: one short, wide cylinder -- the quadric cap path alone.
  {
    GMANLinearWorldManager world;
    ConstantAlbedoShader const shader(0.5f);
    auto* cylinder = new GMANRayCylinder(kWideCylinderRadius, 0.0f, kWideCylinderHeight, 360.0f, GMANParameterList(),
                                         identityTransform());
    gman::Appearance appearance;
    appearance.shader = asAppearanceShader(shader);
    appearance.Os = GMANColor(1.0f, 1.0f, 1.0f);
    appearance.lights = {};
    cylinder->setAppearance(appearance);
    world.add(cylinder);

    GMANRayBVH bvh;
    bvh.build(world);
    GMANRayOccluder const occluder(bvh);

    std::remove(logPath.c_str());
    setLogFile(logPath.c_str());
    setScreenOutput(false);
    GMANRadiosityPass pass;
    GMANOptions options;
    options.setRadiosityElementSize(kCapElementSize);
    pass.prepare(world, occluder, options);
    setLogFile("/dev/null");
    setScreenOutput(true);

    std::string const log = readFile(logPath);
    check(countOccurrences(log, "elementsize") == 1,
          "cap warnings: the wide-cylinder world logs exactly one warning naming elementsize");
  }
}

} // namespace

int main() {
  FloorAlbedoShader const floorShader;
  ConstantAlbedoShader const wallShader(kWallAlbedo);
  ConstantAlbedoShader const sphereShader(kSphereAlbedo);
  GMANLight const light = makePointLight(GMANPoint(kLightX, kLightY, kLightZ), kLightIntensity);

  std::unique_ptr<MainFixture> fixture = buildMainFixture(floorShader, wallShader, sphereShader, light);

  testBeforePrepareIsBlack(*fixture);
  testPrepareChangesNothing(*fixture);

  GMANRadiosityPass pass;
  GMANOptions options;
  options.setRadiosityElementSize(kSizeUnderTest);
  pass.prepare(fixture->world, fixture->occluder, options);
  Reference const ref = buildReference(fixture->world, fixture->occluder, kSizeUnderTest);

  double worstDeviation = 0;
  testSizeSetMatchesReference(*fixture, pass, ref, worstDeviation);
  testCornersDifferAndInterpolate(pass, ref, *fixture);
  testWallFrontAndBack(pass, ref, *fixture);
  testDefaultSizeMatchesReference(*fixture);
  testPrimitiveOutsideWorldIsBlack(pass);
  testOutOfRangeAlbedoAnswersBlack(floorShader, wallShader, light);
  testLoneSphereAnswersExactlyBlack();
  testCapWarnings(*fixture);

  return checkSummary("GMANRadiosityPass matches a reference built from the same public functions, interpolates "
                      "bilinearly across an element's four corners, answers each side of a two-sided wall "
                      "correctly, matches the reference at the world's own default element size, answers black for an "
                      "unknown primitive, changes nothing during prepare(), answers black after an out-of-range "
                      "albedo, answers exactly black for a lone convex sphere, and logs the mesh's own cap "
                      "warning exactly once per prepare()");
}
