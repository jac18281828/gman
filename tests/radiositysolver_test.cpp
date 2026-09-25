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
 * GMANRadiositySolver against analytic answers: a closed sphere lit from
 * inside, form-factor closure in a cube, transmission through a
 * translucent square, two-sided squares, a sphere lit from outside beside
 * a floor, and determinism.
 */

#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "check.h"
#include "gmancolor.h"
#include "gmanerror.h"
#include "gmanlightsourcemgr.h"
#include "gmanlinearworldmanager.h"
#include "gmanmath.h"
#include "gmanmatrix4.h"
#include "gmanparameterlist.h"
#include "gmanpoint.h"
#include "gmanradiositymesh.h"
#include "gmanradiositysolver.h"
#include "gmanraybvh.h"
#include "gmanrayoccluder.h"
#include "gmanraypolygon.h"
#include "gmanraysphere.h"
#include "gmanshading.h"
#include "gmantransform.h"
#include "gmanvector.h"

namespace {

// ---- The closed sphere ----

// The sphere's radius, centre and light intensity.
constexpr double kSphereRadius = 1.0;
constexpr double kSphereCentreZ = 5.0;
constexpr double kSphereIntensity = 2.0;

// The largest element edge: the coarsest that dices the sphere into at
// least 100 elements (120; the next coarser gives 98), since the solve's
// cost grows with the square of the element count.
constexpr RtFloat kSphereMaxEdge = 0.42f;

// The sum of A_i against 4 pi R^2: the midpoint rule over each cell.
constexpr double kSphereAreaTolerance = 0.005;

// H_d at a true-surface point against I / R^2: float rounding only.
constexpr double kDirectTolerance = 1e-4;

// Each inner side's H_ind, and each inner node's, against rho H_d /
// (1 - rho): the disc approximation's near-field deficit varies element
// to element.
constexpr double kElementIndirectTolerance = 0.05;

// The A_i-weighted mean inner H_ind. A closure deficit eps becomes an
// error of about eps / (1 - rho), so rho = 0.8 amplifies it fivefold over
// rho = 0.5's twofold; the tolerances scale with that amplification.
constexpr double kMeanIndirectToleranceHalf = 0.02;
constexpr double kMeanIndirectToleranceEightTenths = 0.03;

// ---- Form-factor closure in a cube ----

// The cube's edge and each face's element edge: three elements a side.
constexpr double kCubeEdge = 1.0;
constexpr RtFloat kCubeMaxEdge = 0.34f;

// Each receiver's summed form factors against 1: the disc approximation
// and point sampling err most at a receiver beside an edge or corner.
constexpr double kClosureTolerance = 0.03;

// ---- Transmission through a translucent square ----

constexpr RtFloat kSquareMaxEdge = 0.5f;

// Square B's mean H_ind with the middle square against (1 - Os) times
// its value with the middle square absent: the same rays, scaled exactly,
// so float rounding only.
constexpr double kTransmissionTolerance = 1e-4;

// ---- Two sides ----

// The lit side's mean H_d under reversed winding: the same cells in a
// different order, so float rounding only.
constexpr double kWindingTolerance = 1e-5;

// ---- A sphere lit from outside beside a floor ----

// A coarse sphere: the lit-from-outside checks hold at any resolution.
constexpr RtFloat kOutsideSphereMaxEdge = 0.7f;
constexpr double kOutsideIntensity = 10.0;

// Nodes sameNode joins across the u seam carry equal H_ind to this
// relative tolerance.
constexpr double kSeamTolerance = 1e-5;

// ---- No direct power ----

constexpr RtFloat kNoDirectPowerMaxEdge = 0.5f;

double relativeError(double measured, double expected) { return std::fabs(measured - expected) / std::fabs(expected); }

GMANTransform translation(double x, double y, double z) {
  GMANMatrix4 matrix;
  matrix.trans((RtFloat)x, (RtFloat)y, (RtFloat)z);
  GMANOneMatrix storage(matrix);
  return GMANTransform(storage);
}

gman::Appearance opaqueAppearance(std::vector<GMANLight const*> lights) {
  gman::Appearance appearance;
  appearance.Os = GMANColor(1.0f, 1.0f, 1.0f);
  appearance.lights = std::move(lights);
  return appearance;
}

GMANLight pointLight(GMANPoint const& position, double intensity) {
  auto const channel = (RtFloat)intensity;
  return GMANLight(GMAN_LIGHT_POINT, GMANColor(channel, channel, channel), position, GMANVector());
}

// The side of element facing point.
GMANRadiositySide sideFacing(GMANRadiosityMesh const& mesh, std::size_t element, GMANPoint const& point) {
  gman::RadiosityReceiver const receiver = gman::radiosityReceiver(mesh, element);
  return receiver.N.dot(GMANVector(receiver.P, point)) > 0 ? GMANRadiositySide::front : GMANRadiositySide::back;
}

GMANRadiositySide otherSide(GMANRadiositySide side) {
  return side == GMANRadiositySide::front ? GMANRadiositySide::back : GMANRadiositySide::front;
}

std::vector<GMANColor> uniformReflectance(std::size_t count, double rho) {
  return std::vector<GMANColor>(count, GMANColor((RtFloat)rho, (RtFloat)rho, (RtFloat)rho));
}

GMANRayPolygon* polygon(std::vector<GMANPoint> vertices, gman::Appearance const& appearance) {
  auto* result = new GMANRayPolygon(std::move(vertices), GMANParameterList());
  result->setAppearance(appearance);
  return result;
}

// An axis-aligned square in the plane z, spanning [x0, x1] x [y0, y1],
// counter-clockwise seen from +z unless reversed.
std::vector<GMANPoint> squareXY(double x0, double x1, double y0, double y1, double z, bool reversed = false) {
  std::vector<GMANPoint> corners = {
      GMANPoint((RtFloat)x0, (RtFloat)y0, (RtFloat)z), GMANPoint((RtFloat)x1, (RtFloat)y0, (RtFloat)z),
      GMANPoint((RtFloat)x1, (RtFloat)y1, (RtFloat)z), GMANPoint((RtFloat)x0, (RtFloat)y1, (RtFloat)z)};
  if (reversed) {
    return {corners[3], corners[2], corners[1], corners[0]};
  }
  return corners;
}

// The A_i-weighted mean of one side's value over elements [begin, end).
template <class Value>
GMANColor areaMean(GMANRadiositySolution const& solution, std::size_t begin, std::size_t end, Value value) {
  double weight = 0;
  double sum[3] = {0, 0, 0};
  for (std::size_t e = begin; e < end; ++e) {
    GMANColor const c = value(e);
    double const area = solution.getArea(e);
    weight += area;
    sum[0] += area * c.getRed();
    sum[1] += area * c.getGreen();
    sum[2] += area * c.getBlue();
  }
  return GMANColor((RtFloat)(sum[0] / weight), (RtFloat)(sum[1] / weight), (RtFloat)(sum[2] / weight));
}

// The first element index at or after begin whose primitive is not
// primitive: the end of primitive's contiguous run.
std::size_t runEnd(GMANRadiosityMesh const& mesh, std::size_t begin, GMANRayInterface const* primitive) {
  std::size_t end = begin;
  while (end < mesh.getElementCount() && mesh.getElementPrimitive(end) == primitive) {
    ++end;
  }
  return end;
}

void report(char const* fixture, GMANRadiositySolution const& solution) {
  std::printf("solver: %s elements=%zu shots=%zu stop=%s\n", fixture, solution.getElementCount(),
              solution.getShotCount(), solution.reachedShotCap() ? "cap" : "convergence");
}

bool bitwiseEqual(GMANColor const& a, GMANColor const& b) {
  RtFloat const left[3] = {a.getRed(), a.getGreen(), a.getBlue()};
  RtFloat const right[3] = {b.getRed(), b.getGreen(), b.getBlue()};
  return std::memcmp(left, right, sizeof(left)) == 0;
}

bool solutionsBitwiseEqual(GMANRadiositySolution const& a, GMANRadiositySolution const& b) {
  if (a.getElementCount() != b.getElementCount() || a.getNodeCount() != b.getNodeCount() ||
      a.getShotCount() != b.getShotCount()) {
    return false;
  }
  for (GMANRadiositySide const side : {GMANRadiositySide::front, GMANRadiositySide::back}) {
    for (std::size_t e = 0; e < a.getElementCount(); ++e) {
      if (!bitwiseEqual(a.getDirect(e, side), b.getDirect(e, side)) ||
          !bitwiseEqual(a.getIrradiance(e, side), b.getIrradiance(e, side)) ||
          !bitwiseEqual(a.getIndirect(e, side), b.getIndirect(e, side))) {
        return false;
      }
    }
    for (std::size_t n = 0; n < a.getNodeCount(); ++n) {
      if (!bitwiseEqual(a.getNodeIndirect(n, side), b.getNodeIndirect(n, side))) {
        return false;
      }
    }
  }
  return true;
}

// The closed sphere's checks at one rho, over an already-solved mesh.
void checkClosedSphere(GMANRadiosityMesh const& mesh, GMANRadiositySolution const& solution, GMANPoint const& centre,
                       double rho, double meanTolerance) {
  std::string const label = "closed sphere rho=" + std::to_string(rho).substr(0, 3);
  double const expectedDirect = kSphereIntensity / (kSphereRadius * kSphereRadius);
  double const expectedIndirect = rho * expectedDirect / (1 - rho);

  double worstElement = 0;
  double weightedSum = 0;
  double areaSum = 0;
  bool outerDark = true;
  for (std::size_t e = 0; e < mesh.getElementCount(); ++e) {
    GMANRadiositySide const inner = sideFacing(mesh, e, centre);
    double const direct = solution.getDirect(e, inner).getRed();
    double const indirect = solution.getIndirect(e, inner).getRed();
    worstElement = GMANMax(worstElement, relativeError(indirect, rho * direct / (1 - rho)));
    weightedSum += solution.getArea(e) * indirect;
    areaSum += solution.getArea(e);

    GMANColor const& outer = solution.getIrradiance(e, otherSide(inner));
    outerDark = outerDark && outer.getRed() == 0 && outer.getGreen() == 0 && outer.getBlue() == 0;
  }
  double const meanError = relativeError(weightedSum / areaSum, expectedIndirect);

  // Every element touching a node shares its inner side index.
  GMANRadiositySide const inner = sideFacing(mesh, 0, centre);
  double worstNode = 0;
  for (std::size_t n = 0; n < mesh.getNodeCount(); ++n) {
    worstNode = GMANMax(worstNode, relativeError(solution.getNodeIndirect(n, inner).getRed(), expectedIndirect));
  }

  std::printf("%s: expected H_ind=%.6f mean=%.6f meanError=%.4g worstElement=%.4g worstNode=%.4g\n", label.c_str(),
              expectedIndirect, weightedSum / areaSum, meanError, worstElement, worstNode);
  check(worstElement < kElementIndirectTolerance, label + ": each inner side's H_ind is within 5% of rho H_d/(1-rho)");
  check(meanError < meanTolerance, label + ": the A_i-weighted mean inner H_ind is within tolerance");
  check(outerDark, label + ": every outer side's H is 0");
  check(worstNode < kElementIndirectTolerance, label + ": every inner node's H_ind is within 5%");
}

void testClosedSphere() {
  GMANPoint const centre(0.0f, 0.0f, (RtFloat)kSphereCentreZ);
  GMANLight const light = pointLight(centre, kSphereIntensity);

  GMANLinearWorldManager worldManager;
  auto* sphere = new GMANRaySphere((RtFloat)kSphereRadius, (RtFloat)-kSphereRadius, (RtFloat)kSphereRadius, 360.0f,
                                   GMANParameterList(), translation(0, 0, kSphereCentreZ));
  sphere->setAppearance(opaqueAppearance({&light}));
  worldManager.add(sphere);
  GMANRayBVH bvh;
  bvh.build(worldManager);
  GMANRayOccluder const occluder(bvh);
  GMANRadiosityMesh mesh;
  mesh.build(worldManager, kSphereMaxEdge);

  GMANRadiositySolver const solver;
  std::size_t const elementCount = mesh.getElementCount();
  GMANRadiositySolution const half = solver.solve(mesh, occluder, uniformReflectance(elementCount, 0.5));
  report("closed sphere rho=0.5", half);

  double areaSum = 0;
  double worstDirect = 0;
  double const expectedDirect = kSphereIntensity / (kSphereRadius * kSphereRadius);
  for (std::size_t e = 0; e < elementCount; ++e) {
    areaSum += half.getArea(e);
    GMANRadiositySide const inner = sideFacing(mesh, e, centre);
    worstDirect = GMANMax(worstDirect, relativeError(half.getDirect(e, inner).getRed(), expectedDirect));
  }
  double const analyticArea = 4 * PI * kSphereRadius * kSphereRadius;
  double const areaError = relativeError(areaSum, analyticArea);
  std::printf("closed sphere: elements=%zu nodes=%zu sum A_i=%.6f 4piR^2=%.6f areaError=%.4g worstDirect=%.4g\n",
              elementCount, mesh.getNodeCount(), areaSum, analyticArea, areaError, worstDirect);
  check(elementCount >= 100, "closed sphere: the mesh has at least 100 elements");
  check(areaError < kSphereAreaTolerance, "closed sphere: the sum of A_i is 4 pi R^2 within 0.5%");
  check(worstDirect < kDirectTolerance, "closed sphere: every inner side's H_d is I / R^2");

  checkClosedSphere(mesh, half, centre, 0.5, kMeanIndirectToleranceHalf);

  GMANRadiositySolution const eightTenths = solver.solve(mesh, occluder, uniformReflectance(elementCount, 0.8));
  report("closed sphere rho=0.8", eightTenths);
  checkClosedSphere(mesh, eightTenths, centre, 0.8, kMeanIndirectToleranceEightTenths);

  // Determinism.
  GMANRadiositySolution const again = solver.solve(mesh, occluder, uniformReflectance(elementCount, 0.5));
  check(solutionsBitwiseEqual(half, again), "closed sphere: two solves at rho = 0.5 are bitwise equal");
}

void testCubeClosure() {
  double const h = kCubeEdge / 2;
  double const cz = 5.0;
  GMANPoint const centre(0.0f, 0.0f, (RtFloat)cz);
  gman::Appearance const opaque = opaqueAppearance({});
  auto const at = [&](double x, double y, double z) { return GMANPoint((RtFloat)x, (RtFloat)y, (RtFloat)(cz + z)); };

  GMANLinearWorldManager worldManager;
  worldManager.add(polygon({at(-h, -h, -h), at(h, -h, -h), at(h, h, -h), at(-h, h, -h)}, opaque));
  worldManager.add(polygon({at(-h, -h, h), at(-h, h, h), at(h, h, h), at(h, -h, h)}, opaque));
  worldManager.add(polygon({at(-h, -h, -h), at(-h, h, -h), at(-h, h, h), at(-h, -h, h)}, opaque));
  worldManager.add(polygon({at(h, -h, -h), at(h, -h, h), at(h, h, h), at(h, h, -h)}, opaque));
  worldManager.add(polygon({at(-h, -h, -h), at(-h, -h, h), at(h, -h, h), at(h, -h, -h)}, opaque));
  worldManager.add(polygon({at(-h, h, -h), at(h, h, -h), at(h, h, h), at(-h, h, h)}, opaque));
  GMANRayBVH bvh;
  bvh.build(worldManager);
  GMANRayOccluder const occluder(bvh);
  GMANRadiosityMesh mesh;
  mesh.build(worldManager, kCubeMaxEdge);

  std::size_t const count = mesh.getElementCount();
  std::size_t const samples = GMANRadiositySolver::kDefaultSamples;
  std::vector<std::vector<gman::RadiositySample>> shooters(count);
  std::vector<GMANRadiositySide> inner(count);
  for (std::size_t e = 0; e < count; ++e) {
    shooters[e] = gman::placeRadiositySamples(mesh, e, gman::radiosityElementArea(mesh, e, samples), samples);
    inner[e] = sideFacing(mesh, e, centre);
  }

  double worst = 0;
  double lowest = 2;
  double highest = 0;
  for (std::size_t r = 0; r < count; ++r) {
    gman::RadiosityReceiver const receiver = gman::radiosityReceiver(mesh, r);
    double closure = 0;
    for (std::size_t s = 0; s < count; ++s) {
      if (s == r) {
        continue;
      }
      std::array<GMANColor, 2> const factors = gman::radiosityFormFactors(occluder, receiver, shooters[s], inner[s]);
      closure += factors[static_cast<std::size_t>(inner[r])].getRed();
    }
    worst = GMANMax(worst, std::fabs(closure - 1));
    lowest = GMANMin(lowest, closure);
    highest = GMANMax(highest, closure);
  }
  std::printf("cube closure: elements=%zu K=%zu lowest=%.5f highest=%.5f worst=%.4g\n", count, samples, lowest, highest,
              worst);
  check(count == 54, "cube closure: three elements a side on each face");
  check(worst < kClosureTolerance, "cube closure: every receiver's form factors sum to 1 within 3%");
}

// Squares A (z = 5, rho = 0.5, lit) and B (z = 6, rho = 0, unlit), and,
// when middleOs is given, a larger unlit square at z = 5.5 of that
// opacity covering every A-B segment. Returns B's facing-side mean H_ind.
GMANColor transmittedIndirect(bool withMiddle, GMANColor const& middleOs) {
  GMANLight const light = pointLight(GMANPoint(0.5f, 0.5f, 5.25f), 1.0);
  GMANLinearWorldManager worldManager;
  auto* squareA = polygon(squareXY(0, 1, 0, 1, 5), opaqueAppearance({&light}));
  auto* squareB = polygon(squareXY(0, 1, 0, 1, 6), opaqueAppearance({}));
  worldManager.add(squareA);
  worldManager.add(squareB);
  if (withMiddle) {
    gman::Appearance middle = opaqueAppearance({});
    middle.Os = middleOs;
    worldManager.add(polygon(squareXY(-0.5, 1.5, -0.5, 1.5, 5.5), middle));
  }
  GMANRayBVH bvh;
  bvh.build(worldManager);
  GMANRayOccluder const occluder(bvh);
  GMANRadiosityMesh mesh;
  mesh.build(worldManager, kSquareMaxEdge);

  std::size_t const endA = runEnd(mesh, 0, squareA);
  std::size_t const endB = runEnd(mesh, endA, squareB);
  std::vector<GMANColor> reflectance(mesh.getElementCount(), GMANColor(0.0f, 0.0f, 0.0f));
  for (std::size_t e = 0; e < endA; ++e) {
    reflectance[e] = GMANColor(0.5f, 0.5f, 0.5f);
  }
  GMANRadiositySolution const solution = GMANRadiositySolver().solve(mesh, occluder, reflectance);
  report(withMiddle ? "transmission with middle" : "transmission without middle", solution);

  GMANPoint const facingA(0.5f, 0.5f, 5.0f);
  return areaMean(solution, endA, endB,
                  [&](std::size_t e) { return solution.getIndirect(e, sideFacing(mesh, e, facingA)); });
}

void testTransmission() {
  GMANColor const open = transmittedIndirect(false, GMANColor());
  GMANColor const half = transmittedIndirect(true, GMANColor(0.5f, 0.5f, 0.5f));
  GMANColor const opaque = transmittedIndirect(true, GMANColor(1.0f, 1.0f, 1.0f));
  GMANColor const tinted = transmittedIndirect(true, GMANColor(0.25f, 0.5f, 1.0f));

  std::printf("transmission: open=%.6g half=%.6g opaque=%.6g tinted=(%.6g, %.6g, %.6g)\n", open.getRed(), half.getRed(),
              opaque.getRed(), tinted.getRed(), tinted.getGreen(), tinted.getBlue());
  check(open.getRed() > 0, "transmission: B receives light from A with the middle square absent");
  check(relativeError(half.getRed(), 0.5 * open.getRed()) < kTransmissionTolerance,
        "transmission: a middle square of Os 0.5 halves B's H_ind");
  check(opaque.getRed() == 0 && opaque.getGreen() == 0 && opaque.getBlue() == 0,
        "transmission: an opaque middle square leaves B's H_ind 0");
  check(relativeError(tinted.getRed(), 0.75 * open.getRed()) < kTransmissionTolerance &&
            relativeError(tinted.getGreen(), 0.5 * open.getGreen()) < kTransmissionTolerance && tinted.getBlue() == 0,
        "transmission: Os (0.25, 0.5, 1) scales each channel by 1 - Os");
}

struct LitSquare {
  GMANRadiositySide litSide = GMANRadiositySide::front;
  double litMeanDirect = 0;
  bool unlitDark = true;
};

LitSquare solveLitSquare(bool reversed) {
  GMANPoint const lightPosition(0.3f, 0.6f, 6.0f);
  GMANLight const light = pointLight(lightPosition, 1.0);
  GMANLinearWorldManager worldManager;
  worldManager.add(polygon(squareXY(0, 1, 0, 1, 5, reversed), opaqueAppearance({&light})));
  GMANRayBVH bvh;
  bvh.build(worldManager);
  GMANRayOccluder const occluder(bvh);
  GMANRadiosityMesh mesh;
  mesh.build(worldManager, 0.25f);

  std::size_t const count = mesh.getElementCount();
  GMANRadiositySolution const solution = GMANRadiositySolver().solve(mesh, occluder, uniformReflectance(count, 0.5));
  report(reversed ? "two sides reversed" : "two sides", solution);

  LitSquare result;
  result.litSide = sideFacing(mesh, 0, lightPosition);
  result.litMeanDirect =
      areaMean(solution, 0, count, [&](std::size_t e) { return solution.getDirect(e, result.litSide); }).getRed();
  for (std::size_t e = 0; e < count; ++e) {
    GMANColor const& unlit = solution.getIrradiance(e, otherSide(result.litSide));
    result.unlitDark = result.unlitDark && unlit.getRed() == 0 && unlit.getGreen() == 0 && unlit.getBlue() == 0;
  }
  return result;
}

void testTwoSides() {
  LitSquare const forward = solveLitSquare(false);
  LitSquare const reversed = solveLitSquare(true);
  std::printf("two sides: lit side %d mean H_d=%.7g; reversed lit side %d mean H_d=%.7g\n", (int)forward.litSide,
              forward.litMeanDirect, (int)reversed.litSide, reversed.litMeanDirect);
  check(forward.litMeanDirect > 0, "two sides: the lit side receives direct light");
  check(forward.unlitDark && reversed.unlitDark, "two sides: the unlit side's H is 0");
  check(reversed.litSide == otherSide(forward.litSide), "two sides: reversing the winding moves the lit side index");
  check(relativeError(reversed.litMeanDirect, forward.litMeanDirect) < kWindingTolerance,
        "two sides: reversing the winding leaves the lit side's mean H_d unchanged");
}

void testLitFromOutside() {
  GMANPoint const centre(0.0f, 0.0f, 5.0f);
  // On the +x side, where the sphere's u = 0 meridian runs.
  GMANPoint const lightPosition(3.0f, 2.0f, 5.5f);
  GMANLight const light = pointLight(lightPosition, kOutsideIntensity);

  GMANLinearWorldManager worldManager;
  auto* sphere = new GMANRaySphere(1.0f, -1.0f, 1.0f, 360.0f, GMANParameterList(), translation(0, 0, 5));
  sphere->setAppearance(opaqueAppearance({&light}));
  worldManager.add(sphere);
  std::vector<GMANPoint> const floorCorners = {GMANPoint(-1.4f, -1.3f, 3.6f), GMANPoint(1.4f, -1.3f, 3.6f),
                                               GMANPoint(1.4f, -1.3f, 6.4f), GMANPoint(-1.4f, -1.3f, 6.4f)};
  auto* floor = polygon(floorCorners, opaqueAppearance({&light}));
  worldManager.add(floor);
  GMANRayBVH bvh;
  bvh.build(worldManager);
  GMANRayOccluder const occluder(bvh);
  GMANRadiosityMesh mesh;
  mesh.build(worldManager, kOutsideSphereMaxEdge);

  std::size_t const count = mesh.getElementCount();
  GMANRadiositySolution const solution = GMANRadiositySolver().solve(mesh, occluder, uniformReflectance(count, 0.5));
  report("lit from outside", solution);

  std::size_t const sphereEnd = runEnd(mesh, 0, sphere);
  double worstDirect = 0;
  int facing = 0;
  for (std::size_t e = 0; e < sphereEnd; ++e) {
    gman::RadiosityReceiver const receiver = gman::radiosityReceiver(mesh, e);
    GMANVector toLight(receiver.P, lightPosition);
    double const d = toLight.magnitude();
    toLight.normalize();
    double const cosTheta = receiver.N.dot(toLight);
    if (cosTheta <= 0) {
      continue;
    }
    ++facing;
    double const expected = kOutsideIntensity * cosTheta / (d * d);
    GMANRadiositySide const outer = sideFacing(mesh, e, lightPosition);
    worstDirect = GMANMax(worstDirect, relativeError(solution.getDirect(e, outer).getRed(), expected));
  }

  GMANPoint const above(0.0f, 0.0f, 5.0f);
  double const floorIndirect = areaMean(solution, sphereEnd, count, [&](std::size_t e) {
                                 return solution.getIndirect(e, sideFacing(mesh, e, above));
                               }).getRed();

  // The sphere's grid: nu from element 0's corners, row-major nodes.
  GMANRadiosityElement const& first = mesh.getElement(0);
  std::size_t const nu = first.corners[3] - first.corners[0] - 1;
  std::size_t const nv = sphereEnd / nu;
  double worstSeamNode = 0;
  double leastSeamElement = 1;
  bool joined = true;
  for (std::size_t j = 1; j < nv; ++j) {
    std::size_t const seamStart = j * (nu + 1);
    std::size_t const seamEnd = seamStart + nu;
    joined = joined && mesh.sameNode(seamStart, seamEnd);
    // The elements either side of the seam in row j, and their shared
    // outer side.
    std::size_t const leftElement = j * nu;
    std::size_t const rightElement = j * nu + nu - 1;
    GMANRadiositySide const outer = otherSide(sideFacing(mesh, leftElement, centre));
    double const a = solution.getNodeIndirect(seamStart, outer).getRed();
    double const b = solution.getNodeIndirect(seamEnd, outer).getRed();
    worstSeamNode = GMANMax(worstSeamNode, relativeError(a, b));

    double const left = solution.getIndirect(leftElement, outer).getRed();
    double const right = solution.getIndirect(rightElement, outer).getRed();
    leastSeamElement = GMANMin(leastSeamElement, relativeError(left, right));
  }

  std::printf("lit from outside: elements=%zu sphere nu=%zu nv=%zu facing=%d worstDirect=%.3g floor H_ind=%.6g "
              "worstSeamNode=%.3g leastSeamElement=%.3g\n",
              count, nu, nv, facing, worstDirect, floorIndirect, worstSeamNode, leastSeamElement);
  check(facing > 0, "lit from outside: receivers face the light");
  check(worstDirect < kDirectTolerance, "lit from outside: every facing receiver's H_d is I cos(theta) / d^2");
  check(floorIndirect > 0, "lit from outside: the floor's facing side gains H_ind from the sphere");
  check(joined, "lit from outside: sameNode joins each seam row's u = 0 and u = 1 nodes");
  check(worstSeamNode < kSeamTolerance, "lit from outside: joined seam nodes carry equal outer H_ind");
  check(leastSeamElement > 100 * kSeamTolerance,
        "lit from outside: the elements either side of the seam differ, so the seam check can fail");
}

// A lit square with rho = 0 on every element: unshot starts and stays at
// 0, so the solve returns before its first shot.
void testNoDirectPower() {
  GMANPoint const lightPosition(0.5f, 0.5f, 6.0f);
  GMANLight const light = pointLight(lightPosition, 1.0);
  GMANLinearWorldManager worldManager;
  worldManager.add(polygon(squareXY(0, 1, 0, 1, 5), opaqueAppearance({&light})));
  GMANRayBVH bvh;
  bvh.build(worldManager);
  GMANRayOccluder const occluder(bvh);
  GMANRadiosityMesh mesh;
  mesh.build(worldManager, kNoDirectPowerMaxEdge);

  std::size_t const count = mesh.getElementCount();
  GMANRadiositySolution const solution = GMANRadiositySolver().solve(mesh, occluder, uniformReflectance(count, 0.0));
  report("no direct power", solution);

  GMANRadiositySide const lit = sideFacing(mesh, 0, lightPosition);
  double litDirect = 0;
  bool everyHEqualsHd = true;
  bool everyIndirectZero = true;
  for (std::size_t e = 0; e < count; ++e) {
    litDirect = GMANMax(litDirect, (double)solution.getDirect(e, lit).getRed());
    for (GMANRadiositySide const side : {GMANRadiositySide::front, GMANRadiositySide::back}) {
      everyHEqualsHd = everyHEqualsHd && bitwiseEqual(solution.getDirect(e, side), solution.getIrradiance(e, side));
      GMANColor const& indirect = solution.getIndirect(e, side);
      everyIndirectZero =
          everyIndirectZero && indirect.getRed() == 0 && indirect.getGreen() == 0 && indirect.getBlue() == 0;
    }
  }

  bool everyNodeIndirectZero = true;
  for (std::size_t n = 0; n < mesh.getNodeCount(); ++n) {
    for (GMANRadiositySide const side : {GMANRadiositySide::front, GMANRadiositySide::back}) {
      GMANColor const& nodeIndirect = solution.getNodeIndirect(n, side);
      everyNodeIndirectZero = everyNodeIndirectZero && nodeIndirect.getRed() == 0 && nodeIndirect.getGreen() == 0 &&
                              nodeIndirect.getBlue() == 0;
    }
  }

  std::printf("no direct power: elements=%zu litDirect=%.6g\n", count, litDirect);
  check(litDirect > 0, "no direct power: the lit side's H_d is greater than 0");
  check(everyHEqualsHd, "no direct power: every side's H equals its H_d");
  check(everyIndirectZero, "no direct power: every element side's H_ind is exactly 0");
  check(everyNodeIndirectZero, "no direct power: every node side's H_ind is exactly 0");
}

// True when solve throws GMANError for these arguments.
bool rejects(GMANRadiosityMesh const& mesh, GMANRayOccluder const& occluder, std::vector<GMANColor> const& reflectance,
             std::size_t samples) {
  try {
    GMANRadiositySolver().solve(mesh, occluder, reflectance, samples);
  } catch (GMANError const&) {
    return true;
  }
  return false;
}

void testRejectsBadInput() {
  GMANLight const light = pointLight(GMANPoint(0.5f, 0.5f, 6.0f), 1.0);
  GMANLinearWorldManager worldManager;
  worldManager.add(polygon(squareXY(0, 1, 0, 1, 5), opaqueAppearance({&light})));
  GMANRayBVH bvh;
  bvh.build(worldManager);
  GMANRayOccluder const occluder(bvh);
  GMANRadiosityMesh mesh;
  mesh.build(worldManager, 0.5f);

  std::size_t const count = mesh.getElementCount();
  std::vector<GMANColor> const valid = uniformReflectance(count, 0.5);
  std::vector<GMANColor> tooBright = valid;
  tooBright[1] = GMANColor(0.5f, 1.5f, 0.5f);
  std::vector<GMANColor> negative = valid;
  negative[0] = GMANColor(0.5f, 0.5f, -0.1f);

  check(!rejects(mesh, occluder, valid, 16) && !rejects(mesh, occluder, valid, 4),
        "bad input: valid reflectance and sample counts solve");
  check(rejects(mesh, occluder, uniformReflectance(count + 1, 0.5), 16),
        "bad input: a reflectance count mismatch throws");
  check(rejects(mesh, occluder, tooBright, 16) && rejects(mesh, occluder, negative, 16),
        "bad input: a reflectance channel outside [0, 1] throws");
  check(rejects(mesh, occluder, valid, 9) && rejects(mesh, occluder, valid, 15) && rejects(mesh, occluder, valid, 0),
        "bad input: a sample count that is not a square of an even root throws");
}

} // namespace

int main() {
  testClosedSphere();
  testCubeClosure();
  testTransmission();
  testTwoSides();
  testLitFromOutside();
  testNoDirectPower();
  testRejectsBadInput();

  return checkSummary("GMANRadiositySolver matches its analytic fixtures");
}
