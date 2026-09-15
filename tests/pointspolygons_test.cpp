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
 * PointsPolygons and PointsGeneralPolygons: every face is the equivalent
 * Polygon/GeneralPolygon, gathered through "verts" out of a shared "P"
 * instead of carrying its own. This suite proves that equivalence three
 * ways: direct object-manager calls (mirroring
 * tests/generalpolygon_test.cpp's own runGetRSGeneralPolygon), twin-RIB
 * renders compared pixel by pixel (tests/goldenimage.h), and malformed
 * request fixtures (tests/paramclamp_test.cpp's own Fixture shape).
 *
 * countFaces here walks every body on the object's own chain --
 * generalpolygon_test.cpp's own countFaces reads only the first, since a
 * GeneralPolygon never produces more than one.
 */

#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

#include "check.h"
#include "checkertexture.h"
#include "goldenimage.h"
#include "gmanattributes.h"
#include "gmandictionary.h"
#include "gmanobject.h"
#include "gmanoptions.h"
#include "gmanparameterlist.h"
#include "gmanpatchpolyobjectmanager.h"
#include "gmanprimitives.h"
#include "gmantransform.h"

namespace {

// ---- geometry oracle, independent of the implementation under test ----

std::array<double, 3> newellNormal(const std::vector<GMANPoint> &ring) {
  double nx = 0.0, ny = 0.0, nz = 0.0;
  const std::size_t n = ring.size();
  for (std::size_t i = 0; i < n; ++i) {
    const GMANPoint &cur = ring[i];
    const GMANPoint &next = ring[(i + 1) % n];
    double cx = cur.getX(), cy = cur.getY(), cz = cur.getZ();
    double nxp = next.getX(), nyp = next.getY(), nzp = next.getZ();
    nx += (cy - nyp) * (cz + nzp);
    ny += (cz - nzp) * (cx + nxp);
    nz += (cx - nxp) * (cy + nyp);
  }
  return {nx, ny, nz};
}

double ringArea(const std::vector<GMANPoint> &ring) {
  std::array<double, 3> n = newellNormal(ring);
  return 0.5 * std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
}

double triangleArea(const GMANPoint &a, const GMANPoint &b,
                     const GMANPoint &c) {
  double e1x = b.getX() - a.getX(), e1y = b.getY() - a.getY(),
         e1z = b.getZ() - a.getZ();
  double e2x = c.getX() - a.getX(), e2y = c.getY() - a.getY(),
         e2z = c.getZ() - a.getZ();
  double cx = e1y * e2z - e1z * e2y;
  double cy = e1z * e2x - e1x * e2z;
  double cz = e1x * e2y - e1y * e2x;
  return 0.5 * std::sqrt(cx * cx + cy * cy + cz * cz);
}

// The concave rings tests/generalpolygon_test.cpp already defines, at unit
// scale, z=0 -- reused here rather than shared through a header, since no
// such header exists yet and adding one is outside this task's scope.
std::vector<GMANPoint> concaveL() {
  return {{1, -1, 0}, {1, 0, 0}, {0, 0, 0}, {0, 1, 0}, {-1, 1, 0}, {-1, -1, 0}};
}

std::vector<GMANPoint> fourPointedStar() {
  return {{1.0f, 0.0f, 0},  {0.2475f, 0.2475f, 0},
          {0.0f, 1.0f, 0},  {-0.2475f, 0.2475f, 0},
          {-1.0f, 0.0f, 0}, {-0.2475f, -0.2475f, 0},
          {0.0f, -1.0f, 0}, {0.2475f, -0.2475f, 0}};
}

// ---- object-manager call helpers ----

GMANPrimitive *runGetRSPolygonDirect(const std::vector<GMANPoint> &ring) {
  const RtInt nverts = (RtInt) ring.size();
  std::vector<RtFloat> p(3 * nverts);
  for (RtInt i = 0; i < nverts; ++i) {
    p[3 * i] = ring[i].getX();
    p[3 * i + 1] = ring[i].getY();
    p[3 * i + 2] = ring[i].getZ();
  }
  GMANDictionary dictionary;
  RtToken tokens[1] = {RI_P};
  RtPointer parms[1] = {p.data()};
  GMANParameterList pl(dictionary, 1, tokens, parms, nverts, nverts, 1);
  GMANOptions options;
  GMANAttributes attr;
  GMANTransform transform;
  GMANPatchPolyObjectManager mgr;
  return mgr.getRSPolygon(nverts, pl, &options, &attr, &transform);
}

GMANPrimitive *runGetRSGeneralPolygonDirect(
    const std::vector<std::vector<GMANPoint>> &loops) {
  const RtInt nloops = (RtInt) loops.size();
  std::vector<RtInt> nverts(nloops);
  RtInt total = 0;
  for (RtInt i = 0; i < nloops; ++i) {
    nverts[i] = (RtInt) loops[i].size();
    total += nverts[i];
  }
  std::vector<RtFloat> p(3 * total);
  RtInt k = 0;
  for (RtInt i = 0; i < nloops; ++i) {
    for (const GMANPoint &pt : loops[i]) {
      p[3 * k] = pt.getX();
      p[3 * k + 1] = pt.getY();
      p[3 * k + 2] = pt.getZ();
      ++k;
    }
  }
  GMANDictionary dictionary;
  RtToken tokens[1] = {RI_P};
  RtPointer parms[1] = {p.data()};
  GMANParameterList pl(dictionary, 1, tokens, parms, total, total, 1, total);
  GMANOptions options;
  GMANAttributes attr;
  GMANTransform transform;
  GMANPatchPolyObjectManager mgr;
  return mgr.getRSGeneralPolygon(nloops, nverts.data(), pl, &options, &attr,
                                 &transform);
}

// points is the shared "P" -- point i lands at p[3*i..3*i+2] -- and verts
// indexes into it exactly as RiPointsPolygonsV's own "verts" does.
GMANPrimitive *runGetRSPointsPolygon(RtInt npolys, std::vector<RtInt> nverts,
                                      std::vector<RtInt> verts,
                                      const std::vector<GMANPoint> &points) {
  const RtInt pointCount = (RtInt) points.size();
  std::vector<RtFloat> p(3 * pointCount);
  for (RtInt i = 0; i < pointCount; ++i) {
    p[3 * i] = points[i].getX();
    p[3 * i + 1] = points[i].getY();
    p[3 * i + 2] = points[i].getZ();
  }
  GMANDictionary dictionary;
  RtToken tokens[1] = {RI_P};
  RtPointer parms[1] = {p.data()};
  GMANParameterList pl(dictionary, 1, tokens, parms, /*vertex=*/pointCount,
                       /*varying=*/pointCount, /*uniform=*/npolys,
                       /*facevarying=*/(RtInt) verts.size());
  GMANOptions options;
  GMANAttributes attr;
  GMANTransform transform;
  GMANPatchPolyObjectManager mgr;
  return mgr.getRSPointsPolygon(npolys, nverts.data(), verts.data(), pl,
                                &options, &attr, &transform);
}

GMANPrimitive *runGetRSPointsGeneralPolygons(
    RtInt npolys, std::vector<RtInt> nloops, std::vector<RtInt> nverts,
    std::vector<RtInt> verts, const std::vector<GMANPoint> &points) {
  const RtInt pointCount = (RtInt) points.size();
  std::vector<RtFloat> p(3 * pointCount);
  for (RtInt i = 0; i < pointCount; ++i) {
    p[3 * i] = points[i].getX();
    p[3 * i + 1] = points[i].getY();
    p[3 * i + 2] = points[i].getZ();
  }
  GMANDictionary dictionary;
  RtToken tokens[1] = {RI_P};
  RtPointer parms[1] = {p.data()};
  GMANParameterList pl(dictionary, 1, tokens, parms, pointCount, pointCount,
                       npolys, (RtInt) verts.size());
  GMANOptions options;
  GMANAttributes attr;
  GMANTransform transform;
  GMANPatchPolyObjectManager mgr;
  return mgr.getRSPointsGeneralPolygons(npolys, nloops.data(), nverts.data(),
                                        verts.data(), pl, &options, &attr,
                                        &transform);
}

// ---- object-chain readers ----

int countBodies(GMANObject *object) {
  int count = 0;
  for (GMANBody *body = object->getBody(); body != nullptr;
       body = body->getNext()) {
    ++count;
  }
  return count;
}

// Walks every body on the chain, unlike generalpolygon_test.cpp's own
// countFaces (which reads only the first): a GeneralPolygon never produces
// more than one body, but a PointsPolygons/PointsGeneralPolygons mesh's
// faces each own their own (settled decision "One primitive, many
// bodies").
int countFaces(GMANObject *object) {
  int count = 0;
  for (GMANBody *body = object->getBody(); body != nullptr;
       body = body->getNext()) {
    GMANSurface *surface = body->getSurface();
    for (GMANFace *face = surface ? surface->getFace() : nullptr;
         face != nullptr; face = face->getNext()) {
      ++count;
    }
  }
  return count;
}

std::vector<GMANVertex *> vertexChain(GMANObject *object) {
  std::vector<GMANVertex *> chain;
  for (GMANVertex *v = object->getVert(); v != nullptr; v = v->getNext()) {
    chain.push_back(v);
  }
  return chain;
}

bool sameLocationOrder(const std::vector<GMANVertex *> &a,
                       const std::vector<GMANVertex *> &b) {
  if (a.size() != b.size()) {
    return false;
  }
  for (std::size_t i = 0; i < a.size(); ++i) {
    const GMANPoint &pa = a[i]->getLocation();
    const GMANPoint &pb = b[i]->getLocation();
    if (pa.getX() != pb.getX() || pa.getY() != pb.getY() ||
        pa.getZ() != pb.getZ()) {
      return false;
    }
  }
  return true;
}

std::vector<std::array<int, 3>> triangleTriples(
    GMANObject *object, const std::map<const GMANVertex *, int> &index) {
  std::vector<std::array<int, 3>> triples;
  for (GMANBody *body = object->getBody(); body != nullptr;
       body = body->getNext()) {
    GMANSurface *surface = body->getSurface();
    for (GMANFace *face = surface ? surface->getFace() : nullptr;
         face != nullptr; face = face->getNext()) {
      auto it0 = index.find(face->getVertex(0));
      auto it1 = index.find(face->getVertex(1));
      auto it2 = index.find(face->getVertex(2));
      if (it0 == index.end() || it1 == index.end() || it2 == index.end()) {
        continue;
      }
      triples.push_back({it0->second, it1->second, it2->second});
    }
  }
  return triples;
}

std::map<const GMANVertex *, int> indexVertices(
    const std::vector<GMANVertex *> &chain) {
  std::map<const GMANVertex *, int> index;
  for (std::size_t i = 0; i < chain.size(); ++i) {
    index[chain[i]] = (int) i;
  }
  return index;
}

// One body's own summed triangle area and every face normal, checked
// against an independent oracle -- the ring's own Newell normal and half
// its magnitude.
void checkFaceGeometry(const std::string &label, GMANBody *body,
                       double expectedArea,
                       const std::array<double, 3> &expectedNormal) {
  const double expectedMag =
      std::sqrt(expectedNormal[0] * expectedNormal[0] +
               expectedNormal[1] * expectedNormal[1] +
               expectedNormal[2] * expectedNormal[2]);
  GMANSurface *surface = body->getSurface();
  double summedArea = 0.0;
  bool normalOk = true;
  for (GMANFace *face = surface ? surface->getFace() : nullptr;
       face != nullptr; face = face->getNext()) {
    const GMANPoint &v0 = face->getVertex(0)->getLocation();
    const GMANPoint &v1 = face->getVertex(1)->getLocation();
    const GMANPoint &v2 = face->getVertex(2)->getLocation();
    summedArea += triangleArea(v0, v1, v2);

    const GMANVector &n = face->getNormal();
    double dot = n.getX() * expectedNormal[0] + n.getY() * expectedNormal[1] +
                n.getZ() * expectedNormal[2];
    double nMag =
        std::sqrt(n.getX() * n.getX() + n.getY() * n.getY() + n.getZ() * n.getZ());
    if (nMag > 0.0 && expectedMag > 0.0 &&
        dot / (nMag * expectedMag) < 0.999) {
      normalOk = false;
    }
  }
  check(std::fabs(summedArea - expectedArea) <=
            1e-6 * std::max(1.0, expectedArea),
        label + ": area matches its own face (got " +
            std::to_string(summedArea) + ", expected " +
            std::to_string(expectedArea) + ")");
  check(normalOk, label + ": every triangle's normal matches its own face");
}

// ---- Unit (commit 2) ----

// getRSPointsPolygon with one face and verts = 0..n-1 produces the same
// vertex positions, triangles and area as getRSPolygon on the same "P".
void testOneFaceIdentity() {
  const std::vector<std::pair<std::string, std::vector<GMANPoint>>> cases = {
      {"concave L", concaveL()}, {"4-pointed star", fourPointedStar()}};

  for (const auto &namedRing : cases) {
    const std::string &name = namedRing.first;
    const std::vector<GMANPoint> &ring = namedRing.second;
    const RtInt n = (RtInt) ring.size();
    std::vector<RtInt> verts(n);
    for (RtInt i = 0; i < n; ++i) {
      verts[i] = i;
    }

    GMANPrimitive *polyPrim = runGetRSPolygonDirect(ring);
    GMANPrimitive *pointsPrim =
        runGetRSPointsPolygon(1, {n}, verts, ring);
    GMANObject *polyObj = dynamic_cast<GMANObject *>(polyPrim);
    GMANObject *pointsObj = dynamic_cast<GMANObject *>(pointsPrim);
    check(polyObj != nullptr && pointsObj != nullptr,
          name + ": getRSPolygon and getRSPointsPolygon both return an "
                 "object");
    if (polyObj == nullptr || pointsObj == nullptr) {
      delete polyPrim;
      delete pointsPrim;
      continue;
    }

    std::vector<GMANVertex *> polyVerts = vertexChain(polyObj);
    std::vector<GMANVertex *> pointsVerts = vertexChain(pointsObj);
    check(sameLocationOrder(polyVerts, pointsVerts),
          name + ": same vertex positions, same order");

    std::map<const GMANVertex *, int> polyIndex = indexVertices(polyVerts);
    std::map<const GMANVertex *, int> pointsIndex = indexVertices(pointsVerts);
    std::vector<std::array<int, 3>> polyTriples =
        triangleTriples(polyObj, polyIndex);
    std::vector<std::array<int, 3>> pointsTriples =
        triangleTriples(pointsObj, pointsIndex);
    check(polyTriples == pointsTriples,
          name + ": same triangle index triples");

    double polyArea = 0.0, pointsArea = 0.0;
    for (const auto &t : polyTriples) {
      polyArea += triangleArea(polyVerts[t[0]]->getLocation(),
                               polyVerts[t[1]]->getLocation(),
                               polyVerts[t[2]]->getLocation());
    }
    for (const auto &t : pointsTriples) {
      pointsArea += triangleArea(pointsVerts[t[0]]->getLocation(),
                                 pointsVerts[t[1]]->getLocation(),
                                 pointsVerts[t[2]]->getLocation());
    }
    check(std::fabs(polyArea - pointsArea) <= 1e-9 * std::max(1.0, polyArea),
          name + ": same area");

    delete polyPrim;
    delete pointsPrim;
  }
}

// Two faces sharing an edge -- six points, faces [0 1 4 3] and [1 2 5 4]
// -- with "P" supplied in a scrambled order and "verts" adjusted to
// match: two bodies, each with its own four vertices, each body's area
// and normal equal to its own face's.
void testIndicesNotOrder() {
  const std::vector<GMANPoint> canonical = {{0, 0, 0}, {1, 0, 0}, {2, 0, 0},
                                            {0, 1, 0}, {1, 1, 0}, {2, 1, 0}};
  const int order[6] = {3, 1, 5, 0, 4, 2};  // scrambled[i] = canonical[order[i]]
  std::vector<GMANPoint> scrambled(6);
  int newIndexOf[6];
  for (int i = 0; i < 6; ++i) {
    scrambled[i] = canonical[order[i]];
    newIndexOf[order[i]] = i;
  }

  std::vector<RtInt> verts = {
      newIndexOf[0], newIndexOf[1], newIndexOf[4], newIndexOf[3],
      newIndexOf[1], newIndexOf[2], newIndexOf[5], newIndexOf[4]};

  GMANPrimitive *prim =
      runGetRSPointsPolygon(2, {4, 4}, verts, scrambled);
  GMANObject *object = dynamic_cast<GMANObject *>(prim);
  check(object != nullptr, "indices, not order: returns an object");
  if (object == nullptr) {
    delete prim;
    return;
  }

  check(countBodies(object) == 2, "indices, not order: two bodies");
  check(countFaces(object) == 4,
        "indices, not order: four triangles total (two per quad)");
  std::vector<GMANVertex *> chain = vertexChain(object);
  check(chain.size() == 8,
        "indices, not order: eight vertices total (4+4, none shared)");

  const std::vector<GMANPoint> faceARing = {canonical[0], canonical[1],
                                            canonical[4], canonical[3]};
  const std::vector<GMANPoint> faceBRing = {canonical[1], canonical[2],
                                            canonical[5], canonical[4]};

  GMANBody *bodyA = object->getBody();
  GMANBody *bodyB = bodyA != nullptr ? bodyA->getNext() : nullptr;
  check(bodyA != nullptr && bodyB != nullptr,
        "indices, not order: both bodies present");
  if (bodyA != nullptr) {
    checkFaceGeometry("indices, not order: face A", bodyA,
                      ringArea(faceARing), newellNormal(faceARing));
  }
  if (bodyB != nullptr) {
    checkFaceGeometry("indices, not order: face B", bodyB,
                      ringArea(faceBRing), newellNormal(faceBRing));
  }

  delete prim;
}

// getRSPointsGeneralPolygons with one face of two loops matches
// getRSGeneralPolygon on the gathered loops.
void testHoleThroughIndices() {
  const std::vector<GMANPoint> outer = {
      {0, 0, 0}, {4, 0, 0}, {4, 4, 0}, {0, 4, 0}};
  const std::vector<GMANPoint> hole = {
      {1, 1, 0}, {3, 1, 0}, {3, 3, 0}, {1, 3, 0}};

  std::vector<GMANPoint> points = outer;
  points.insert(points.end(), hole.begin(), hole.end());

  GMANPrimitive *pointsPrim = runGetRSPointsGeneralPolygons(
      1, {2}, {4, 4}, {0, 1, 2, 3, 4, 5, 6, 7}, points);
  GMANPrimitive *generalPrim = runGetRSGeneralPolygonDirect({outer, hole});

  GMANObject *pointsObj = dynamic_cast<GMANObject *>(pointsPrim);
  GMANObject *generalObj = dynamic_cast<GMANObject *>(generalPrim);
  check(pointsObj != nullptr && generalObj != nullptr,
        "hole through indices: both return an object");
  if (pointsObj == nullptr || generalObj == nullptr) {
    delete pointsPrim;
    delete generalPrim;
    return;
  }

  std::vector<GMANVertex *> pointsVerts = vertexChain(pointsObj);
  std::vector<GMANVertex *> generalVerts = vertexChain(generalObj);
  check(sameLocationOrder(pointsVerts, generalVerts),
        "hole through indices: same vertex positions, same order");

  std::map<const GMANVertex *, int> pointsIndex = indexVertices(pointsVerts);
  std::map<const GMANVertex *, int> generalIndex = indexVertices(generalVerts);
  check(triangleTriples(pointsObj, pointsIndex) ==
            triangleTriples(generalObj, generalIndex),
        "hole through indices: same triangle index triples as "
        "getRSGeneralPolygon");

  delete pointsPrim;
  delete generalPrim;
}

// Three faces, the middle one collinear: two bodies survive with the
// other two faces' own area. All three degenerate: create().
void testOneBadFace() {
  const std::vector<GMANPoint> points = {
      {0, 0, 0}, {2, 0, 0}, {1, 2, 0},   // face 0: a valid triangle
      {3, 0, 0}, {4, 0, 0}, {5, 0, 0},   // face 1: collinear -- degenerate
      {0, 5, 0}, {2, 5, 0}, {1, 7, 0},   // face 2: a valid triangle
  };
  const std::vector<RtInt> nverts = {3, 3, 3};
  const std::vector<RtInt> verts = {0, 1, 2, 3, 4, 5, 6, 7, 8};

  GMANPrimitive *prim = runGetRSPointsPolygon(3, nverts, verts, points);
  GMANObject *object = dynamic_cast<GMANObject *>(prim);
  check(object != nullptr, "one bad face: returns an object");
  if (object != nullptr) {
    check(countBodies(object) == 2,
          "one bad face: the collinear middle face is skipped, the other "
          "two survive");
    check(countFaces(object) == 2,
          "one bad face: one triangle per surviving face");
    const std::vector<GMANPoint> face0Ring = {points[0], points[1],
                                              points[2]};
    const std::vector<GMANPoint> face2Ring = {points[6], points[7],
                                              points[8]};
    GMANBody *body0 = object->getBody();
    GMANBody *body2 = body0 != nullptr ? body0->getNext() : nullptr;
    if (body0 != nullptr) {
      checkFaceGeometry("one bad face: face 0", body0, ringArea(face0Ring),
                        newellNormal(face0Ring));
    }
    if (body2 != nullptr) {
      checkFaceGeometry("one bad face: face 2", body2, ringArea(face2Ring),
                        newellNormal(face2Ring));
    }
  }
  delete prim;

  // Every face degenerate: the whole mesh is the empty stub -- a mesh with
  // no surviving face, not a partially drawn one.
  const std::vector<GMANPoint> allDegenerate = {
      {0, 0, 0}, {1, 0, 0}, {2, 0, 0}, {3, 0, 0}, {4, 0, 0},
      {5, 0, 0}, {6, 0, 0}, {7, 0, 0}, {8, 0, 0},
  };
  GMANPrimitive *degeneratePrim =
      runGetRSPointsPolygon(3, nverts, verts, allDegenerate);
  GMANObject *degenerateObject = dynamic_cast<GMANObject *>(degeneratePrim);
  check(degenerateObject != nullptr,
        "one bad face, all degenerate: returns an object");
  check(degenerateObject != nullptr && degenerateObject->getBody() == nullptr,
        "one bad face, all degenerate: the returned object is the empty "
        "stub");
  delete degeneratePrim;
}

// Unreferenced points in "P" change nothing.
void testUnreferencedPoints() {
  const std::vector<GMANPoint> withoutExtra = {
      {0, 0, 0}, {2, 0, 0}, {1, 2, 0}};
  std::vector<GMANPoint> withExtra = withoutExtra;
  withExtra.push_back({99, 99, 99});
  withExtra.push_back({-50, -50, -50});
  withExtra.push_back({0, 0, -1});

  const std::vector<RtInt> nverts = {3};
  const std::vector<RtInt> verts = {0, 1, 2};

  GMANPrimitive *basePrim =
      runGetRSPointsPolygon(1, nverts, verts, withoutExtra);
  GMANPrimitive *extraPrim =
      runGetRSPointsPolygon(1, nverts, verts, withExtra);

  GMANObject *baseObj = dynamic_cast<GMANObject *>(basePrim);
  GMANObject *extraObj = dynamic_cast<GMANObject *>(extraPrim);
  check(baseObj != nullptr && extraObj != nullptr,
        "unreferenced points: both return an object");
  if (baseObj != nullptr && extraObj != nullptr) {
    check(sameLocationOrder(vertexChain(baseObj), vertexChain(extraObj)),
          "unreferenced points: unreferenced \"P\" entries change nothing");
  }
  delete basePrim;
  delete extraPrim;
}

// ---- Rendering (commit 2): twin RIBs compared pixel by pixel ----

int runGman(const std::string &gman, const std::string &rib) {
  const std::string command = "\"" + gman + "\" \"" + rib + "\" >/dev/null 2>&1";
  int status = std::system(command.c_str());
  return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

void renderAndCompare(const std::string &gman, const std::string &ribDir,
                      const std::string &underTest, const std::string &twin) {
  check(runGman(gman, ribDir + "/" + underTest + ".rib") == 0,
        underTest + ".rib renders");
  check(runGman(gman, ribDir + "/" + twin + ".rib") == 0, twin + ".rib renders");
  checkGoldenImage(underTest + ".tif", twin + ".tif", GOLDEN_CHANNEL_TOL,
                   GOLDEN_MAX_FRACTION, underTest + "_diff.tif");
}

// ---- Requests and malformed input (commit 1) ----

struct RunResult {
  bool timedOut = false;
  bool crashed = false;
  int exitStatus = -1;
  std::string output;
};

// Runs gman out-of-process with a hard wall-clock bound, capturing its
// combined stdout/stderr -- tests/paramclamp_test.cpp's own shape, plus an
// optional "-d" so the desync check below can read the debug keyword
// trace.
RunResult runCapturingOutput(const std::string &gman, const std::string &rib,
                             int timeoutSeconds, bool debug = false) {
  RunResult result;

  int pipeFds[2];
  if (pipe(pipeFds) != 0) {
    return result;
  }

  pid_t pid = fork();
  if (pid < 0) {
    close(pipeFds[0]);
    close(pipeFds[1]);
    return result;
  }
  if (pid == 0) {
    close(pipeFds[0]);
    dup2(pipeFds[1], STDOUT_FILENO);
    dup2(pipeFds[1], STDERR_FILENO);
    close(pipeFds[1]);
    if (debug) {
      execl(gman.c_str(), gman.c_str(), "-d", rib.c_str(), (char *) nullptr);
    } else {
      execl(gman.c_str(), gman.c_str(), rib.c_str(), (char *) nullptr);
    }
    _exit(127);
  }
  close(pipeFds[1]);

  char buf[4096];
  ssize_t n;
  while ((n = read(pipeFds[0], buf, sizeof(buf))) > 0) {
    result.output.append(buf, (std::size_t) n);
  }
  close(pipeFds[0]);

  const int pollIntervalUs = 50 * 1000;
  const int maxPolls = (timeoutSeconds * 1000000) / pollIntervalUs;
  int status = 0;
  for (int i = 0; i < maxPolls; ++i) {
    pid_t r = waitpid(pid, &status, WNOHANG);
    if (r == pid) {
      if (WIFEXITED(status)) {
        result.exitStatus = WEXITSTATUS(status);
      } else if (WIFSIGNALED(status)) {
        result.crashed = true;
      }
      return result;
    }
    usleep(pollIntervalUs);
  }

  result.timedOut = true;
  kill(pid, SIGKILL);
  waitpid(pid, &status, 0);
  return result;
}

void testMalformedFixtures(const std::string &gman,
                           const std::string &malformedDir) {
  struct Fixture {
    const char *file;
    const char *expectedWarning;
    // The five structural-check fixtures degrade: they warn, exit 0 and
    // keep parsing the trailing Sphere. A non-integer array entry is a
    // token-level syntax error instead (RIE_SYNTAX), fatal to the whole
    // file, so the two leak-regression fixtures below override both.
    int expectedExit = 0;
    bool expectsSphere = true;
  };
  const Fixture fixtures[] = {
      {"pointspolygons_short_verts.rib",
       "PointsPolygons: verts length 7 does not match nverts sum 8; "
       "ignoring."},
      {"pointsgeneralpolygons_short_nverts.rib",
       "PointsGeneralPolygons: nverts length 1 does not match nloops sum "
       "2; ignoring."},
      {"pointspolygons_negative_index.rib",
       "PointsPolygons: verts[2] = -1 is negative; ignoring."},
      {"pointsgeneralpolygons_zero_loops.rib",
       "PointsGeneralPolygons: nloops[0] = 0 is invalid; ignoring."},
      {"pointspolygons_short_p.rib",
       "Parameter \"P\": declared length 12, supplied length 9"},
      {"pointspolygons_nonint_verts.rib",
       "RIE_SYNTAX -- Non-integer in array.",
       /*expectedExit=*/1, /*expectsSphere=*/false},
      {"pointsgeneralpolygons_nonint_nverts.rib",
       "RIE_SYNTAX -- Non-integer in array.",
       /*expectedExit=*/1, /*expectsSphere=*/false},
  };

  for (const Fixture &fixture : fixtures) {
    const std::string rib = malformedDir + "/" + fixture.file;

    RunResult r = runCapturingOutput(gman, rib, 10);
    check(!r.timedOut,
          std::string(fixture.file) + ": does not hang (10s bound)");
    check(!r.crashed, std::string(fixture.file) + ": does not crash");
    check(r.exitStatus == fixture.expectedExit,
          std::string(fixture.file) + ": exits " +
              std::to_string(fixture.expectedExit));
    check(r.output.find(fixture.expectedWarning) != std::string::npos,
          std::string(fixture.file) +
              ": warns naming the rule and both values");

    if (fixture.expectsSphere) {
      RunResult debugRun = runCapturingOutput(gman, rib, 10, /*debug=*/true);
      check(debugRun.output.find("Keyword token: Sphere") !=
                std::string::npos,
            std::string(fixture.file) +
                ": the Sphere after it still parses (no desync)");
    }
  }
}

}  // namespace

int main(int argc, char *argv[]) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s <gman-binary> <tests/rib dir>\n", argv[0]);
    return 2;
  }
  const std::string gman = argv[1];
  const std::string ribDir = argv[2];

  testOneFaceIdentity();
  testIndicesNotOrder();
  testHoleThroughIndices();
  testOneBadFace();
  testUnreferencedPoints();

  check(writeCheckerTexture("checker_texture.tif"),
        "checker_texture.tif writes into the render's working directory");

  renderAndCompare(gman, ribDir, "pointspolygons_cube", "polygons_cube");
  renderAndCompare(gman, ribDir, "pointspolygons_textured",
                   "polygons_textured");
  renderAndCompare(gman, ribDir, "pointsgeneralpolygons_hole",
                   "generalpolygon_hole_twin");

  testMalformedFixtures(gman, ribDir + "/malformed");

  return checkSummary("pointspolygons holds");
}
