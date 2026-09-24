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
 * GMANRadiosityMesh over a holed GeneralPolygon, a PointsPolygons mesh and
 * a PointsGeneralPolygons face with a hole: element areas against the
 * analytic outer-minus-hole area, node inside flags around a hole, and
 * locate() against hits a GMANRayBVH reports on a mesh face.
 */

#include <cmath>
#include <cstdio>
#include <vector>

#include "check.h"
#include "gmanattributes.h"
#include "gmandictionary.h"
#include "gmanlinearworldmanager.h"
#include "gmanoptions.h"
#include "gmanparameterlist.h"
#include "gmanradiositymesh.h"
#include "gmanray.h"
#include "gmanraybvh.h"
#include "gmanrayinterface.h"
#include "gmanrayobjectmanager.h"
#include "gmanraypolygon.h"
#include "gmanraypolygonmesh.h"
#include "gmantransform.h"
#include "gmanvector.h"

namespace {

// A clipped polygon's own elements sum to its analytic area within this
// relative tolerance -- exact geometry (Sutherland-Hodgman clipping), so
// only float rounding to spend, the same bound
// tests/radiositymesh_test.cpp's own kPolygonAreaRelTolerance uses.
constexpr RtFloat kAreaRelTolerance = 1e-5f;

// A hit's own point, reproduced from its located element's four corner
// positions and weights, must match within this fraction of the cube's own
// edge length -- flat faces, so the round trip is exact up to float
// rounding.
constexpr RtFloat kRoundTripTolerance = 1e-3f;

// ---------------------------------------------------------------------
// Fixture construction, through GMANRayObjectManager's own factories.
// ---------------------------------------------------------------------

GMANParameterList makePointList(GMANDictionary& dictionary, std::vector<RtFloat> const& p, RtInt vertex, RtInt varying,
                                RtInt uniform, RtInt facevarying = -1) {
  RtToken tokens[1] = {RI_P};
  RtPointer parms[1] = {(RtPointer)p.data()};
  if (facevarying < 0) {
    return GMANParameterList(dictionary, 1, tokens, parms, vertex, varying, uniform);
  }
  return GMANParameterList(dictionary, 1, tokens, parms, vertex, varying, uniform, facevarying);
}

std::vector<RtFloat> flattenPoints(std::vector<GMANPoint> const& points) {
  std::vector<RtFloat> p(3 * points.size());
  for (std::size_t i = 0; i < points.size(); ++i) {
    p[3 * i] = points[i].getX();
    p[3 * i + 1] = points[i].getY();
    p[3 * i + 2] = points[i].getZ();
  }
  return p;
}

GMANPrimitive* buildPolygon(std::vector<GMANPoint> const& outer) {
  RtInt const nverts = (RtInt)outer.size();
  std::vector<RtFloat> const p = flattenPoints(outer);
  GMANDictionary dictionary;
  GMANParameterList pl = makePointList(dictionary, p, nverts, nverts, 1);
  GMANOptions options;
  GMANAttributes attr;
  GMANTransform transform;
  GMANRayObjectManager mgr;
  return mgr.getRSPolygon(nverts, pl, &options, &attr, &transform);
}

GMANPrimitive* buildGeneralPolygon(std::vector<std::vector<GMANPoint>> const& loops) {
  RtInt const nloops = (RtInt)loops.size();
  std::vector<RtInt> nverts(nloops);
  RtInt total = 0;
  std::vector<GMANPoint> flat;
  for (RtInt i = 0; i < nloops; ++i) {
    nverts[i] = (RtInt)loops[i].size();
    total += nverts[i];
    flat.insert(flat.end(), loops[i].begin(), loops[i].end());
  }
  std::vector<RtFloat> const p = flattenPoints(flat);
  GMANDictionary dictionary;
  GMANParameterList pl = makePointList(dictionary, p, total, total, 1);
  GMANOptions options;
  GMANAttributes attr;
  GMANTransform transform;
  GMANRayObjectManager mgr;
  return mgr.getRSGeneralPolygon(nloops, nverts.data(), pl, &options, &attr, &transform);
}

GMANPrimitive* buildPointsPolygon(std::vector<GMANPoint> const& points, std::vector<std::vector<RtInt>> const& faces) {
  RtInt const pointCount = (RtInt)points.size();
  std::vector<RtFloat> const p = flattenPoints(points);

  RtInt const npolys = (RtInt)faces.size();
  std::vector<RtInt> nverts(npolys);
  std::vector<RtInt> verts;
  for (RtInt i = 0; i < npolys; ++i) {
    nverts[i] = (RtInt)faces[i].size();
    verts.insert(verts.end(), faces[i].begin(), faces[i].end());
  }

  GMANDictionary dictionary;
  GMANParameterList pl = makePointList(dictionary, p, pointCount, pointCount, npolys, (RtInt)verts.size());
  GMANOptions options;
  GMANAttributes attr;
  GMANTransform transform;
  GMANRayObjectManager mgr;
  return mgr.getRSPointsPolygon(npolys, nverts.data(), verts.data(), pl, &options, &attr, &transform);
}

// One PointsGeneralPolygons request: faces holds, per face, its own loops
// (loop 0 the outer boundary, every later loop a hole), each loop a list
// of indices into points.
GMANPrimitive* buildPointsGeneralPolygons(std::vector<GMANPoint> const& points,
                                          std::vector<std::vector<std::vector<RtInt>>> const& faces) {
  RtInt const pointCount = (RtInt)points.size();
  std::vector<RtFloat> const p = flattenPoints(points);

  RtInt const npolys = (RtInt)faces.size();
  std::vector<RtInt> nloops(npolys);
  std::vector<RtInt> nverts;
  std::vector<RtInt> verts;
  for (RtInt i = 0; i < npolys; ++i) {
    nloops[i] = (RtInt)faces[i].size();
    for (std::vector<RtInt> const& loop : faces[i]) {
      nverts.push_back((RtInt)loop.size());
      verts.insert(verts.end(), loop.begin(), loop.end());
    }
  }

  GMANDictionary dictionary;
  GMANParameterList pl = makePointList(dictionary, p, pointCount, pointCount, npolys, (RtInt)verts.size());
  GMANOptions options;
  GMANAttributes attr;
  GMANTransform transform;
  GMANRayObjectManager mgr;
  return mgr.getRSPointsGeneralPolygons(npolys, nloops.data(), nverts.data(), verts.data(), pl, &options, &attr,
                                        &transform);
}

// ---------------------------------------------------------------------
// Helpers shared across fixtures.
// ---------------------------------------------------------------------

RtFloat sumElementAreas(GMANRadiosityMesh const& mesh) {
  RtFloat total = 0;
  for (std::size_t i = 0; i < mesh.getElementCount(); ++i) {
    total += mesh.getElement(i).area;
  }
  return total;
}

// The z == 0 plane's own shoelace area, independent of the mesh under
// test.
double shoelaceArea(std::vector<GMANPoint> const& loop) {
  double area = 0;
  std::size_t const n = loop.size();
  for (std::size_t i = 0; i < n; ++i) {
    GMANPoint const& a = loop[i];
    GMANPoint const& b = loop[(i + 1) % n];
    area += (double)a.getX() * (double)b.getY() - (double)b.getX() * (double)a.getY();
  }
  return std::fabs(area) * 0.5;
}

RtFloat pointDistance(GMANPoint const& a, GMANPoint const& b) {
  GMANVector const v(a, b);
  return (RtFloat)std::sqrt(v.dot(v));
}

// outerSquare/holeSquare: side 2 and side 1, centred on the origin in the
// z == 0 plane -- the same fixture tests/raypolygonhole_test.cpp casts
// rays through.
std::vector<GMANPoint> outerSquare() {
  return {GMANPoint(-1.0, -1.0, 0.0), GMANPoint(1.0, -1.0, 0.0), GMANPoint(1.0, 1.0, 0.0), GMANPoint(-1.0, 1.0, 0.0)};
}

std::vector<GMANPoint> holeSquare() {
  return {GMANPoint(-0.5, -0.5, 0.0), GMANPoint(0.5, -0.5, 0.0), GMANPoint(0.5, 0.5, 0.0), GMANPoint(-0.5, 0.5, 0.0)};
}

// ---------------------------------------------------------------------
// A. Holes.
// ---------------------------------------------------------------------

// A maxEdgeLength of 0.35 over the outer square's own extent (2) resolves
// to a 6x6 grid, step 1/3, cell boundaries at -1, -2/3, -1/3, 0, 1/3, 2/3,
// 1 -- the hole's own edges at +-0.5 fall at the midpoint of a straddling
// cell on each side, splitting every boundary cell across the hole rather
// than aligning with it.
void testHoledSquareArea() {
  GMANPrimitive* prim = buildGeneralPolygon({outerSquare(), holeSquare()});
  GMANLinearWorldManager worldManager;
  worldManager.add(prim);

  GMANRadiosityMesh mesh;
  RtFloat const maxEdgeLength = 0.35f;
  mesh.build(worldManager, maxEdgeLength);

  RtFloat const expected = 3.0f; // outer area 4 minus hole area 1
  RtFloat const diced = sumElementAreas(mesh);
  RtFloat const relError = std::fabs(diced - expected) / expected;
  std::printf("area: holed square elements=%zu expected=%.6f diced=%.6f relError=%.6g\n", mesh.getElementCount(),
              expected, diced, relError);
  check(relError < kAreaRelTolerance, "holed square: elements sum to outer minus hole area within 1e-5 relative");

  bool anyCentreInHole = false;
  for (std::size_t i = 0; i < mesh.getElementCount(); ++i) {
    GMANPoint const& c = mesh.getElement(i).centre;
    if (std::fabs(c.getX()) < 0.5f && std::fabs(c.getY()) < 0.5f) {
      anyCentreInHole = true;
    }
  }
  check(!anyCentreInHole, "holed square: no element's centre lies inside the hole");

  bool everyHoleNodeOutside = true;
  for (std::size_t i = 0; i < mesh.getNodeCount(); ++i) {
    GMANRadiosityNode const& node = mesh.getNode(i);
    // Strictly inside the hole's own bounds: the grid's own node
    // coordinates (a byproduct of the step above) land at 0, +-1/3, well
    // inside +-0.5 with no boundary ambiguity.
    if (std::fabs(node.position.getX()) < 0.5f && std::fabs(node.position.getY()) < 0.5f) {
      if (node.inside) {
        everyHoleNodeOutside = false;
      }
    }
  }
  check(everyHoleNodeOutside, "holed square: every node inside the hole is flagged outside");
}

// An L shape (a unit square notch cut from a 2x2 square): concave, so its
// own diced area proves clipping needs no convexity from the subject.
void testConcaveLShapeArea() {
  std::vector<GMANPoint> const lShape = {
      GMANPoint(0.0, 0.0, 0.0), GMANPoint(2.0, 0.0, 0.0), GMANPoint(2.0, 1.0, 0.0),
      GMANPoint(1.0, 1.0, 0.0), GMANPoint(1.0, 2.0, 0.0), GMANPoint(0.0, 2.0, 0.0),
  };
  GMANPrimitive* prim = buildPolygon(lShape);
  GMANLinearWorldManager worldManager;
  worldManager.add(prim);

  GMANRadiosityMesh mesh;
  mesh.build(worldManager, 0.5f);

  RtFloat const expected = (RtFloat)shoelaceArea(lShape);
  RtFloat const diced = sumElementAreas(mesh);
  RtFloat const relError = std::fabs(diced - expected) / expected;
  std::printf("area: concave L elements=%zu expected=%.6f diced=%.6f relError=%.6g\n", mesh.getElementCount(), expected,
              diced, relError);
  check(relError < kAreaRelTolerance, "concave L: elements sum to its own shoelace area within 1e-5 relative");
}

// ---------------------------------------------------------------------
// B. Meshes.
// ---------------------------------------------------------------------

// A unit cube (side 1, centred on the origin): six PointsPolygons faces,
// vertex order matching tests/polygonmeshbvh_test.cpp's own cubeFaces().
std::vector<GMANPoint> unitCubePoints() {
  RtFloat const h = 0.5f;
  return {
      GMANPoint(-h, -h, -h), GMANPoint(h, -h, -h), GMANPoint(h, h, -h), GMANPoint(-h, h, -h),
      GMANPoint(-h, -h, h),  GMANPoint(h, -h, h),  GMANPoint(h, h, h),  GMANPoint(-h, h, h),
  };
}

std::vector<std::vector<RtInt>> unitCubeFaces() {
  return {
      {0, 1, 2, 3}, // bottom, z == -h
      {4, 5, 6, 7}, // top, z == h
      {0, 3, 7, 4}, // left, x == -h
      {1, 2, 6, 5}, // right, x == h
      {0, 1, 5, 4}, // front, y == -h
      {3, 2, 6, 7}, // back, y == h
  };
}

void testPointsPolygonCubeArea() {
  GMANPrimitive* prim = buildPointsPolygon(unitCubePoints(), unitCubeFaces());
  GMANLinearWorldManager worldManager;
  worldManager.add(prim);

  GMANRadiosityMesh mesh;
  mesh.build(worldManager, 0.2f);

  RtFloat const expected = 6.0f; // unit cube: 6 faces, area 1 each
  RtFloat const diced = sumElementAreas(mesh);
  RtFloat const relError = std::fabs(diced - expected) / expected;
  std::printf("area: PointsPolygons cube elements=%zu expected=%.6f diced=%.6f relError=%.6g skipped=%zu\n",
              mesh.getElementCount(), expected, diced, relError, mesh.getSkippedCount());
  check(relError < kAreaRelTolerance, "PointsPolygons cube: elements sum to 6 within 1e-5 relative");
  check(mesh.getSkippedCount() == 0, "PointsPolygons cube: the mesh primitive is diced, never counted as skipped");
}

// One PointsGeneralPolygons face, outerSquare with holeSquare cut from it,
// through the shared point pool a mesh request gathers vertices from.
void testPointsGeneralPolygonsHoleArea() {
  std::vector<GMANPoint> points = outerSquare();
  std::vector<GMANPoint> const hole = holeSquare();
  points.insert(points.end(), hole.begin(), hole.end());

  std::vector<std::vector<std::vector<RtInt>>> const faces = {{{0, 1, 2, 3}, {4, 5, 6, 7}}};
  GMANPrimitive* prim = buildPointsGeneralPolygons(points, faces);
  GMANLinearWorldManager worldManager;
  worldManager.add(prim);

  GMANRadiosityMesh mesh;
  mesh.build(worldManager, 0.4f);

  RtFloat const expected = 3.0f; // outer area 4 minus hole area 1
  RtFloat const diced = sumElementAreas(mesh);
  RtFloat const relError = std::fabs(diced - expected) / expected;
  std::printf("area: PointsGeneralPolygons hole elements=%zu expected=%.6f diced=%.6f relError=%.6g\n",
              mesh.getElementCount(), expected, diced, relError);
  check(relError < kAreaRelTolerance, "PointsGeneralPolygons face with a hole: sums to outer minus hole");
}

// ---------------------------------------------------------------------
// C. Lookup through the BVH.
// ---------------------------------------------------------------------

// One axis-aligned ray toward one face of the unit cube from outside it,
// the same construction tests/polygonmeshbvh_test.cpp's own axisRay uses,
// scaled to this file's own half-extent.
GMANRay cubeAxisRay(int axis, RtFloat faceSign, RtFloat u, RtFloat v) {
  RtFloat coord[3];
  int const other0 = (axis + 1) % 3;
  int const other1 = (axis + 2) % 3;
  coord[axis] = faceSign * 2.0f;
  coord[other0] = u;
  coord[other1] = v;

  RtFloat dir[3] = {0, 0, 0};
  dir[axis] = -faceSign;
  return GMANRay(GMANPoint(coord[0], coord[1], coord[2]), GMANVector(dir[0], dir[1], dir[2]));
}

void testMeshLookupThroughBVH() {
  GMANPrimitive* prim = buildPointsPolygon(unitCubePoints(), unitCubeFaces());
  GMANLinearWorldManager worldManager;
  worldManager.add(prim);

  GMANRayBVH bvh;
  bvh.build(worldManager);
  GMANRadiosityMesh mesh;
  mesh.build(worldManager, 0.2f);

  struct FaceCase {
    int axis;
    RtFloat sign;
  };
  FaceCase const cases[] = {{2, -1.0f}, {2, 1.0f}, {0, -1.0f}, {0, 1.0f}, {1, -1.0f}, {1, 1.0f}};
  // Interior samples and the face's own edges, a hair inside the cube's
  // own +-0.5 half extent: GMANRayPolygon::intersect's even-odd test
  // excludes a ray landing exactly on its own high boundary, a
  // pre-existing property of that test, not of this dicer, so the edge
  // samples below sit in the dicing grid's own last cell without tripping
  // it.
  constexpr RtFloat kEdgeEpsilon = 1e-4f;
  RtFloat const samples[] = {-0.5f + kEdgeEpsilon, -0.3f, 0.0f, 0.3f, 0.5f - kEdgeEpsilon};

  bool everyHitLocates = true;
  bool everyRoundTripOk = true;
  std::size_t hitCount = 0;
  RtFloat maxPositionError = 0;

  for (FaceCase const& c : cases) {
    for (RtFloat u : samples) {
      for (RtFloat v : samples) {
        GMANRay const ray = cubeAxisRay(c.axis, c.sign, u, v);

        GMANHit hit;
        GMANRayInterface const* hitPrimitive = nullptr;
        bool const found = bvh.nearestHit(ray, hit, hitPrimitive);
        if (!found) {
          continue;
        }
        ++hitCount;

        GMANRadiosityLocation location;
        bool const located = mesh.locate(hit, location);
        if (!located) {
          everyHitLocates = false;
          continue;
        }

        GMANPoint blended(0, 0, 0);
        for (int k = 0; k < 4; ++k) {
          GMANPoint const& corner = mesh.getNode(location.corners[k]).position;
          blended += corner * location.weights[k];
        }
        RtFloat const error = pointDistance(blended, hit.point);
        maxPositionError = GMANMax(maxPositionError, error);
        if (error >= kRoundTripTolerance) {
          everyRoundTripOk = false;
        }
      }
    }
  }

  std::printf("lookup: cube hits=%zu maxPositionError=%.6g\n", hitCount, maxPositionError);
  check(hitCount == (std::size_t)(6 * 5 * 5), "cube: every interior and edge ray hits a face");
  check(everyHitLocates, "cube: locate() finds an element for every hit the BVH reports");
  check(everyRoundTripOk, "cube: the blended corner positions reproduce hit.point within tolerance");
}

} // namespace

int main() {
  testHoledSquareArea();
  testConcaveLShapeArea();

  testPointsPolygonCubeArea();
  testPointsGeneralPolygonsHoleArea();

  testMeshLookupThroughBVH();

  return checkSummary("GMANRadiosityMesh dices polygon holes and polygon mesh faces");
}
