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
 * Every ray primitive's new placement accessors -- getObjectToCamera() on
 * the seven quadrics, getOuterLoop()/getPlaneNormal()/isDegenerate() on
 * GMANRayPolygon -- return exactly what the primitive was built with,
 * unpacking no state intersect() itself did not already hold.
 */

#include <cmath>
#include <string>
#include <vector>

#include "check.h"
#include "gmanmath.h"
#include "gmanmatrix4.h"
#include "gmanpoint.h"
#include "gmanpolygon.h"
#include "gmanraycone.h"
#include "gmanraycylinder.h"
#include "gmanraydisk.h"
#include "gmanrayhyperboloid.h"
#include "gmanrayparaboloid.h"
#include "gmanraypolygon.h"
#include "gmanraysphere.h"
#include "gmanraytorus.h"
#include "gmantransform.h"

namespace {

GMANTransform makeTransform(GMANMatrix4 matrix) {
  GMANOneMatrix storage(matrix);
  return GMANTransform(storage);
}

// Translate, rotate and a non-uniform scale, composed together: a
// placement no accessor could satisfy by accident (e.g. by returning an
// identity matrix or the untransformed shutter-open transform).
GMANMatrix4 representativePlacement() {
  GMANMatrix4 m;
  m.trans(3.0, -2.0, 7.0);
  GMANMatrix4 r;
  r.rot(GMANRadians(25.0), 1.0, 0.0, 0.0);
  m.concat(r);
  GMANMatrix4 s;
  s.scale(1.3, 0.7, 2.1);
  m.concat(s);
  return m;
}

bool matricesEqual(GMANMatrix4 const& a, GMANMatrix4 const& b) {
  for (std::size_t i = 0; i < 4; ++i) {
    for (std::size_t j = 0; j < 4; ++j) {
      if (a[i][j] != b[i][j]) {
        return false;
      }
    }
  }
  return true;
}

void testQuadrics() {
  GMANMatrix4 const placement = representativePlacement();
  GMANTransform const transform = makeTransform(placement);

  GMANRaySphere const sphere(2.0, -2.0, 2.0, 360.0, GMANParameterList(), transform);
  check(matricesEqual(sphere.getObjectToCamera(), placement), "sphere: getObjectToCamera() is the built transform");

  GMANRayCone const cone(3.0, 1.5, 360.0, GMANParameterList(), transform);
  check(matricesEqual(cone.getObjectToCamera(), placement), "cone: getObjectToCamera() is the built transform");

  GMANRayCylinder const cylinder(1.2, -1.0, 2.5, 360.0, GMANParameterList(), transform);
  check(matricesEqual(cylinder.getObjectToCamera(), placement), "cylinder: getObjectToCamera() is the built transform");

  GMANRayDisk const disk(1.0, 1.0, 360.0, GMANParameterList(), transform);
  check(matricesEqual(disk.getObjectToCamera(), placement), "disk: getObjectToCamera() is the built transform");

  GMANRayParaboloid const paraboloid(1.0, 0.0, 0.5, 360.0, GMANParameterList(), transform);
  check(matricesEqual(paraboloid.getObjectToCamera(), placement),
        "paraboloid: getObjectToCamera() is the built transform");

  RtPoint point1 = {0.5, 0.0, -1.0};
  RtPoint point2 = {1.2, 0.0, 2.0};
  GMANRayHyperboloid const hyperboloid(point1, point2, 360.0, GMANParameterList(), transform);
  check(matricesEqual(hyperboloid.getObjectToCamera(), placement),
        "hyperboloid: getObjectToCamera() is the built transform");

  GMANRayTorus const torus(2.0, 0.5, -180.0, 180.0, 360.0, GMANParameterList(), transform);
  check(matricesEqual(torus.getObjectToCamera(), placement), "torus: getObjectToCamera() is the built transform");
}

void testPolygon() {
  std::vector<GMANPoint> const verts = {
      GMANPoint(0.0, 0.0, 0.0),
      GMANPoint(4.0, 0.0, 0.0),
      GMANPoint(4.0, 3.0, 0.0),
      GMANPoint(0.0, 3.0, 0.0),
  };
  GMANRayPolygon const polygon(verts, GMANParameterList());

  std::vector<GMANPoint> const& loop = polygon.getOuterLoop();
  bool sameVertices = loop.size() == verts.size();
  for (std::size_t i = 0; sameVertices && i < verts.size(); ++i) {
    sameVertices =
        loop[i].getX() == verts[i].getX() && loop[i].getY() == verts[i].getY() && loop[i].getZ() == verts[i].getZ();
  }
  check(sameVertices, "polygon: getOuterLoop() is the built vertex list, in order");

  check(!polygon.isDegenerate(), "polygon: a real quad is not degenerate");

  GMANVector const expectedNormal = gman::newellNormal(verts) / gman::newellNormal(verts).magnitude();
  GMANVector const normal = polygon.getPlaneNormal();
  constexpr RtFloat kEpsilon = 1e-6f;
  check(std::fabs(normal.getX() - expectedNormal.getX()) < kEpsilon &&
            std::fabs(normal.getY() - expectedNormal.getY()) < kEpsilon &&
            std::fabs(normal.getZ() - expectedNormal.getZ()) < kEpsilon,
        "polygon: getPlaneNormal() is the built plane's unit normal");

  // Two coincident vertices: isDegeneratePolygon's own "fewer than three
  // vertices" clause never triggers (the ring still holds three entries),
  // but its sliver-area clause does.
  std::vector<GMANPoint> const sliver = {
      GMANPoint(0.0, 0.0, 0.0),
      GMANPoint(0.0, 0.0, 0.0),
      GMANPoint(1.0, 0.0, 0.0),
  };
  GMANRayPolygon const degenerate(sliver, GMANParameterList());
  check(degenerate.isDegenerate(), "polygon: a sliver ring is degenerate");
}

} // namespace

int main() {
  testQuadrics();
  testPolygon();

  return checkSummary("Ray primitive placement accessors return what each primitive was built with");
}
