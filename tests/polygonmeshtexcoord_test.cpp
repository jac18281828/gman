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
 * tests/rib/pointspolygons_textured.rib's own geometry: two unit-square
 * faces sharing an edge, "st" gathered per face through "verts" out of
 * the shared "P" pool rather than resolved by local slot. Face 0 spans
 * world x in [0, 1] and maps s = x; face 1 spans x in [1, 2] and maps
 * s = 2 - x; t = y on both. Both faces' own centroids sit at t = 0.5 and
 * the same s = 0.5, so a constant, swapped or mirrored (s, t) would still
 * pass there -- this casts at each face's own off-centre local (0.25,
 * 0.3) instead, where the two faces disagree.
 */

#include <cmath>
#include <vector>

#include "check.h"
#include "gmanattributes.h"
#include "gmandictionary.h"
#include "gmanoptions.h"
#include "gmanparameterlist.h"
#include "gmanray.h"
#include "gmanrayinterface.h"
#include "gmanrayobjectmanager.h"
#include "gmantransform.h"

namespace {

constexpr RtFloat kTolerance = 1e-5f;

bool near(RtFloat a, RtFloat b, RtFloat tol = kTolerance) { return std::fabs(a - b) <= tol; }

GMANPrimitive* buildTexturedMesh() {
  // tests/rib/pointspolygons_textured.rib's own "P"/"st"/"verts", read
  // directly here rather than through a RIB parse.
  std::vector<GMANPoint> const points = {
      GMANPoint(0, 0, 0), GMANPoint(1, 0, 0), GMANPoint(2, 0, 0),
      GMANPoint(0, 1, 0), GMANPoint(1, 1, 0), GMANPoint(2, 1, 0),
  };
  RtFloat st[] = {0, 0, 1, 0, 0, 0, 0, 1, 1, 1, 0, 1};
  std::vector<RtInt> nverts = {4, 4};
  std::vector<RtInt> verts = {0, 1, 4, 3, 1, 2, 5, 4};

  RtInt const pointCount = (RtInt)points.size();
  std::vector<RtFloat> p(3 * pointCount);
  for (RtInt i = 0; i < pointCount; ++i) {
    p[3 * i] = points[i].getX();
    p[3 * i + 1] = points[i].getY();
    p[3 * i + 2] = points[i].getZ();
  }

  GMANDictionary dictionary;
  RtToken tokens[2] = {RI_P, RI_ST};
  RtPointer parms[2] = {(RtPointer)p.data(), (RtPointer)st};
  GMANParameterList pl(dictionary, 2, tokens, parms, /*vertex=*/pointCount, /*varying=*/pointCount,
                       /*uniform=*/(RtInt)nverts.size(), /*facevarying=*/(RtInt)verts.size());
  GMANOptions options;
  GMANAttributes attr;
  GMANTransform transform;
  GMANRayObjectManager mgr;
  return mgr.getRSPointsPolygon((RtInt)nverts.size(), nverts.data(), verts.data(), pl, &options, &attr, &transform);
}

// A camera-space ray straight through world (x, y, 0).
GMANRay rayThrough(RtFloat x, RtFloat y) { return GMANRay(GMANPoint(x, y, -5.0), GMANVector(0.0, 0.0, 1.0)); }

void testPerFaceStGatheredThroughVerts() {
  GMANPrimitive* meshPrim = buildTexturedMesh();
  GMANRayInterface* mesh = dynamic_cast<GMANRayInterface*>(meshPrim);
  check(mesh != nullptr, "per-face st: getRSPointsPolygon returns a GMANRayInterface");
  if (mesh == nullptr) {
    delete meshPrim;
    return;
  }

  // Face 0: world x in [0, 1], local (0.25, 0.3) -> world (0.25, 0.3).
  // s == x, t == y.
  {
    GMANHit hit;
    bool const found = mesh->intersect(rayThrough(0.25f, 0.3f), hit);
    check(found, "face 0: the off-centre ray hits");
    check(near(hit.u, 0.25f), "face 0: s == x == 0.25");
    check(near(hit.v, 0.3f), "face 0: t == y == 0.3");
  }

  // Face 1: world x in [1, 2], local (0.25, 0.3) -> world (1.25, 0.3).
  // s == 2 - x, t == y.
  {
    GMANHit hit;
    bool const found = mesh->intersect(rayThrough(1.25f, 0.3f), hit);
    check(found, "face 1: the off-centre ray hits");
    check(near(hit.u, 0.75f), "face 1: s == 2 - x == 0.75");
    check(near(hit.v, 0.3f), "face 1: t == y == 0.3");
  }

  delete meshPrim;
}

} // namespace

int main() {
  testPerFaceStGatheredThroughVerts();

  return checkSummary("PointsPolygons per-face s/t is gathered through \"verts\", not indexed by local slot");
}
