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
 * GMANRayPrimitiveFactory::getRSPatch and getRSPatchMesh tessellate each
 * request into a watertight triangle mesh over the z-buffer's own
 * kParametricDiceCount grid. Expected points come from the blending
 * functions of RISpec 3.2 section 5.2, written out here.
 */

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <initializer_list>
#include <limits>
#include <map>
#include <memory>
#include <numbers>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "check.h"
#include "gmanattributes.h"
#include "gmandictionary.h"
#include "gmanoptions.h"
#include "gmanparameterlist.h"
#include "gmanpoint.h"
#include "gmanprimitives.h"
#include "gmanray.h"
#include "gmanraypolygon.h"
#include "gmanraypolygonmesh.h"
#include "gmanrayprimitivefactory.h"
#include "gmantransform.h"
#include "gmanvector.h"
#include "ri.h"

namespace {

using Vec3 = std::array<double, 3>;
using Matrix4 = std::array<std::array<double, 4>, 4>;

struct BasisDef {
  Matrix4 matrix;
  RtInt step;
};

BasisDef const kBezier = {{{{-1, 3, -3, 1}, {3, -6, 3, 0}, {-3, 3, 0, 0}, {1, 0, 0, 0}}}, 3};

BasisDef const kBSpline = {{{{-1.0 / 6, 3.0 / 6, -3.0 / 6, 1.0 / 6},
                             {3.0 / 6, -6.0 / 6, 3.0 / 6, 0.0},
                             {-3.0 / 6, 0.0, 3.0 / 6, 0.0},
                             {1.0 / 6, 4.0 / 6, 1.0 / 6, 0.0}}},
                           1};

BasisDef const kCatmullRom = {{{{-1.0 / 2, 3.0 / 2, -3.0 / 2, 1.0 / 2},
                                {2.0 / 2, -5.0 / 2, 4.0 / 2, -1.0 / 2},
                                {-1.0 / 2, 0.0, 1.0 / 2, 0.0},
                                {0.0, 2.0 / 2, 0.0, 0.0}}},
                              1};

BasisDef const kHermite = {{{{2, 1, -2, 1}, {-3, -2, 3, -1}, {0, 1, 0, 0}, {1, 0, 0, 0}}}, 2};

BasisDef const kPower = {{{{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}}}, 4};

struct SurfaceDef {
  bool bicubic = false;
  BasisDef uBasis = kBezier;
  BasisDef vBasis = kBezier;
  RtInt nu = 0;
  RtInt nv = 0;
  bool uPeriodic = false;
  bool vPeriodic = false;
  std::vector<RtFloat> p;
};

double cubicWeight(Matrix4 const& m, double t, std::size_t k) {
  return t * t * t * m[0][k] + t * t * m[1][k] + t * m[2][k] + m[3][k];
}

struct Cell {
  RtInt start;
  double local;
};

Cell cellOf(double t, RtInt patches) {
  RtInt const start = std::clamp((RtInt)std::floor(t * patches), 0, patches - 1);
  return {start, t * patches - start};
}

Vec3 controlPoint(SurfaceDef const& s, RtInt i, RtInt j) {
  std::size_t const base = 3 * ((std::size_t)(i % s.nu) + (std::size_t)s.nu * (std::size_t)(j % s.nv));
  return {s.p[base], s.p[base + 1], s.p[base + 2]};
}

Vec3 evaluate(SurfaceDef const& s, double u, double v) {
  Vec3 result = {0, 0, 0};
  auto const add = [&result](double weight, Vec3 const& point) {
    for (std::size_t axis = 0; axis < 3; ++axis) {
      result[axis] += weight * point[axis];
    }
  };
  if (s.bicubic) {
    RtInt uPatches = (s.nu - 4) / s.uBasis.step + 1;
    if (s.uPeriodic) {
      uPatches = s.nu / s.uBasis.step;
    }
    RtInt vPatches = (s.nv - 4) / s.vBasis.step + 1;
    if (s.vPeriodic) {
      vPatches = s.nv / s.vBasis.step;
    }
    Cell const uCell = cellOf(u, uPatches);
    Cell const vCell = cellOf(v, vPatches);
    for (std::size_t j = 0; j < 4; ++j) {
      for (std::size_t i = 0; i < 4; ++i) {
        double const weight =
            cubicWeight(s.uBasis.matrix, uCell.local, i) * cubicWeight(s.vBasis.matrix, vCell.local, j);
        add(weight, controlPoint(s, uCell.start * s.uBasis.step + (RtInt)i, vCell.start * s.vBasis.step + (RtInt)j));
      }
    }
    return result;
  }
  Cell const uCell = cellOf(u, s.uPeriodic ? s.nu : s.nu - 1);
  Cell const vCell = cellOf(v, s.vPeriodic ? s.nv : s.nv - 1);
  add((1 - uCell.local) * (1 - vCell.local), controlPoint(s, uCell.start, vCell.start));
  add(uCell.local * (1 - vCell.local), controlPoint(s, uCell.start + 1, vCell.start));
  add((1 - uCell.local) * vCell.local, controlPoint(s, uCell.start, vCell.start + 1));
  add(uCell.local * vCell.local, controlPoint(s, uCell.start + 1, vCell.start + 1));
  return result;
}

Vec3 operator-(Vec3 const& a, Vec3 const& b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }

Vec3 cross(Vec3 const& a, Vec3 const& b) {
  return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}

double dot(Vec3 const& a, Vec3 const& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

double length(Vec3 const& a) { return std::sqrt(dot(a, a)); }

Vec3 surfaceCross(SurfaceDef const& s, double u, double v) {
  constexpr double h = 1e-5;
  Vec3 const du = evaluate(s, u + h, v) - evaluate(s, u - h, v);
  Vec3 const dv = evaluate(s, u, v + h) - evaluate(s, u, v - h);
  return cross(du, dv);
}

double angleDegrees(Vec3 const& a, Vec3 const& b) {
  double const cosine = std::clamp(dot(a, b) / (length(a) * length(b)), -1.0, 1.0);
  return std::acos(cosine) * 180.0 / std::numbers::pi;
}

double extentOf(std::vector<RtFloat> const& p) {
  std::array<double, 3> lo = {1e30, 1e30, 1e30};
  std::array<double, 3> hi = {-1e30, -1e30, -1e30};
  for (std::size_t i = 0; i + 2 < p.size(); i += 3) {
    for (std::size_t axis = 0; axis < 3; ++axis) {
      lo[axis] = std::min(lo[axis], (double)p[i + axis]);
      hi[axis] = std::max(hi[axis], (double)p[i + axis]);
    }
  }
  return std::max({hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2]});
}

void setBasis(GMANAttributes& attr, BasisDef const& u, BasisDef const& v) {
  RtBasis ub;
  RtBasis vb;
  for (std::size_t r = 0; r < 4; ++r) {
    for (std::size_t c = 0; c < 4; ++c) {
      ub[r][c] = (RtFloat)u.matrix[r][c];
      vb[r][c] = (RtFloat)v.matrix[r][c];
    }
  }
  attr.setUVBasis(ub, u.step, vb, v.step);
}

struct Param {
  RtToken token;
  std::vector<RtFloat> values;
};

struct Counts {
  RtInt vertex;
  RtInt varying;
  RtInt uniform;
};

GMANParameterList makeList(std::vector<Param>& params, Counts counts) {
  GMANDictionary dictionary;
  std::vector<RtToken> tokens;
  std::vector<RtPointer> data;
  for (Param& param : params) {
    tokens.push_back(param.token);
    data.push_back((RtPointer)param.values.data());
  }
  return GMANParameterList(dictionary, (RtInt)params.size(), tokens.data(), data.data(), counts.vertex, counts.varying,
                           counts.uniform);
}

struct Request {
  GMANRayPrimitiveFactory manager;
  GMANOptions options;
  GMANAttributes attr;
  GMANTransform transform;
  std::unique_ptr<GMANPrimitive> primitive;

