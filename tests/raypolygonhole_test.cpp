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
 * GMANRayObjectManager::getRSGeneralPolygon builds a multi-loop
 * GMANRayPolygon: loops[0] is the outer boundary, every later loop a
 * hole. A ray through the hole's centre misses, one through the ring
 * between hole and edge hits at the analytic t, and one outside the outer
 * loop misses. getRSPolygon's own one-loop case keeps today's hits on the
 * same three rays, with no hole to cut them short.
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

constexpr RtFloat kTolerance = 1e-3f;

bool near(RtFloat a, RtFloat b, RtFloat tol = kTolerance) { return std::fabs(a - b) <= tol; }

// The outer square and hole tests/rib/generalpolygon_hole_twin.rib
// renders, side 2 and side 1 respectively, both centred on the origin in
// the z == 0 plane.
std::vector<GMANPoint> outerSquare() {
  return {GMANPoint(-1.0, -1.0, 0.0), GMANPoint(1.0, -1.0, 0.0), GMANPoint(1.0, 1.0, 0.0), GMANPoint(-1.0, 1.0, 0.0)};
}

std::vector<GMANPoint> holeSquare() {
  return {GMANPoint(-0.5, -0.5, 0.0), GMANPoint(0.5, -0.5, 0.0), GMANPoint(0.5, 0.5, 0.0), GMANPoint(-0.5, 0.5, 0.0)};
}

GMANPrimitive* runGetRSGeneralPolygonDirect(std::vector<std::vector<GMANPoint>> const& loops) {
  RtInt const nloops = (RtInt)loops.size();
  std::vector<RtInt> nverts(nloops);
  RtInt total = 0;
  for (RtInt i = 0; i < nloops; ++i) {
    nverts[i] = (RtInt)loops[i].size();
    total += nverts[i];
  }
  std::vector<RtFloat> p(3 * total);
  RtInt k = 0;
  for (RtInt i = 0; i < nloops; ++i) {
    for (GMANPoint const& pt : loops[i]) {
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
  GMANRayObjectManager mgr;
  return mgr.getRSGeneralPolygon(nloops, nverts.data(), pl, &options, &attr, &transform);
}

GMANPrimitive* runGetRSPolygonDirect(std::vector<GMANPoint> const& ring) {
  RtInt const nverts = (RtInt)ring.size();
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
  GMANRayObjectManager mgr;
  return mgr.getRSPolygon(nverts, pl, &options, &attr, &transform);
}

// A camera-space ray through object point (x, y, 0), the plane every
// fixture here shares.
GMANRay rayThrough(RtFloat x, RtFloat y) { return GMANRay(GMANPoint(x, y, -5.0), GMANVector(0.0, 0.0, 1.0)); }

void testHoleMissesFaceHitsEdgeMisses() {
  GMANPrimitive* prim = runGetRSGeneralPolygonDirect({outerSquare(), holeSquare()});
  GMANRayInterface* polygon = dynamic_cast<GMANRayInterface*>(prim);
  check(polygon != nullptr, "hole: getRSGeneralPolygon returns a GMANRayInterface");
  if (polygon == nullptr) {
    delete prim;
    return;
  }

  GMANHit centreHit;
  check(!polygon->intersect(rayThrough(0.0, 0.0), centreHit), "hole: a ray through the hole's centre misses");

  GMANHit ringHit;
  bool const ringHitFound = polygon->intersect(rayThrough(0.75, 0.0), ringHit);
  check(ringHitFound && near(ringHit.t, 5.0), "hole: a ray through the ring between hole and edge hits at t == 5");

  GMANHit outsideHit;
  check(!polygon->intersect(rayThrough(1.5, 0.0), outsideHit), "hole: a ray outside the outer loop misses");

  delete prim;
}

void testOneLoopPolygonUnchanged() {
  GMANPrimitive* prim = runGetRSPolygonDirect(outerSquare());
  GMANRayInterface* polygon = dynamic_cast<GMANRayInterface*>(prim);
  check(polygon != nullptr, "one loop: getRSPolygon returns a GMANRayInterface");
  if (polygon == nullptr) {
    delete prim;
    return;
  }

  // The same three rays the hole check casts: with no hole, the centre
  // and the ring both hit, only the point outside the outer loop misses.
  GMANHit centreHit;
  check(polygon->intersect(rayThrough(0.0, 0.0), centreHit), "one loop: a ray through the centre still hits");

  GMANHit ringHit;
  check(polygon->intersect(rayThrough(0.75, 0.0), ringHit), "one loop: a ray through the ring still hits");

  GMANHit outsideHit;
  check(!polygon->intersect(rayThrough(1.5, 0.0), outsideHit), "one loop: a ray outside the outer loop still misses");

  delete prim;
}

} // namespace

int main() {
  testHoleMissesFaceHitsEdgeMisses();
  testOneLoopPolygonUnchanged();

  return checkSummary("GeneralPolygon holes miss, faces hit, and Polygon keeps today's hits");
}
