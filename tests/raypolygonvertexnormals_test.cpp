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

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <fstream>
#include <functional>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "check.h"
#include "gmanattributes.h"
#include "gmandictionary.h"
#include "gmanlog.h"
#include "gmanmatrix4.h"
#include "gmannormal.h"
#include "gmanoptions.h"
#include "gmanpoint.h"
#include "gmanpolygonmesh.h"
#include "gmanpolygonmeshfactory.h"
#include "gmanray.h"
#include "gmanrayinterface.h"
#include "gmanrayobjectmanager.h"
#include "gmanraypolygon.h"
#include "gmanshading.h"
#include "gmantransform.h"
#include "gmanvector.h"
#include "maketransform.h"

namespace {

constexpr double kTolerance = 1e-5;

using Vec3 = std::array<double, 3>;
using Weights = std::array<double, 3>;

constexpr std::array<std::array<double, 2>, 4> kCorners = {{{0.0, 0.0}, {0.0, 1.0}, {1.0, 1.0}, {1.0, 0.0}}};
constexpr std::array<std::array<std::size_t, 3>, 2> kTriangles = {{{0, 1, 2}, {0, 2, 3}}};

constexpr double kInteriorXFirst = 0.125;
constexpr double kInteriorYFirst = 0.5;
constexpr double kInteriorXSecond = 0.875;
constexpr double kInteriorYSecond = 0.25;

Vec3 unit(double x, double y, double z) {
  double const magnitude = std::sqrt(x * x + y * y + z * z);
  return {x / magnitude, y / magnitude, z / magnitude};
}

std::array<Vec3, 4> tiltedNormals() {
  return {unit(0.25, 0.125, -1.0), unit(-0.5, 0.25, -1.0), unit(0.375, -0.5, -1.0), unit(-0.25, -0.375, -1.0)};
}

Weights barycentricWeights(std::size_t triangle, double x, double y) {
  auto const& a = kCorners[kTriangles[triangle][0]];
  auto const& b = kCorners[kTriangles[triangle][1]];
  auto const& c = kCorners[kTriangles[triangle][2]];
  double const d = (b[1] - c[1]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[1] - c[1]);
  double const wa = ((b[1] - c[1]) * (x - c[0]) + (c[0] - b[0]) * (y - c[1])) / d;
  double const wb = ((c[1] - a[1]) * (x - c[0]) + (a[0] - c[0]) * (y - c[1])) / d;
  return {wa, wb, 1.0 - wa - wb};
}

Vec3 normalizedBlend(std::array<Vec3, 4> const& normals, std::size_t triangle, double x, double y) {
  Weights const w = barycentricWeights(triangle, x, y);
  Vec3 blend = {0.0, 0.0, 0.0};
  for (std::size_t corner = 0; corner < 3; ++corner) {
    for (std::size_t axis = 0; axis < 3; ++axis) {
      blend[axis] += w[corner] * normals[kTriangles[triangle][corner]][axis];
    }
  }
  return unit(blend[0], blend[1], blend[2]);
}

std::array<Vec3, 4> normalsTiltedFromCentre(double sign) {
  std::array<Vec3, 4> normals;
  for (std::size_t i = 0; i < normals.size(); ++i) {
    normals[i] = unit(sign * (kCorners[i][0] - 0.5), sign * (kCorners[i][1] - 0.5), -1.0);
  }
  return normals;
}

std::array<Vec3, 4> domeNormals() { return normalsTiltedFromCentre(1.0); }

std::array<Vec3, 4> bowlNormals() { return normalsTiltedFromCentre(-1.0); }

Weights signedOffsets(std::array<Vec3, 4> const& normals, std::size_t triangle, double x, double y) {
  Weights offsets;
  for (std::size_t corner = 0; corner < 3; ++corner) {
    std::size_t const index = kTriangles[triangle][corner];
    offsets[corner] = (kCorners[index][0] - x) * normals[index][0] + (kCorners[index][1] - y) * normals[index][1];
  }
  return offsets;
}

Vec3 expectedShadowOrigin(std::array<Vec3, 4> const& normals, std::size_t triangle, double x, double y) {
  Weights const weights = barycentricWeights(triangle, x, y);
  Weights const offsets = signedOffsets(normals, triangle, x, y);
  Vec3 origin = {x, y, 0.0};
  for (std::size_t corner = 0; corner < 3; ++corner) {
    for (std::size_t axis = 0; axis < 3; ++axis) {
      origin[axis] += weights[corner] * std::max(0.0, offsets[corner]) * normals[kTriangles[triangle][corner]][axis];
    }
  }
  return origin;
}

std::array<Vec3, 4> inverseTransposeImages(std::array<Vec3, 4> const& normals, double scaleX) {
  std::array<Vec3, 4> images;
  for (std::size_t i = 0; i < normals.size(); ++i) {
    images[i] = unit(normals[i][0] / scaleX, normals[i][1], normals[i][2]);
  }
  return images;
}

std::array<Vec3, 4> forwardImages(std::array<Vec3, 4> const& normals, double scaleX) {
  std::array<Vec3, 4> images;
  for (std::size_t i = 0; i < normals.size(); ++i) {
    images[i] = unit(normals[i][0] * scaleX, normals[i][1], normals[i][2]);
  }
  return images;
}

std::array<Vec3, 4> negated(std::array<Vec3, 4> normals) {
  for (Vec3& normal : normals) {
    for (double& component : normal) {
      component = -component;
    }
  }
  return normals;
}

std::vector<RtFloat> flatten(std::array<Vec3, 4> const& normals) {
  std::vector<RtFloat> flat;
  for (Vec3 const& normal : normals) {
    for (double const component : normal) {
      flat.push_back((RtFloat)component);
    }
  }
  return flat;
}

bool near(double a, double b, double tolerance = kTolerance) { return std::fabs(a - b) <= tolerance; }

bool near(GMANPoint const& p, Vec3 const& expected, double tolerance = kTolerance) {
  return near(p.getX(), expected[0], tolerance) && near(p.getY(), expected[1], tolerance) &&
         near(p.getZ(), expected[2], tolerance);
}

bool near(GMANVector const& v, Vec3 const& expected, double tolerance = kTolerance) {
  return near(v.getX(), expected[0], tolerance) && near(v.getY(), expected[1], tolerance) &&
         near(v.getZ(), expected[2], tolerance);
}

double distance(GMANVector const& a, GMANVector const& b) {
  GMANVector const d = a - b;
  return std::sqrt((double)d.dot(d));
}

struct Geometry {
  std::vector<RtFloat> points;
  std::vector<RtInt> nverts;
  std::vector<RtInt> verts;
};

Geometry square() {
  Geometry g;
  for (auto const& corner : kCorners) {
    g.points.insert(g.points.end(), {(RtFloat)corner[0], (RtFloat)corner[1], 0.0f});
  }
  g.nverts = {3, 3};
  for (auto const& triangle : kTriangles) {
    for (std::size_t corner : triangle) {
      g.verts.push_back((RtInt)corner);
    }
  }
  return g;
}

Geometry tetrahedron() {
  Geometry g;
  g.points = {1, 1, 1, 1, -1, -1, -1, 1, -1, -1, -1, 1};
  g.nverts = {3, 3, 3, 3};
  g.verts = {0, 1, 2, 0, 1, 3, 0, 2, 3, 1, 2, 3};
  return g;
}

struct NormalInput {
  RtToken token;
  std::vector<RtFloat> values;
};

struct Built {
  std::unique_ptr<GMANPrimitive> owner;
  GMANRayInterface* mesh = nullptr;
};

Built buildMesh(Geometry const& geometry, std::optional<NormalInput> const& normals, GMANTransform& transform) {
  GMANDictionary dictionary;
  std::vector<RtFloat> points = geometry.points;
  RtToken tokens[2] = {RI_P, normals ? normals->token : nullptr};
  std::vector<RtFloat> values = normals ? normals->values : std::vector<RtFloat>();
  RtPointer parms[2] = {(RtPointer)points.data(), (RtPointer)values.data()};
  std::optional<GMANPolygonMesh> const mesh =
      gman::pointsPolygonsMesh((RtInt)geometry.nverts.size(), geometry.nverts.data(), geometry.verts.data(), dictionary,
                               normals ? 2 : 1, tokens, parms, nullptr);
  check(mesh.has_value(), "the factory builds a mesh");
  Built built;
  if (!mesh.has_value()) {
    return built;
  }
  GMANOptions options;
  GMANAttributes attr;
  GMANRayObjectManager manager;
  built.owner.reset(manager.getRSPolygonMesh(*mesh, &options, &attr, &transform));
  built.mesh = dynamic_cast<GMANRayInterface*>(built.owner.get());
  check(built.mesh != nullptr, "getRSPolygonMesh returns a GMANRayInterface");
  return built;
}

Built buildMesh(Geometry const& geometry, std::optional<NormalInput> const& normals) {
  GMANTransform identity;
  return buildMesh(geometry, normals, identity);
}

NormalInput vertexNormals(std::array<Vec3, 4> const& normals) { return NormalInput{RI_N, flatten(normals)}; }

GMANRay rayThrough(double x, double y) {
  return GMANRay(GMANPoint((RtFloat)x, (RtFloat)y, -5.0f), GMANVector(0.0f, 0.0f, 1.0f));
}

std::optional<GMANHit> castAt(Built const& built, double x, double y) {
  GMANHit hit;
  if (built.mesh == nullptr || !built.mesh->intersect(rayThrough(x, y), hit)) {
    return std::nullopt;
  }
  return hit;
}

std::string readFile(std::string const& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream contents;
  contents << in.rdbuf();
  return contents.str();
}

std::size_t occurrences(std::string const& haystack, std::string const& needle) {
  std::size_t count = 0;
  for (std::size_t pos = haystack.find(needle); pos != std::string::npos; pos = haystack.find(needle, pos + 1)) {
    ++count;
  }
  return count;
}

std::string warningLines(std::string const& log) {
  std::istringstream lines(log);
  std::string warnings;
  for (std::string line; std::getline(lines, line);) {
    if (line.find("GMAN WARNING:") != std::string::npos) {
      warnings += line + "\n";
    }
  }
  return warnings;
}

std::string capturedWarnings(std::string const& path, std::function<void()> const& action) {
  std::remove(path.c_str());
  setLogFile(path.c_str());
  setScreenOutput(false);
  action();
  setLogFile("/dev/null");
  setScreenOutput(true);
  return warningLines(readFile(path));
}

void testInterpolation() {
  Built const built = buildMesh(square(), vertexNormals(tiltedNormals()));
  std::array<Vec3, 4> const normals = tiltedNormals();
  struct Probe {
    std::size_t triangle;
    double x;
    double y;
  };
  for (Probe const& probe :
       {Probe{0, kInteriorXFirst, kInteriorYFirst}, Probe{1, kInteriorXSecond, kInteriorYSecond}}) {
    std::string const name = "triangle " + std::to_string(probe.triangle) + ": ";
    std::optional<GMANHit> const hit = castAt(built, probe.x, probe.y);
    check(hit.has_value(), name + "the ray hits");
    if (!hit) {
      continue;
    }
    check(hit->shadingNormal.has_value(), name + "the hit carries a shading normal");
    if (!hit->shadingNormal) {
      continue;
    }
    Vec3 const expected = normalizedBlend(normals, probe.triangle, probe.x, probe.y);
    check(near(*hit->shadingNormal, expected), name + "the shading normal is the normalized barycentric blend");
    GMANVector shading = *hit->shadingNormal;
    check(near(shading.magnitude(), 1.0), name + "the shading normal is unit length");
    check(near(hit->normal, {0.0, 0.0, -1.0}), name + "the hit normal stays the face normal");
  }
}

void testContinuityAcrossDiagonal() {
  Built const built = buildMesh(square(), vertexNormals(tiltedNormals()));
  constexpr double step = 1.0 / 64.0;
  constexpr double bound = 8.0 * step;
  std::optional<GMANHit> const first = castAt(built, 0.5 - step, 0.5 + step);
  std::optional<GMANHit> const second = castAt(built, 0.5 + step, 0.5 - step);
  check(first.has_value() && second.has_value(), "continuity: both rays hit");
  if (!first || !second || !first->shadingNormal || !second->shadingNormal) {
    check(false, "continuity: both hits carry a shading normal");
    return;
  }
  check(first->primitive != second->primitive, "continuity: the two hits land on different triangles");
  double const gap = distance(*first->shadingNormal, *second->shadingNormal);
  check(gap < bound, "continuity: shading normals either side of the diagonal differ by under 8 steps, found " +
                         std::to_string(gap));
}

void testNonUniformScaleUsesInverseTranspose() {
  constexpr double scaleX = 2.0;
  GMANMatrix4 matrix;
  matrix.scale((RtFloat)scaleX, 1.0f, 1.0f);
  GMANTransform transform = makeTransform(matrix);

  std::array<Vec3, 4> const normals = tiltedNormals();
  Built const built = buildMesh(square(), vertexNormals(normals), transform);
  std::optional<GMANHit> const hit = castAt(built, scaleX * kInteriorXFirst, kInteriorYFirst);
  check(hit.has_value(), "transform: the ray hits");
  if (!hit || !hit->shadingNormal) {
    check(false, "transform: the hit carries a shading normal");
    return;
  }
  Vec3 const expected = normalizedBlend(inverseTransposeImages(normals, scaleX), 0, kInteriorXFirst, kInteriorYFirst);
  Vec3 const forward = normalizedBlend(forwardImages(normals, scaleX), 0, kInteriorXFirst, kInteriorYFirst);
  check(near(*hit->shadingNormal, expected),
        "transform: the shading normal blends the inverse-transpose images of the vertex normals");
  check(!near(*hit->shadingNormal, forward, 1e-2),
        "transform: the shading normal is no blend of the forward-transformed normals");
}

void testOrientationAgainstWinding() {
  std::array<Vec3, 4> const normals = tiltedNormals();
  std::array<Vec3, 4> const opposite = negated(normals);
  GMANRay const ray = rayThrough(kInteriorXFirst, kInteriorYFirst);

  {
    Built const built = buildMesh(square(), vertexNormals(normals));
    std::optional<GMANHit> const hit = castAt(built, kInteriorXFirst, kInteriorYFirst);
    check(hit.has_value() && hit->shadingNormal.has_value(), "agreeing normals: the hit carries a shading normal");
    if (hit && hit->shadingNormal) {
      gman::SurfacePoint const point = gman::hitSurfacePoint(ray, *hit);
      Vec3 const expected = normalizedBlend(normals, 0, kInteriorXFirst, kInteriorYFirst);
      check(near(point.N, expected), "agreeing normals: N is the shading normal");
      check(near(point.Ng, {0.0, 0.0, -1.0}), "agreeing normals: Ng is the face normal");
      check(point.N.dot(point.Ng) > 0.0f, "agreeing normals: N . Ng is positive");
    }
  }

  {
    Built const built = buildMesh(square(), vertexNormals(opposite));
    std::optional<GMANHit> const hit = castAt(built, kInteriorXFirst, kInteriorYFirst);
    check(hit.has_value() && hit->shadingNormal.has_value(), "opposed normals: the hit carries a shading normal");
    if (hit && hit->shadingNormal) {
      check(near(hit->normal, {0.0, 0.0, -1.0}), "opposed normals: the hit normal stays the face normal");
      gman::SurfacePoint const point = gman::hitSurfacePoint(ray, *hit);
      Vec3 const expected = normalizedBlend(opposite, 0, kInteriorXFirst, kInteriorYFirst);
      check(near(point.N, expected), "opposed normals: N is the shading normal");
      check(near(point.Ng, {0.0, 0.0, 1.0}), "opposed normals: Ng is the face normal turned to face N");
      check(point.N.dot(point.Ng) > 0.0f, "opposed normals: N . Ng is positive");
    }
  }
}

void testAbsentOrUnusableNormalShadesFlat() {
  std::vector<RtFloat> const facevaryingValues(18, 0.25f);
  struct Case {
    std::string name;
    std::optional<NormalInput> normals;
  };
  std::vector<Case> const cases = {
      {"no N", std::nullopt},
      {"facevarying N of the wrong length", NormalInput{"facevarying normal N", facevaryingValues}},
  };
  GMANRay const ray = rayThrough(kInteriorXFirst, kInteriorYFirst);
  for (Case const& c : cases) {
    Built const built = buildMesh(square(), c.normals);
    std::optional<GMANHit> const hit = castAt(built, kInteriorXFirst, kInteriorYFirst);
    check(hit.has_value(), c.name + ": the ray hits");
    if (!hit) {
      continue;
    }
    check(!hit->shadingNormal.has_value(), c.name + ": the hit carries no shading normal");
    gman::SurfacePoint const point = gman::hitSurfacePoint(ray, *hit);
    check(near(point.N, {hit->normal.getX(), hit->normal.getY(), hit->normal.getZ()}, 0.0),
          c.name + ": N is the hit normal");
    check(near(point.Ng, {hit->normal.getX(), hit->normal.getY(), hit->normal.getZ()}, 0.0),
          c.name + ": Ng is the hit normal");
  }
}

void testUniformNormalOnTetrahedronShadesFlat() {
  NormalInput const uniform{"uniform normal N", std::vector<RtFloat>(12, 0.5f)};
  Geometry const geometry = tetrahedron();
  Built const built = buildMesh(geometry, uniform);
  for (std::size_t face = 0; face < geometry.nverts.size(); ++face) {
    GMANVector centroid;
    for (std::size_t corner = 0; corner < 3; ++corner) {
      auto const index = (std::size_t)geometry.verts[3 * face + corner];
      centroid +=
          GMANVector(geometry.points[3 * index], geometry.points[3 * index + 1], geometry.points[3 * index + 2]);
    }
    GMANHit hit;
    bool const found =
        built.mesh != nullptr && built.mesh->intersect(GMANRay(GMANPoint(0.0f, 0.0f, 0.0f), centroid), hit);
    std::string const name = "tetrahedron face " + std::to_string(face) + ": ";
    check(found, name + "the ray from the centre hits");
    if (found) {
      check(!hit.shadingNormal.has_value(), name + "a uniform N of 3 * pointCount floats gives no shading normal");
    }
  }
}

void testWarnings() {
  std::string const needle = "GMAN WARNING:";
  std::vector<RtFloat> const wrongLength(18, 0.25f);

  {
    std::string const log = capturedWarnings("raypolygonvertexnormals_facevarying.log", [&] {
      Built const built = buildMesh(square(), NormalInput{"facevarying normal N", wrongLength});
    });
    check(occurrences(log, needle) == 1, "facevarying N: exactly one warning");
    check(log.find("facevarying") != std::string::npos, "facevarying N: the warning names the class");
  }

  {
    std::string const log = capturedWarnings("raypolygonvertexnormals_uniform.log", [&] {
      Built const built = buildMesh(tetrahedron(), NormalInput{"uniform normal N", std::vector<RtFloat>(12, 0.5f)});
    });
    check(occurrences(log, needle) == 1, "uniform N on a tetrahedron: exactly one warning");
    check(log.find("uniform") != std::string::npos, "uniform N on a tetrahedron: the warning names the class");
  }

  {
    Built built;
    std::string const log = capturedWarnings("raypolygonvertexnormals_length.log", [&] {
      built = buildMesh(square(), NormalInput{"varying float N", std::vector<RtFloat>(4, 0.5f)});
    });
    check(occurrences(log, needle) == 1, "varying float N: exactly one warning");
    check(log.find("holds 4 floats, 12 expected") != std::string::npos,
          "varying float N: the warning names the length found and 3 * pointCount");
    std::optional<GMANHit> const hit = castAt(built, kInteriorXFirst, kInteriorYFirst);
    check(hit.has_value() && !hit->shadingNormal.has_value(), "varying float N: the face shades flat");
  }

  {
    std::string const log = capturedWarnings("raypolygonvertexnormals_absent.log",
                                             [&] { Built const built = buildMesh(square(), std::nullopt); });
    check(occurrences(log, needle) == 0, "no N: no warning");
  }

  {
    std::string const log = capturedWarnings("raypolygonvertexnormals_usable.log", [&] {
      Built const built = buildMesh(square(), vertexNormals(tiltedNormals()));
    });
    check(occurrences(log, needle) == 0, "a usable vertex N: no warning");
  }
}

void testCancellingNormalsLeaveNoShadingNormal() {
  std::array<Vec3, 4> const cancelling = {Vec3{0.0, 0.0, 1.0}, Vec3{0.0, 0.0, -1.0}, Vec3{0.0, 0.0, -1.0},
                                          Vec3{0.0, 0.0, -1.0}};
  Built const built = buildMesh(square(), vertexNormals(cancelling));
  std::optional<GMANHit> const hit = castAt(built, kInteriorXFirst, kInteriorYFirst);
  check(hit.has_value(), "cancelling normals: the ray hits");
  if (hit) {
    check(!hit->shadingNormal.has_value(), "cancelling normals: the zero blend leaves no shading normal");
  }
}

void testDomeLiftsTheShadowOrigin() {
  std::array<Vec3, 4> const normals = domeNormals();
  Built const built = buildMesh(square(), vertexNormals(normals));
  std::optional<GMANHit> const hit = castAt(built, kInteriorXFirst, kInteriorYFirst);
  check(hit.has_value() && hit->shadowOrigin.has_value(), "dome: the hit carries a shadow origin");
  if (!hit || !hit->shadowOrigin) {
    return;
  }
  check(near(*hit->shadowOrigin, expectedShadowOrigin(normals, 0, kInteriorXFirst, kInteriorYFirst)),
        "dome: the shadow origin is the hit lifted to each vertex's tangent plane");
  check(hit->shadowOrigin->getZ() < hit->point.getZ(), "dome: the shadow origin lies on the normals' side");
  constexpr double minimumLift = 0.01;
  check(distance(*hit->shadowOrigin, hit->point) >= minimumLift,
        "dome: the shadow origin sits at least " + std::to_string(minimumLift) + " from the hit point");
}

void testBowlLeavesTheShadowOriginOnTheHitPoint() {
  std::array<Vec3, 4> const normals = bowlNormals();
  Weights const offsets = signedOffsets(normals, 0, kInteriorXFirst, kInteriorYFirst);
  check(offsets[0] < 0.0 && offsets[1] < 0.0 && offsets[2] < 0.0,
        "bowl: every vertex's tangent plane lies below the hit");
  Built const built = buildMesh(square(), vertexNormals(normals));
  std::optional<GMANHit> const hit = castAt(built, kInteriorXFirst, kInteriorYFirst);
  check(hit.has_value() && hit->shadowOrigin.has_value(), "bowl: the hit carries a shadow origin");
  if (hit && hit->shadowOrigin) {
    check(near(*hit->shadowOrigin, {hit->point.getX(), hit->point.getY(), hit->point.getZ()}),
          "bowl: the shadow origin is the hit point");
  }
}

void testMixedCurvatureLiftsOnlyAbovePlanes() {
  std::array<Vec3, 4> const normals = tiltedNormals();
  struct Probe {
    std::size_t triangle;
    double x;
    double y;
  };
  for (Probe const& probe :
       {Probe{0, kInteriorXFirst, kInteriorYFirst}, Probe{1, kInteriorXSecond, kInteriorYSecond}}) {
    std::string const name = "mixed triangle " + std::to_string(probe.triangle) + ": ";
    Weights const offsets = signedOffsets(normals, probe.triangle, probe.x, probe.y);
    bool const above = offsets[0] > 0.0 || offsets[1] > 0.0 || offsets[2] > 0.0;
    bool const below = offsets[0] < 0.0 || offsets[1] < 0.0 || offsets[2] < 0.0;
    check(above && below, name + "a tangent plane lies above the hit and another below");
    Built const built = buildMesh(square(), vertexNormals(normals));
    std::optional<GMANHit> const hit = castAt(built, probe.x, probe.y);
    check(hit.has_value() && hit->shadowOrigin.has_value(), name + "the hit carries a shadow origin");
    if (hit && hit->shadowOrigin) {
      check(near(*hit->shadowOrigin, expectedShadowOrigin(normals, probe.triangle, probe.x, probe.y)),
            name + "the shadow origin lifts by the planes above the hit alone");
    }
  }
}

void testNoShadowOriginWithoutAShadingNormal() {
  Built const plain = buildMesh(square(), std::nullopt);
  std::optional<GMANHit> const plainHit = castAt(plain, kInteriorXFirst, kInteriorYFirst);
  check(plainHit.has_value(), "no N: the ray hits");
  if (plainHit) {
    check(!plainHit->shadowOrigin.has_value(), "no N: the hit carries no shadow origin");
  }

  std::array<Vec3, 4> const cancelling = {Vec3{0.0, 0.0, 1.0}, Vec3{0.0, 0.0, -1.0}, Vec3{0.0, 0.0, -1.0},
                                          Vec3{0.0, 0.0, -1.0}};
  Built const cancelled = buildMesh(square(), vertexNormals(cancelling));
  std::optional<GMANHit> const cancelledHit = castAt(cancelled, kInteriorXFirst, kInteriorYFirst);
  check(cancelledHit.has_value(), "cancelling normals: the ray hits");
  if (cancelledHit) {
    check(!cancelledHit->shadowOrigin.has_value(), "cancelling normals: the hit carries no shadow origin");
  }
}

void testSurfacePointCarriesTheShadowOrigin() {
  GMANRay const ray = rayThrough(kInteriorXFirst, kInteriorYFirst);
  Built const dome = buildMesh(square(), vertexNormals(domeNormals()));
  std::optional<GMANHit> const domeHit = castAt(dome, kInteriorXFirst, kInteriorYFirst);
  check(domeHit.has_value() && domeHit->shadowOrigin.has_value(), "carried: the dome hit carries a shadow origin");
  if (domeHit && domeHit->shadowOrigin) {
    gman::SurfacePoint const point = gman::hitSurfacePoint(ray, *domeHit);
    check(point.shadowOrigin.has_value(), "carried: the surface point carries a shadow origin");
    if (point.shadowOrigin) {
      check(near(*point.shadowOrigin,
                 {domeHit->shadowOrigin->getX(), domeHit->shadowOrigin->getY(), domeHit->shadowOrigin->getZ()}, 0.0),
            "carried: the surface point's shadow origin is the hit's");
    }
  }

  Built const plain = buildMesh(square(), std::nullopt);
  std::optional<GMANHit> const plainHit = castAt(plain, kInteriorXFirst, kInteriorYFirst);
  check(plainHit.has_value(), "carried: the plain ray hits");
  if (plainHit) {
    check(!gman::hitSurfacePoint(ray, *plainHit).shadowOrigin.has_value(),
          "carried: a hit without N gives a surface point no shadow origin");
  }
}

void testNonFiniteLiftLeavesNoShadowOrigin() {
  constexpr double extent = 1e5;
  constexpr double normalLength = 1e18;
  std::vector<GMANPoint> outer;
  std::vector<std::pair<RtFloat, RtFloat>> texCoords;
  std::vector<GMANVector> normals;
  std::array<Vec3, 4> const unitNormals = domeNormals();
  for (std::size_t i = 0; i < kCorners.size(); ++i) {
    outer.emplace_back((RtFloat)(extent * kCorners[i][0]), (RtFloat)(extent * kCorners[i][1]), 0.0f);
    texCoords.emplace_back((RtFloat)kCorners[i][0], (RtFloat)kCorners[i][1]);
    normals.emplace_back((RtFloat)(normalLength * unitNormals[i][0]), (RtFloat)(normalLength * unitNormals[i][1]),
                         (RtFloat)(normalLength * unitNormals[i][2]));
  }
  GMANRayPolygon const polygon(outer, {}, texCoords, normals);
  GMANHit hit;
  bool const found = polygon.intersect(rayThrough(extent * kInteriorXFirst, extent * kInteriorYFirst), hit);
  check(found, "non-finite lift: the ray hits");
  if (!found) {
    return;
  }
  check(hit.shadingNormal.has_value(), "non-finite lift: the hit carries a shading normal");
  check(!hit.shadowOrigin.has_value(), "non-finite lift: the hit carries no shadow origin");
}

void testSingularTransformShadesFlat() {
  GMANMatrix4 matrix;
  matrix.scale(1.0f, 1.0f, 0.0f);
  GMANTransform transform = makeTransform(matrix);
  Built built;
  std::string const log = capturedWarnings("raypolygonvertexnormals_singular.log", [&] {
    built = buildMesh(square(), vertexNormals(tiltedNormals()), transform);
  });
  std::optional<GMANHit> const hit = castAt(built, kInteriorXFirst, kInteriorYFirst);
  check(hit.has_value(), "singular transform: the ray hits");
  if (hit) {
    check(!hit->shadingNormal.has_value(), "singular transform: the hit carries no shading normal");
  }
  check(occurrences(log, "GMAN WARNING:") == 0, "singular transform: no warning");
}

} // namespace

int main() {
  testInterpolation();
  testContinuityAcrossDiagonal();
  testNonUniformScaleUsesInverseTranspose();
  testOrientationAgainstWinding();
  testAbsentOrUnusableNormalShadesFlat();
  testUniformNormalOnTetrahedronShadesFlat();
  testWarnings();
  testCancellingNormalsLeaveNoShadingNormal();
  testSingularTransformShadesFlat();
  testDomeLiftsTheShadowOrigin();
  testBowlLeavesTheShadowOriginOnTheHitPoint();
  testMixedCurvatureLiftsOnlyAbovePlanes();
  testNoShadowOriginWithoutAShadingNormal();
  testSurfacePointCarriesTheShadowOrigin();
  testNonFiniteLiftLeavesNoShadowOrigin();

  return checkSummary("a polygon's vertex normals interpolate across each hit and orient into the shader's hemisphere");
}
