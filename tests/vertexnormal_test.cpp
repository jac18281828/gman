/* SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Copyright (c) 2026 John Cairns <john@2ad.com>
 */

/*
 * Review finding: GMANVertex::calcNormal (the area-weighted average of
 * adjacent face normals, phase-3-REPORT.md's step 2) has no call site in
 * this tree -- setFaceList is never called by any primitive, since every
 * quadric goes through GMANParametric::getNormal instead. Untested dead
 * code. This exercises it directly: two faces of known normal and area
 * are attached to a vertex's face list, and the result is checked against
 * the hand-computed area-weighted average.
 *
 * Revert check (verified by actually reverting, not asserted): restoring
 * the body this phase found -- `#if 0`'d around the nonexistent members
 * `flist`/`GetNormal` -- would not compile at all, let alone pass.
 * Reverting instead to an unweighted average (dropping `* getArea()`)
 * makes testAreaWeightedAverage fail: the two faces here have area 1 and
 * 2, so an unweighted average would disagree with the area-weighted one
 * this test asserts.
 */

#include <cmath>
#include <cstdio>
#include <string>

#include "check.h"
#include "gmanface.h"
#include "gmanpoint.h"
#include "gmantypes.h"
#include "gmanvector.h"
#include "gmanvertex.h"

namespace {

void testAreaWeightedAverage() {
  // Face A: unit square in the z=0 plane -- normal (0,0,1), area 1.
  GMANVertex cornersA[4];
  cornersA[0].setLocation(GMANPoint(0.0, 0.0, 0.0));
  cornersA[1].setLocation(GMANPoint(1.0, 0.0, 0.0));
  cornersA[2].setLocation(GMANPoint(1.0, 1.0, 0.0));
  cornersA[3].setLocation(GMANPoint(0.0, 1.0, 0.0));
  GMANVertex *vertsA[GMAN_NFACE_VERTS] = {&cornersA[0], &cornersA[1],
                                           &cornersA[2], &cornersA[3]};
  GMANFace faceA(vertsA, nullptr);
  faceA.calcArea();
  faceA.calcNormal();
  check(std::fabs(faceA.getArea() - 1.0) < 1e-5,
        "face A: area is 1.0 (a hand-computable reference)");
  check(faceA.getNormal().dot(GMANVector(0.0, 0.0, 1.0)) > 0.999,
        "face A: normal is (0,0,1) (a hand-computable reference)");

  // Face B: a 1x2 rectangle in the x=0 plane -- normal (1,0,0), area 2.
  // Different plane, different area, so the two faces' contributions to
  // an area-weighted average are neither equal nor parallel -- an
  // unweighted average or a sign error both produce a different answer
  // than the one asserted below.
  GMANVertex cornersB[4];
  cornersB[0].setLocation(GMANPoint(0.0, 0.0, 0.0));
  cornersB[1].setLocation(GMANPoint(0.0, 1.0, 0.0));
  cornersB[2].setLocation(GMANPoint(0.0, 1.0, 2.0));
  cornersB[3].setLocation(GMANPoint(0.0, 0.0, 2.0));
  GMANVertex *vertsB[GMAN_NFACE_VERTS] = {&cornersB[0], &cornersB[1],
                                           &cornersB[2], &cornersB[3]};
  GMANFace faceB(vertsB, nullptr);
  faceB.calcArea();
  faceB.calcNormal();
  check(std::fabs(faceB.getArea() - 2.0) < 1e-5,
        "face B: area is 2.0 (a hand-computable reference)");
  check(faceB.getNormal().dot(GMANVector(1.0, 0.0, 0.0)) > 0.999,
        "face B: normal is (1,0,0) (a hand-computable reference)");

  // Attach both faces to one vertex's face list and let calcNormal
  // average them. Neither face actually shares a corner with this
  // vertex -- calcNormal only reads the face list's normals and areas,
  // so this isolates its arithmetic from tessellation topology.
  GMANFaceList faceList;
  faceList.push_back(&faceA);
  faceList.push_back(&faceB);

  GMANVertex vertex;
  vertex.setFaceList(&faceList);
  vertex.calcNormal();

  // Hand-computed: normalize(normalA*areaA + normalB*areaB)
  //              = normalize((0,0,1)*1 + (1,0,0)*2) = normalize((2,0,1))
  GMANVector expected(2.0, 0.0, 1.0);
  expected.normalize();

  GMANVector actual = vertex.getNormal();
  check(std::fabs(actual.magnitude() - 1.0) < 1e-5,
        "calcNormal: result is normalized");
  check(actual.dot(expected) > 0.9999,
        "calcNormal: area-weighted average of the two faces' normals "
        "matches the hand-computed direction");
}

void testEmptyFaceListLeavesDefaultNormal() {
  // calcNormal on a vertex with no face list must not crash, and must
  // leave the default zero normal untouched -- the same "no shading
  // normal set" state createParametric's fallback path relies on.
  GMANVertex vertex;
  vertex.calcNormal();
  GMANVector normal = vertex.getNormal();
  check(normal.magnitude() < 1e-6,
        "calcNormal: an empty face list leaves the default zero normal");
}

}  // namespace

int main() {
  testAreaWeightedAverage();
  testEmptyFaceListLeavesDefaultNormal();

  return checkSummary("vertex normal averaging holds");
}
