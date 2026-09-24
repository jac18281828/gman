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
 * GMANRayObjectManager::getRSPointsPolygon returns one internal mesh
 * primitive; GMANRayBVH::build flattens it into one entry per face rather
 * than one entry for the whole mesh. A ray aimed at a face never reports
 * the mesh as hitPrimitive -- it is a GMANRayPolygon, and it equals the
 * hit's own primitive -- and a fan of rays across
 * tests/rib/pointspolygons_cube.rib's own cube geometry agrees, face by
 * face, between the BVH's nearestHit, the mesh's own linear intersect,
 * and each face's analytic plane.
 */

#include <cmath>
#include <vector>

#include "check.h"
#include "gmanattributes.h"
#include "gmandictionary.h"
#include "gmanlinearworldmanager.h"
#include "gmanoptions.h"
#include "gmanparameterlist.h"
#include "gmanpolygon.h"
#include "gmanray.h"
#include "gmanraybvh.h"
#include "gmanrayobjectmanager.h"
#include "gmanraypolygon.h"
#include "gmantransform.h"

namespace {

constexpr RtFloat kTolerance = 1e-3f;

bool near(RtFloat a, RtFloat b, RtFloat tol = kTolerance) { return std::fabs(a - b) <= tol; }

// tests/rib/pointspolygons_cube.rib's own 8 points and 6 quad faces, read
// directly here rather than through a RIB parse -- an axis-aligned unit
// cube, each face's own outward vertex order matching that fixture.
std::vector<GMANPoint> cubePoints() {
  return {
      GMANPoint(-1, -1, -1), GMANPoint(1, -1, -1), GMANPoint(1, 1, -1), GMANPoint(-1, 1, -1),
      GMANPoint(-1, -1, 1),  GMANPoint(1, -1, 1),  GMANPoint(1, 1, 1),  GMANPoint(-1, 1, 1),
  };
}

std::vector<std::vector<RtInt>> cubeFaces() {
  return {
      {0, 1, 2, 3}, // bottom, z == -1
      {4, 5, 6, 7}, // top, z == 1
      {0, 3, 7, 4}, // left, x == -1
      {1, 2, 6, 5}, // right, x == 1
      {0, 1, 5, 4}, // front, y == -1
      {3, 2, 6, 7}, // back, y == 1
  };
}

GMANPrimitive* runGetRSPointsPolygonDirect(std::vector<GMANPoint> const& points,
                                           std::vector<std::vector<RtInt>> const& faces) {
  RtInt const pointCount = (RtInt)points.size();
  std::vector<RtFloat> p(3 * pointCount);
  for (RtInt i = 0; i < pointCount; ++i) {
    p[3 * i] = points[i].getX();
    p[3 * i + 1] = points[i].getY();
    p[3 * i + 2] = points[i].getZ();
  }

  RtInt const npolys = (RtInt)faces.size();
  std::vector<RtInt> nverts(npolys);
  std::vector<RtInt> verts;
  for (RtInt i = 0; i < npolys; ++i) {
    nverts[i] = (RtInt)faces[i].size();
    verts.insert(verts.end(), faces[i].begin(), faces[i].end());
  }

  GMANDictionary dictionary;
  RtToken tokens[1] = {RI_P};
  RtPointer parms[1] = {p.data()};
  GMANParameterList pl(dictionary, 1, tokens, parms, /*vertex=*/pointCount, /*varying=*/pointCount,
                       /*uniform=*/npolys, /*facevarying=*/(RtInt)verts.size());
  GMANOptions options;
  GMANAttributes attr;
  GMANTransform transform;
  GMANRayObjectManager mgr;
  return mgr.getRSPointsPolygon(npolys, nverts.data(), verts.data(), pl, &options, &attr, &transform);
}

// A face's own analytic ring, independent of GMANRayPolygonMesh's own
// vertex gathering: faces()[k] indexed straight into points(), not
// through the mesh under test.
std::vector<GMANPoint> faceRing(std::vector<GMANPoint> const& points, std::vector<RtInt> const& face) {
  std::vector<GMANPoint> ring(face.size());
  for (std::size_t i = 0; i < face.size(); ++i) {
    ring[i] = points[(std::size_t)face[i]];
  }
  return ring;
}

bool sameDirection(GMANVector const& a, GMANVector const& b) {
  return near(a.getX(), b.getX()) && near(a.getY(), b.getY()) && near(a.getZ(), b.getZ());
}

// One axis-aligned ray, travelling toward one face of the unit cube from
// outside it: originAxis is 5 units out along axis, on the same side as
// the face itself (faceSign), so direction (toward the face) is
// -faceSign. u/v are the ray's own fixed coordinates on the other two
// axes, offset from the face's own centre for the "fan" -- always well
// inside [-1, 1], never near an edge.
GMANRay axisRay(int axis, RtFloat faceSign, RtFloat u, RtFloat v) {
  RtFloat coord[3];
  int const other0 = (axis + 1) % 3;
  int const other1 = (axis + 2) % 3;
  coord[axis] = faceSign * 5.0f;
  coord[other0] = u;
  coord[other1] = v;

  RtFloat dir[3] = {0, 0, 0};
  dir[axis] = -faceSign;

  return GMANRay(GMANPoint(coord[0], coord[1], coord[2]), GMANVector(dir[0], dir[1], dir[2]));
}

// The point axisRay(axis, faceSign, u, v) analytically hits: its own
// fixed coordinates on the perpendicular axes, faceSign on axis.
GMANPoint axisHitPoint(int axis, RtFloat faceSign, RtFloat u, RtFloat v) {
  RtFloat coord[3];
  int const other0 = (axis + 1) % 3;
  int const other1 = (axis + 2) % 3;
  coord[axis] = faceSign;
  coord[other0] = u;
  coord[other1] = v;
  return GMANPoint(coord[0], coord[1], coord[2]);
}

struct FaceCase {
  int axis;
  RtFloat sign;
  std::size_t faceIndex;
};

// axis/sign identify each face's own analytic plane; faceIndex indexes
// cubeFaces() for that same face's own vertex ring.
std::vector<FaceCase> faceCases() {
  return {
      {2, -1.0f, 0}, // bottom
      {2, 1.0f, 1},  // top
      {0, -1.0f, 2}, // left
      {0, 1.0f, 3},  // right
      {1, -1.0f, 4}, // front
      {1, 1.0f, 5},  // back
  };
}

// ---- check: a ray aimed at one face never reports the mesh as
// hitPrimitive, and its t/normal match that face's own analytic plane ----
void testFaceNotMesh() {
  std::vector<GMANPoint> const points = cubePoints();
  std::vector<std::vector<RtInt>> const faces = cubeFaces();

  GMANPrimitive* meshPrim = runGetRSPointsPolygonDirect(points, faces);
  GMANRayInterface* mesh = dynamic_cast<GMANRayInterface*>(meshPrim);
  check(mesh != nullptr, "face k: getRSPointsPolygon returns a GMANRayInterface");

  GMANLinearWorldManager worldManager;
  worldManager.add(meshPrim);

  GMANRayBVH bvh;
  bvh.build(worldManager);

  // The top face, not the bottom: its own analytic normal points opposite
  // this ray's own direction, so a mutation reporting the ray's direction
  // as the hit normal is distinguishable here.
  GMANRay const ray = axisRay(2, 1.0f, 0.0f, 0.0f); // straight at the top face's own centre
  GMANHit hit;
  GMANRayInterface const* hitPrimitive = nullptr;
  bool const found = bvh.nearestHit(ray, hit, hitPrimitive);
  check(found, "face k: the BVH reports a hit");
  if (!found) {
    return;
  }

  GMANRayPolygon const* facePolygon = dynamic_cast<GMANRayPolygon const*>(hitPrimitive);
  check(facePolygon != nullptr, "face k: hitPrimitive is a GMANRayPolygon, never the mesh");
  check(hitPrimitive == hit.primitive, "face k: hitPrimitive equals hit.primitive");

  GMANVector expectedNormal = gman::newellNormal(faceRing(points, faces[1]));
  expectedNormal /= expectedNormal.magnitude();
  check(near(hit.t, 4.0), "face k: t == 4, the analytic distance from the ray's own origin");
  check(sameDirection(hit.normal, expectedNormal), "face k: the hit normal matches the face's own analytic plane");

  // The mesh's own intersect, called directly rather than through the
  // BVH, must agree on the same face's own analytic normal.
  GMANHit meshHit;
  bool const meshFound = mesh != nullptr && mesh->intersect(ray, meshHit);
  check(meshFound, "face k: the mesh's own intersect hits too");
  if (meshFound) {
    check(sameDirection(meshHit.normal, expectedNormal),
          "face k: the mesh's own intersect normal matches the face's own analytic plane");
  }
}

// ---- check: a fan of rays across the cube agrees, face by face, between
// the BVH, the mesh's own linear intersect, and the analytic nearest
// face ----
void testFanAgreesWithMeshAndAnalytic() {
  std::vector<GMANPoint> const points = cubePoints();
  std::vector<std::vector<RtInt>> const faces = cubeFaces();
  std::vector<FaceCase> const cases = faceCases();

  GMANPrimitive* meshPrim = runGetRSPointsPolygonDirect(points, faces);
  GMANRayInterface* mesh = dynamic_cast<GMANRayInterface*>(meshPrim);
  check(mesh != nullptr, "fan: getRSPointsPolygon returns a GMANRayInterface");

  GMANLinearWorldManager worldManager;
  GMANPrimitive* bvhMeshPrim = runGetRSPointsPolygonDirect(points, faces);
  worldManager.add(bvhMeshPrim);
  GMANRayBVH bvh;
  bvh.build(worldManager);

  RtFloat const offsets[3] = {-0.6f, 0.0f, 0.6f};

  for (FaceCase const& c : cases) {
    GMANVector expectedNormal = gman::newellNormal(faceRing(points, faces[c.faceIndex]));
    expectedNormal /= expectedNormal.magnitude();

    for (RtFloat u : offsets) {
      for (RtFloat v : offsets) {
        GMANRay const ray = axisRay(c.axis, c.sign, u, v);
        GMANPoint const expectedPoint = axisHitPoint(c.axis, c.sign, u, v);

        GMANHit bvhHit;
        GMANRayInterface const* bvhPrim = nullptr;
        bool const bvhFound = bvh.nearestHit(ray, bvhHit, bvhPrim);

        GMANHit meshHit;
        bool const meshFound = mesh != nullptr && mesh->intersect(ray, meshHit);

        check(bvhFound && meshFound, "fan: both the BVH and the mesh's own intersect hit");
        if (!bvhFound || !meshFound) {
          continue;
        }

        check(near(bvhHit.t, meshHit.t), "fan: the BVH's t matches the mesh's own intersect");
        check(near(bvhHit.t, 4.0), "fan: t == 4, the analytic distance to the near face");
        check(near(bvhHit.point.getX(), expectedPoint.getX()) && near(bvhHit.point.getY(), expectedPoint.getY()) &&
                  near(bvhHit.point.getZ(), expectedPoint.getZ()),
              "fan: the hit point matches the analytic nearest face");
        check(sameDirection(bvhHit.normal, expectedNormal), "fan: the hit normal matches the analytic nearest face");
      }
    }
  }

  delete meshPrim; // never added to a world manager, so never freed by one
}

} // namespace

int main() {
  testFaceNotMesh();
  testFanAgreesWithMeshAndAnalytic();

  return checkSummary("PointsPolygons faces flatten into the BVH, one entry per face");
}