  GMANRayPolygonMesh const* mesh() const { return dynamic_cast<GMANRayPolygonMesh const*>(primitive.get()); }

  void patch(char const* type, std::vector<Param> params, Counts counts) {
    GMANParameterList const pl = makeList(params, counts);
    primitive = manager.getRSPatch((RtToken)type, pl, options, attr, transform);
  }

  void patchMesh(char const* type, RtInt nu, bool uPeriodic, RtInt nv, bool vPeriodic, std::vector<Param> params,
                 Counts counts) {
    GMANParameterList const pl = makeList(params, counts);
    primitive = manager.getRSPatchMesh((RtToken)type, nu, uPeriodic ? RI_PERIODIC : RI_NONPERIODIC, nv,
                                       vPeriodic ? RI_PERIODIC : RI_NONPERIODIC, pl, options, attr, transform);
  }
};

using PointKey = std::array<RtFloat, 3>;
using Edge = std::pair<PointKey, PointKey>;

PointKey keyOf(GMANPoint const& p) { return {p.getX(), p.getY(), p.getZ()}; }

struct MeshStats {
  std::set<PointKey> vertices;
  std::map<Edge, int> edgeUse;
  std::size_t degenerateFaces = 0;
  bool allFinite = true;
};

MeshStats analyze(GMANRayPolygonMesh const& mesh) {
  MeshStats stats;
  for (std::size_t f = 0; f < mesh.getFaceCount(); ++f) {
    GMANRayPolygon const& face = mesh.getFace(f);
    if (face.isDegenerate()) {
      ++stats.degenerateFaces;
    }
    std::vector<GMANPoint> const& ring = face.getOuterLoop();
    for (std::size_t i = 0; i < ring.size(); ++i) {
      PointKey const a = keyOf(ring[i]);
      PointKey const b = keyOf(ring[(i + 1) % ring.size()]);
      for (RtFloat const value : a) {
        stats.allFinite = stats.allFinite && std::isfinite(value);
      }
      stats.vertices.insert(a);
      ++stats.edgeUse[std::minmax(a, b)];
    }
  }
  return stats;
}

double distanceTo(std::set<PointKey> const& vertices, Vec3 const& target) {
  double best = std::numeric_limits<double>::infinity();
  for (PointKey const& v : vertices) {
    best = std::min(best, length(Vec3{v[0], v[1], v[2]} - target));
  }
  return best;
}

bool hasVertexNear(MeshStats const& stats, Vec3 const& target, double tolerance) {
  return distanceTo(stats.vertices, target) <= tolerance;
}

std::size_t edgesUsed(MeshStats const& stats, int times) {
  return (std::size_t)std::count_if(stats.edgeUse.begin(), stats.edgeUse.end(),
                                    [times](auto const& entry) { return entry.second == times; });
}

GMANRay rayAt(Vec3 const& origin, Vec3 const& through) {
  Vec3 const d = through - origin;
  return GMANRay(GMANPoint(origin[0], origin[1], origin[2]), GMANVector(d[0], d[1], d[2]));
}

Vec3 const kCamera = {0, 0, -5};

bool hits(GMANRayPolygonMesh const& mesh, GMANRay const& ray) {
  GMANHit hit;
  return mesh.intersect(ray, hit);
}

std::vector<RtFloat> floatsOf(std::vector<double> const& values) {
  return std::vector<RtFloat>(values.begin(), values.end());
}

std::vector<RtFloat> bicubicNet(double skew) {
  std::vector<double> p;
  for (int j = 0; j < 4; ++j) {
    for (int i = 0; i < 4; ++i) {
      p.push_back(-1.2 + 0.8 * i + 0.15 * j);
      p.push_back(-1.1 + 0.7 * j - 0.1 * i);
      p.push_back(skew * (0.3 * i * j - 0.2 * i + 0.25 * j * (j - 2.0) + 0.1 * i * i));
    }
  }
  return floatsOf(p);
}

std::vector<RtFloat> twistedNet(double twist) {
  std::vector<double> p;
  for (int j = 0; j < 4; ++j) {
    for (int i = 0; i < 4; ++i) {
      double const x = -1.0 + 2.0 * i / 3.0;
      double const y = -1.0 + 2.0 * j / 3.0;
      p.push_back(x);
      p.push_back(y);
      p.push_back(twist * x * y + 0.25 * (i - j));
    }
  }
  return floatsOf(p);
}

SurfaceDef bicubicPatchDef(std::vector<RtFloat> p, BasisDef const& u, BasisDef const& v) {
  SurfaceDef s;
  s.bicubic = true;
  s.uBasis = u;
  s.vBasis = v;
  s.nu = 4;
  s.nv = 4;
  s.p = std::move(p);
  return s;
}

constexpr RtInt kGrid = gman::kParametricDiceCount;
constexpr std::size_t kTriangles = 2 * (std::size_t)kGrid * (std::size_t)kGrid;
constexpr std::size_t kLines = (std::size_t)kGrid + 1;

double gridU(RtInt i) { return i / (double)kGrid; }

bool hitTexCoordNear(GMANRayPolygonMesh const& mesh, Vec3 const& target, Vec3 const& origin, double s, double t) {
  GMANHit hit;
  if (!mesh.intersect(rayAt(origin, target), hit)) {
    return false;
  }
  return std::fabs(hit.u - s) < 1e-4 && std::fabs(hit.v - t) < 1e-4;
}

double bilerp(double u, double v, double c00, double c10, double c01, double c11) {
  return (1 - u) * (1 - v) * c00 + u * (1 - v) * c10 + (1 - u) * v * c01 + u * v * c11;
}

SurfaceDef flatMeshDef(RtInt nu, RtInt nv) {
  SurfaceDef s;
  s.nu = nu;
  s.nv = nv;
  std::vector<double> p;
  for (RtInt j = 0; j < nv; ++j) {
    for (RtInt i = 0; i < nu; ++i) {
      p.insert(p.end(), {-1.0 + 2.0 * i / (nu - 1), -1.0 + 2.0 * j / (nv - 1), 0.0});
    }
  }
  s.p = floatsOf(p);
  return s;
}

Vec3 outsideOf(Vec3 const& target) {
  double const radius = std::hypot(target[0], target[1]);
  return {target[0] + 3 * target[0] / radius, target[1] + 3 * target[1] / radius, target[2]};
}

Vec3 frontOf(Vec3 const& target) { return {target[0], target[1], -5.0}; }

void testBilinearPatch() {
  SurfaceDef s;
  s.nu = 2;
  s.nv = 2;
  s.p = {-1.0f, -0.8f, 0.2f, 1.1f, -1.0f, -0.1f, -0.9f, 1.2f, 0.4f, 0.8f, 0.9f, -0.6f};

  Request request;
  request.patch("bilinear", {{RI_P, s.p}}, {4, 4, 1});
  GMANRayPolygonMesh const* mesh = request.mesh();
  check(mesh != nullptr, "bilinear Patch: a triangle mesh");
  if (mesh == nullptr) {
    return;
  }
  check(mesh->getFaceCount() == kTriangles, "bilinear Patch: 2 * 16 * 16 faces");

  MeshStats const stats = analyze(*mesh);
  double const tolerance = 1e-5 * extentOf(s.p);
  check(stats.vertices.size() == kLines * kLines, "bilinear Patch: 17 x 17 distinct vertices");
  check(hasVertexNear(stats, evaluate(s, 0, 0), tolerance) && hasVertexNear(stats, evaluate(s, 1, 0), tolerance) &&
            hasVertexNear(stats, evaluate(s, 0, 1), tolerance) && hasVertexNear(stats, evaluate(s, 1, 1), tolerance),
        "bilinear Patch: the corner vertices are the four control points");
  check(hasVertexNear(stats, evaluate(s, gridU(4), gridU(12)), tolerance),
        "bilinear Patch: the vertex at (4/16, 12/16) lies on the bilinear surface");
  bool cornersTagged = true;
  for (std::pair<double, double> const& corner :
       std::array<std::pair<double, double>, 4>{{{0, 0}, {1, 0}, {0, 1}, {1, 1}}}) {
    Vec3 const target = evaluate(s, corner.first, corner.second);
    cornersTagged = cornersTagged && hitTexCoordNear(*mesh, target, frontOf(target), corner.first, corner.second);
  }
  check(cornersTagged, "bilinear Patch: each control point carries its own parametric corner");
}

void testBicubicPatchBases() {
  struct Named {
    char const* name;
    BasisDef basis;
  };
  std::array<Named, 5> const bases = {{{"Bezier", kBezier},
                                       {"B-spline", kBSpline},
                                       {"Catmull-Rom", kCatmullRom},
                                       {"Hermite", kHermite},
                                       {"power", kPower}}};
  for (Named const& named : bases) {
    SurfaceDef const s = bicubicPatchDef(bicubicNet(1.0), named.basis, named.basis);
    Request request;
    setBasis(request.attr, named.basis, named.basis);
    request.patch("bicubic", {{RI_P, s.p}}, {16, 4, 1});
    std::string const label = std::string("bicubic Patch, ") + named.name + ": ";
    GMANRayPolygonMesh const* mesh = request.mesh();
    check(mesh != nullptr, label + "a triangle mesh");
    if (mesh == nullptr) {
      continue;
    }
    MeshStats const stats = analyze(*mesh);
    check(stats.vertices.size() == kLines * kLines, label + "17 x 17 distinct vertices");
    double const tolerance = 1e-5 * extentOf(s.p);
    std::array<std::pair<RtInt, RtInt>, 5> const samples = {{{0, 0}, {kGrid, 0}, {0, kGrid}, {kGrid, kGrid}, {6, 10}}};
    bool onSurface = true;
    for (std::pair<RtInt, RtInt> const& sample : samples) {
      onSurface = onSurface && hasVertexNear(stats, evaluate(s, gridU(sample.first), gridU(sample.second)), tolerance);
    }
    check(onSurface, label + "the corners and an interior grid point equal the blended surface");
  }
}

SurfaceDef bezierMeshDef() {
  SurfaceDef s;
  s.bicubic = true;
  s.nu = 7;
  s.nv = 7;
  std::vector<double> p;
  for (int j = 0; j < 7; ++j) {
    for (int i = 0; i < 7; ++i) {
      p.push_back(-1.5 + 0.5 * i + 0.05 * j);
      p.push_back(-1.5 + 0.5 * j - 0.04 * i);
      p.push_back(0.3 * ((i * j) % 4) - 0.1 * i + 0.05 * j * j);
    }
  }
  s.p = floatsOf(p);
  return s;
}

void testBicubicPatchMesh() {
  SurfaceDef const s = bezierMeshDef();
  Request request;
  request.patchMesh("bicubic", 7, false, 7, false, {{RI_P, s.p}}, {49, 9, 4});
  GMANRayPolygonMesh const* mesh = request.mesh();
  check(mesh != nullptr, "bicubic PatchMesh: a triangle mesh");
  if (mesh == nullptr) {
    return;
  }
  check(mesh->getFaceCount() == kTriangles, "bicubic PatchMesh: 2 * 16 * 16 faces");
  MeshStats const stats = analyze(*mesh);
  check(stats.vertices.size() == kLines * kLines, "bicubic PatchMesh: 17 x 17 distinct vertices");

  std::vector<RtFloat> flat = s.p;
  GMANBasis const basis = request.attr.getUVBasis();
  GMANPatchMesh library(RI_BICUBIC, flat.data(), 7, RI_NONPERIODIC, 7, RI_NONPERIODIC, basis, GMANParameterList());
  bool everyGridPoint = true;
  for (RtInt i = 0; i <= kGrid; ++i) {
    for (RtInt j = 0; j <= kGrid; ++j) {
      GMANPoint const expected = library.getLocation(gridU(i), gridU(j));
      everyGridPoint = everyGridPoint && stats.vertices.count(keyOf(expected)) == 1;
    }
  }
  check(everyGridPoint, "bicubic PatchMesh: every vertex is GMANPatchMesh::getLocation at (i/16, j/16)");

  double const tolerance = 1e-5 * extentOf(s.p);
  bool cornersShared = true;
  for (RtInt gi : {0, 8, 16}) {
    for (RtInt gj : {0, 8, 16}) {
      cornersShared = cornersShared && hasVertexNear(stats, controlPoint(s, gi / 8 * 3, gj / 8 * 3), tolerance);
    }
  }
  check(cornersShared, "bicubic PatchMesh: the sub-patch corners are the shared control points");
}

SurfaceDef bsplineTubeDef() {
  SurfaceDef s;
  s.bicubic = true;
  s.uBasis = kBSpline;
  s.vBasis = kBSpline;
  s.nu = 4;
  s.nv = 4;
  s.uPeriodic = true;
  std::vector<double> p;
  for (int j = 0; j < 4; ++j) {
    for (int i = 0; i < 4; ++i) {
      double const theta = 2.0 * std::numbers::pi * i / 4.0;
      p.push_back(std::cos(theta) * (1.0 + 0.1 * j));
      p.push_back(std::sin(theta) * (1.0 + 0.1 * j));
      p.push_back(0.7 * j - 1.05);
    }
  }
  s.p = floatsOf(p);
  return s;
}

SurfaceDef torusDef() {
  SurfaceDef s;
  s.nu = 4;
  s.nv = 3;
  s.uPeriodic = true;
  s.vPeriodic = true;
  std::vector<double> p;
  for (int j = 0; j < 3; ++j) {
    for (int i = 0; i < 4; ++i) {
      double const theta = 2.0 * std::numbers::pi * i / 4.0;
      double const phi = 2.0 * std::numbers::pi * j / 3.0;
      double const ring = 1.5 + 0.5 * std::cos(phi);
      p.push_back(ring * std::cos(theta));
      p.push_back(ring * std::sin(theta));
      p.push_back(0.5 * std::sin(phi));
    }
  }
  s.p = floatsOf(p);
  return s;
}

bool seamRaysHit(GMANRayPolygonMesh const& mesh, SurfaceDef const& s, std::initializer_list<RtInt> rows) {
  bool allHit = true;
  for (RtInt j : rows) {
    for (double u : {0.0, 0.004, 0.996}) {
      Vec3 const target = evaluate(s, u, gridU(j));
      allHit = allHit && hits(mesh, rayAt(outsideOf(target), target));
    }
  }
  return allHit;
}

void testWrap() {
  SurfaceDef const tube = bsplineTubeDef();
  Request tubeRequest;
  setBasis(tubeRequest.attr, kBSpline, kBSpline);
  tubeRequest.patchMesh("bicubic", 4, true, 4, false, {{RI_P, tube.p}}, {16, 8, 4});
  GMANRayPolygonMesh const* tubeMesh = tubeRequest.mesh();
  check(tubeMesh != nullptr, "periodic bicubic PatchMesh: a triangle mesh");
  if (tubeMesh != nullptr) {
    MeshStats const stats = analyze(*tubeMesh);
    check(stats.vertices.size() == (std::size_t)kGrid * kLines,
          "periodic bicubic PatchMesh: 16 x 17 distinct vertices, the seam evaluated once");
    double const tolerance = 1e-5 * extentOf(tube.p);
    bool onSurface = true;
    for (RtInt i = 0; i < kGrid; ++i) {
      for (RtInt j = 0; j <= kGrid; ++j) {
        onSurface = onSurface && hasVertexNear(stats, evaluate(tube, gridU(i), gridU(j)), tolerance);
      }
    }
    check(onSurface, "periodic bicubic PatchMesh: every vertex equals the blended surface");
    check(edgesUsed(stats, 1) == 2 * (std::size_t)kGrid &&
              edgesUsed(stats, 2) + edgesUsed(stats, 1) == stats.edgeUse.size(),
          "periodic bicubic PatchMesh: only the 32 edges along the open v boundary are unshared, the seam included");
    check(seamRaysHit(*tubeMesh, tube, {1, 4, 8, 12, 15}), "periodic bicubic PatchMesh: rays at the seam hit");
  }

  SurfaceDef const torus = torusDef();
  Request torusRequest;
  torusRequest.patchMesh("bilinear", 4, true, 3, true, {{RI_P, torus.p}}, {12, 12, 12});
  GMANRayPolygonMesh const* torusMesh = torusRequest.mesh();
  check(torusMesh != nullptr, "doubly periodic bilinear PatchMesh: a triangle mesh");
  if (torusMesh != nullptr) {
    MeshStats const stats = analyze(*torusMesh);
    check(stats.vertices.size() == (std::size_t)kGrid * (std::size_t)kGrid,
          "doubly periodic bilinear PatchMesh: 16 x 16 distinct vertices");
    double const tolerance = 1e-5 * extentOf(torus.p);
    bool onSurface = true;
    for (RtInt i = 0; i < kGrid; ++i) {
      for (RtInt j = 0; j < kGrid; ++j) {
        onSurface = onSurface && hasVertexNear(stats, evaluate(torus, gridU(i), gridU(j)), tolerance);
      }
    }
    check(onSurface, "doubly periodic bilinear PatchMesh: every vertex equals the blended surface");
    check(edgesUsed(stats, 2) == stats.edgeUse.size(),
          "doubly periodic bilinear PatchMesh: every edge is shared by two triangles");
    check(seamRaysHit(*torusMesh, torus, {0, 4, 8, 12, 15}),
          "doubly periodic bilinear PatchMesh: rays at the seams hit");
  }
}

std::size_t countMisses(GMANRayPolygonMesh const& mesh, SurfaceDef const& s, int samples) {
  std::size_t misses = 0;
  for (int a = 0; a < samples; ++a) {
    for (int b = 0; b < samples; ++b) {
      double const u = 0.02 + 0.96 * (a + 0.5) / samples;
      double const v = 0.02 + 0.96 * (b + 0.5) / samples;
      if (!hits(mesh, rayAt(kCamera, evaluate(s, u, v)))) {
        ++misses;
      }
    }
  }
  return misses;
}

void testWatertight() {
  SurfaceDef const twisted = bicubicPatchDef(twistedNet(1.8), kBezier, kBezier);
  Request request;
  request.patch("bicubic", {{RI_P, twisted.p}}, {16, 4, 1});
  GMANRayPolygonMesh const* mesh = request.mesh();
  check(mesh != nullptr, "twisted bicubic Patch: a triangle mesh");
  if (mesh != nullptr) {
    std::size_t const misses = countMisses(*mesh, twisted, 400);
    std::printf("twisted bicubic Patch: %zu of 160000 rays missed\n", misses);
    check(misses == 0, "twisted bicubic Patch: all 160000 rays through the surface hit the mesh");
  }

  std::vector<double> lower;
  std::vector<double> upper;
  for (int j = 0; j < 4; ++j) {
    for (int i = 0; i < 4; ++i) {
      double const x = -1.0 + 2.0 * i / 3.0;
      double const wiggle = 0.3 * ((i * 5) % 3) - 0.2;
      lower.insert(lower.end(), {x, -1.6 + 0.5 * j, 0.7 * x * (-1.6 + 0.5 * j) + 0.2 * i - 0.1 * j});
      upper.insert(upper.end(), {x, -0.1 + 0.5 * j, 0.7 * x * (-0.1 + 0.5 * j) + 0.2 * i - 0.1 * j + wiggle});
    }
  }
  for (int i = 0; i < 4; ++i) {
    for (std::size_t axis = 0; axis < 3; ++axis) {
      upper[3 * i + axis] = lower[3 * (12 + i) + axis];
    }
  }
  SurfaceDef const lowerDef = bicubicPatchDef(floatsOf(lower), kBezier, kBezier);
  SurfaceDef const upperDef = bicubicPatchDef(floatsOf(upper), kBezier, kBezier);
  Request lowerRequest;
  Request upperRequest;
  lowerRequest.patch("bicubic", {{RI_P, lowerDef.p}}, {16, 4, 1});
  upperRequest.patch("bicubic", {{RI_P, upperDef.p}}, {16, 4, 1});
  GMANRayPolygonMesh const* lowerMesh = lowerRequest.mesh();
  GMANRayPolygonMesh const* upperMesh = upperRequest.mesh();
  check(lowerMesh != nullptr && upperMesh != nullptr, "two Patches sharing an edge: both are triangle meshes");
  if (lowerMesh == nullptr || upperMesh == nullptr) {
    return;
  }
  std::size_t misses = 0;
  constexpr int kEdgeSamples = 2000;
  for (int k = 0; k < kEdgeSamples; ++k) {
    double const u = (k + 0.5) / kEdgeSamples;
    Vec3 const point = evaluate(lowerDef, u, 1.0);
    GMANRay const ray = rayAt(kCamera, point);
    if (!hits(*lowerMesh, ray) && !hits(*upperMesh, ray)) {
      ++misses;
    }
  }
  std::printf("two Patches sharing an edge: %zu of %d rays missed\n", misses, kEdgeSamples);
  check(misses == 0, "two Patches sharing an edge: every ray through the shared edge hits one of them");
}

SurfaceDef domeDef() {
  std::vector<double> p;
  for (int j = 0; j < 4; ++j) {
    for (int i = 0; i < 4; ++i) {
      double const x = -1.0 + 2.0 * i / 3.0;
      double const y = -1.0 + 2.0 * j / 3.0;
      p.insert(p.end(), {x, y, 0.9 - 0.7 * x * x - 0.5 * y * y + 0.3 * x * y + 0.4 * x});
    }
  }
  return bicubicPatchDef(floatsOf(p), kBezier, kBezier);
}

void testNormals() {
  SurfaceDef const dome = domeDef();
  Request request;
  request.patch("bicubic", {{RI_P, dome.p}}, {16, 4, 1});
  GMANRayPolygonMesh const* mesh = request.mesh();
  check(mesh != nullptr, "normals: a triangle mesh");
  if (mesh == nullptr) {
    return;
  }
  bool shadingPresent = true;
  bool shadingClose = true;
  bool shadingNearer = true;
  bool facetOriented = true;
  for (double us : {0.13, 0.29, 0.41, 0.57, 0.71, 0.87}) {
    for (double vs : {0.13, 0.29, 0.41, 0.57, 0.71, 0.87}) {
      GMANHit hit;
      if (!mesh->intersect(rayAt(kCamera, evaluate(dome, us, vs)), hit)) {
        shadingPresent = false;
        continue;
      }
      Vec3 const analytic = surfaceCross(dome, hit.u, hit.v);
      Vec3 const facet = {hit.normal.getX(), hit.normal.getY(), hit.normal.getZ()};
      facetOriented = facetOriented && dot(facet, analytic) > 0;
      if (!hit.shadingNormal) {
        shadingPresent = false;
        continue;
      }
      Vec3 const shading = {hit.shadingNormal->getX(), hit.shadingNormal->getY(), hit.shadingNormal->getZ()};
      double const shadingError = angleDegrees(shading, analytic);
      shadingClose = shadingClose && shadingError < 1.0;
      shadingNearer = shadingNearer && shadingError < angleDegrees(facet, analytic);
    }
  }
  check(shadingPresent, "normals: every hit carries a shading normal");
  check(shadingClose, "normals: the shading normal lies within 1 degree of the analytic normal");
  check(shadingNearer, "normals: the shading normal is nearer the analytic normal than the facet normal");
  check(facetOriented, "normals: the facet normal follows dP/du x dP/dv");
}

void testPatchTexCoords() {
  SurfaceDef const flat = flatMeshDef(2, 2);
  std::vector<RtFloat> const st = {0.1f, 0.2f, 0.9f, 0.3f, 0.4f, 0.8f, 0.7f, 0.6f};
  std::vector<RtFloat> const sOnly = {0.5f, 0.0f, 0.2f, 1.0f};
  for (bool withS : {false, true}) {
    Request request;
    std::vector<Param> params = {{RI_P, flat.p}, {RI_ST, st}};
    if (withS) {
      params.push_back({RI_S, sOnly});
    }
    request.patch("bilinear", params, {4, 4, 1});
    GMANRayPolygonMesh const* mesh = request.mesh();
    std::string const label = withS ? "Patch with \"st\" and \"s\": " : "Patch with \"st\": ";
    check(mesh != nullptr, label + "a triangle mesh");
    if (mesh == nullptr) {
      continue;
    }
    bool allMatch = true;
    for (std::pair<RtInt, RtInt> const& g : std::array<std::pair<RtInt, RtInt>, 3>{{{4, 12}, {8, 8}, {13, 3}}}) {
      double const u = gridU(g.first);
      double const v = gridU(g.second);
      Vec3 const target = evaluate(flat, u, v);
      double s = bilerp(u, v, st[0], st[2], st[4], st[6]);
      if (withS) {
        s = bilerp(u, v, sOnly[0], sOnly[1], sOnly[2], sOnly[3]);
      }
      double const t = bilerp(u, v, st[1], st[3], st[5], st[7]);
      allMatch = allMatch && hitTexCoordNear(*mesh, target, frontOf(target), s, t);
    }
    check(allMatch, label + "(s, t) is the bilinear blend of the corners, \"s\" overriding \"st\"");
  }
}

void testShortCornerValues() {
  SurfaceDef const flat = flatMeshDef(2, 2);
  Request request;
  request.patch("bilinear", {{RI_P, flat.p}, {RI_ST, {0.9f, 0.9f, 0.9f, 0.9f}}}, {4, 2, 1});
  GMANRayPolygonMesh const* mesh = request.mesh();
  check(mesh != nullptr, "Patch with a short \"st\": a triangle mesh");
  if (mesh == nullptr) {
    return;
  }
  Vec3 const target = evaluate(flat, gridU(4), gridU(12));
  check(hitTexCoordNear(*mesh, target, frontOf(target), gridU(4), gridU(12)),
        "Patch with a short \"st\": the corners stay (s, t) = (u, v)");
}

void testPatchMeshTexCoords() {
  SurfaceDef const flat = flatMeshDef(3, 2);
  for (bool uniformS : {false, true}) {
    Request request;
    request.attr.setTextureCoordinates(0, 0, 2, 0, 0, 2, 2, 2);
    std::vector<Param> params = {{RI_P, flat.p}};
    if (uniformS) {
      params.push_back({(RtToken) "uniform float s", {9.0f, 9.0f, 9.0f, 9.0f, 9.0f, 9.0f}});
    }
    request.patchMesh("bilinear", 3, false, 2, false, params, {6, 6, 6});
    GMANRayPolygonMesh const* mesh = request.mesh();
    std::string const label = uniformS ? "PatchMesh with a uniform \"s\": " : "PatchMesh: ";
    check(mesh != nullptr, label + "a triangle mesh");
    if (mesh == nullptr) {
      continue;
    }
    bool across = true;
    for (RtInt i : {2, 6, 10, 14}) {
      for (RtInt j : {4, 12}) {
        Vec3 const target = evaluate(flat, gridU(i), gridU(j));
        across = across && hitTexCoordNear(*mesh, target, frontOf(target), 2 * gridU(i), 2 * gridU(j));
      }
    }
    check(across, label + "RiTextureCoordinates spans the whole mesh, s = 2u and t = 2v");
  }

  SurfaceDef box;
  box.nu = 4;
  box.nv = 2;
  box.uPeriodic = true;
  box.p = {1, 0, -0.5f, 0, 1, -0.5f, -1, 0, -0.5f, 0, -1, -0.5f, 1, 0, 0.5f, 0, 1, 0.5f, -1, 0, 0.5f, 0, -1, 0.5f};
  std::vector<RtFloat> const st = {0.0f, 0.5f, 0.2f, 0.1f, 0.5f, 0.9f, 0.9f, 0.3f,
                                   0.3f, 0.7f, 0.6f, 0.2f, 0.8f, 0.8f, 0.1f, 0.4f};
  Request request;
  request.patchMesh("bilinear", 4, true, 2, false, {{RI_P, box.p}, {RI_ST, st}}, {8, 8, 4});
  GMANRayPolygonMesh const* mesh = request.mesh();
  check(mesh != nullptr, "periodic PatchMesh with varying \"st\": a triangle mesh");
  if (mesh == nullptr) {
    return;
  }
  bool wrapped = true;
  for (RtInt i : {13, 14, 15}) {
    for (RtInt j : {4, 12}) {
      double const u = gridU(i);
      double const v = gridU(j);
      double const local = u * 4 - 3;
      double const s = bilerp(local, v, st[2 * 3], st[2 * 0], st[2 * 7], st[2 * 4]);
      double const t = bilerp(local, v, st[2 * 3 + 1], st[2 * 0 + 1], st[2 * 7 + 1], st[2 * 4 + 1]);
      Vec3 const target = evaluate(box, u, v);
      wrapped = wrapped && hitTexCoordNear(*mesh, target, outsideOf(target), s, t);
    }
  }
  check(wrapped, "periodic PatchMesh: the last sub-patch blends corner 3 with corner 0");

  Request cornerMapped;
  cornerMapped.patchMesh("bilinear", 4, true, 2, false, {{RI_P, box.p}}, {8, 8, 4});
  GMANRayPolygonMesh const* cornerMesh = cornerMapped.mesh();
  check(cornerMesh != nullptr, "periodic PatchMesh without \"st\": a triangle mesh");
  if (cornerMesh != nullptr) {
    bool followsU = true;
    for (double u : {0.95, 0.97, 0.99}) {
      Vec3 const target = evaluate(box, u, 0.5);
      followsU = followsU && hitTexCoordNear(*cornerMesh, target, outsideOf(target), u, 0.5);
    }
    check(followsU, "periodic PatchMesh without \"st\": s = u across the last strip");
  }
}

void testVaryingMeshTexCoords() {
  SurfaceDef const tube = bsplineTubeDef();
  std::vector<RtFloat> const tubeSt = {0.1f, 0.9f, 0.4f, 0.2f, 0.7f, 0.5f, 0.3f, 0.6f,
                                       0.8f, 0.1f, 0.2f, 0.7f, 0.6f, 0.3f, 0.5f, 0.4f};
  Request tubeRequest;
  setBasis(tubeRequest.attr, kBSpline, kBSpline);
  tubeRequest.patchMesh("bicubic", 4, true, 4, false, {{RI_P, tube.p}, {RI_ST, tubeSt}}, {16, 8, 4});
  GMANRayPolygonMesh const* tubeMesh = tubeRequest.mesh();
  check(tubeMesh != nullptr, "periodic bicubic PatchMesh with varying \"st\": a triangle mesh");
  if (tubeMesh != nullptr) {
    bool blended = true;
    for (std::pair<RtInt, RtInt> const& g : std::array<std::pair<RtInt, RtInt>, 3>{{{5, 6}, {10, 9}, {14, 3}}}) {
      double const u = gridU(g.first);
      double const v = gridU(g.second);
      double const local = u * 4 - std::floor(u * 4);
      std::size_t const first = (std::size_t)std::floor(u * 4);
      std::size_t const next = (first + 1) % 4;
      double const s =
          bilerp(local, v, tubeSt[2 * first], tubeSt[2 * next], tubeSt[2 * (first + 4)], tubeSt[2 * (next + 4)]);
      double const t = bilerp(local, v, tubeSt[2 * first + 1], tubeSt[2 * next + 1], tubeSt[2 * (first + 4) + 1],
                              tubeSt[2 * (next + 4) + 1]);
      Vec3 const target = evaluate(tube, u, v);
      blended = blended && hitTexCoordNear(*tubeMesh, target, outsideOf(target), s, t);
    }
    check(blended, "periodic bicubic PatchMesh: varying \"st\" blends within a sub-patch");
  }

  SurfaceDef const flat = flatMeshDef(3, 2);
  std::vector<RtFloat> const st = {0.1f, 0.2f, 0.9f, 0.3f, 0.4f, 0.8f, 0.7f, 0.6f, 0.2f, 0.5f, 0.3f, 0.9f};
  std::vector<RtFloat> const sOnly = {0.5f, 0.0f, 0.2f, 1.0f, 0.6f, 0.3f};
  std::vector<RtFloat> const tOnly = {0.0f, 0.4f, 0.8f, 0.2f, 0.9f, 0.1f};
  for (bool withT : {false, true}) {
    Request request;
    std::vector<Param> params = {{RI_P, flat.p}, {RI_ST, st}, {RI_S, sOnly}};
    if (withT) {
      params.push_back({RI_T, tOnly});
    }
    request.patchMesh("bilinear", 3, false, 2, false, params, {6, 6, 2});
    GMANRayPolygonMesh const* mesh = request.mesh();
    std::string label = "PatchMesh with varying \"st\" and \"s\": ";
    if (withT) {
      label = "PatchMesh with varying \"st\", \"s\" and \"t\": ";
    }
    check(mesh != nullptr, label + "a triangle mesh");
    if (mesh == nullptr) {
      continue;
    }
    bool overridden = true;
    for (std::pair<RtInt, RtInt> const& g : std::array<std::pair<RtInt, RtInt>, 3>{{{4, 4}, {12, 12}, {9, 5}}}) {
      double const u = gridU(g.first);
      double const v = gridU(g.second);
      std::size_t const first = u < 0.5 ? 0 : 1;
      double const local = u * 2 - (double)first;
      double const s = bilerp(local, v, sOnly[first], sOnly[first + 1], sOnly[first + 3], sOnly[first + 4]);
      double t = bilerp(local, v, st[2 * first + 1], st[2 * (first + 1) + 1], st[2 * (first + 3) + 1],
                        st[2 * (first + 4) + 1]);
      if (withT) {
        t = bilerp(local, v, tOnly[first], tOnly[first + 1], tOnly[first + 3], tOnly[first + 4]);
      }
      Vec3 const target = evaluate(flat, u, v);
      overridden = overridden && hitTexCoordNear(*mesh, target, frontOf(target), s, t);
    }
    check(overridden, label + "\"s\" and \"t\" override \"st\" within a sub-patch");
  }
}

void testNonFiniteAndDegenerate() {
  {
    Request request;
    std::vector<RtFloat> p = bicubicNet(1.0);
    p[17] = std::numeric_limits<RtFloat>::quiet_NaN();
    request.patch("bicubic", {{RI_P, p}}, {16, 4, 1});
    check(request.mesh() == nullptr, "a \"P\" holding a NaN returns no mesh");
  }
  {
    Request request;
    std::vector<RtFloat> p = bicubicNet(1.0);
    p[5] = std::numeric_limits<RtFloat>::infinity();
    request.patch("bicubic", {{RI_P, p}}, {16, 4, 1});
    check(request.mesh() == nullptr, "a \"P\" holding an infinity returns no mesh");
  }
  {
    Request request;
    setBasis(request.attr, kPower, kPower);
    std::vector<RtFloat> p(48, std::numeric_limits<RtFloat>::max());
    request.patch("bicubic", {{RI_P, p}}, {16, 4, 1});
    check(request.mesh() == nullptr, "a finite \"P\" whose grid overflows returns no mesh");
  }
  {
    Request request;
    std::vector<double> line;
    for (int k = 0; k < 16; ++k) {
      line.insert(line.end(), {0.1 * k - 0.8, 0.0, 0.0});
    }
    request.patch("bicubic", {{RI_P, floatsOf(line)}}, {16, 4, 1});
    check(request.mesh() == nullptr, "a patch whose control points lie on one line returns no mesh");
  }
  {
    std::vector<RtFloat> p = bicubicNet(1.0);
    for (std::size_t i = 0; i < 4; ++i) {
      for (std::size_t axis = 0; axis < 3; ++axis) {
        p[3 * (12 + i) + axis] = p[3 * 15 + axis];
      }
    }
    Request request;
    request.patch("bicubic", {{RI_P, p}}, {16, 4, 1});
    GMANRayPolygonMesh const* mesh = request.mesh();
    check(mesh != nullptr, "a patch with a collapsed edge: a triangle mesh");
    if (mesh != nullptr) {
      MeshStats const stats = analyze(*mesh);
      check(mesh->getFaceCount() < kTriangles, "a patch with a collapsed edge: fewer than 2 * 16 * 16 faces");
      check(stats.degenerateFaces == 0, "a patch with a collapsed edge: no degenerate face");
      check(stats.allFinite, "a patch with a collapsed edge: every vertex is finite");
    }
  }
}

void testRequestChecks() {
  SurfaceDef const mesh7 = bezierMeshDef();
  {
    Request request;
    request.patchMesh("bicubic", 7, false, 7, false, {{RI_P, std::vector<RtFloat>(30, 0.5f)}}, {10, 9, 4});
    check(request.mesh() == nullptr, "a PatchMesh whose \"P\" holds fewer points than nu * nv returns no mesh");
  }
  {
    Request request;
    request.patch("bilinear", {{RI_P, std::vector<RtFloat>(6, 0.5f)}}, {2, 4, 1});
    check(request.mesh() == nullptr, "a bilinear Patch whose \"P\" holds fewer than four points returns no mesh");
  }
  {
    std::vector<RtFloat> p(5 * 7 * 3, 0.5f);
    Request request;
    request.patchMesh("bicubic", 5, false, 7, false, {{RI_P, p}}, {35, 12, 4});
    check(request.mesh() == nullptr, "a nonperiodic bicubic mesh with nu = 5 returns no mesh");
  }
  {
    Request request;
    setBasis(request.attr, BasisDef{kBezier.matrix, 0}, kBezier);
    request.patchMesh("bicubic", 7, false, 7, false, {{RI_P, mesh7.p}}, {49, 9, 4});
    check(request.mesh() == nullptr, "a basis with ustep 0 returns no mesh");
  }
  {
    Request request;
    request.patch("quadratic", {{RI_P, bicubicNet(1.0)}}, {16, 4, 1});
    check(request.mesh() == nullptr, "an unknown Patch type returns no mesh");
  }
  {
    Request request;
    request.patch("bicubic", {{RI_CS, {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f}}},
                  {16, 4, 1});
    check(request.mesh() == nullptr, "a Patch without \"P\" returns no mesh");
  }
}

} // namespace

int main() {
  testBilinearPatch();
  testBicubicPatchBases();
  testBicubicPatchMesh();
  testWrap();
  testWatertight();
  testNormals();
  testPatchTexCoords();
  testShortCornerValues();
  testPatchMeshTexCoords();
  testVaryingMeshTexCoords();
  testNonFiniteAndDegenerate();
  testRequestChecks();

  return checkSummary("Patch and PatchMesh tessellate into watertight, correctly shaded triangle meshes");
}
