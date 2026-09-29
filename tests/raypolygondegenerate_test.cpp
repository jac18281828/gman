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
 * A degenerate Polygon -- four collinear points -- traces as the empty
 * stub: neither a GMANRayPolygon nor a GMANRayPolygonMesh.
 */

#include <optional>
#include <vector>

#include "check.h"
#include "gmanattributes.h"
#include "gmandictionary.h"
#include "gmanoptions.h"
#include "gmanpolygonmesh.h"
#include "gmanpolygonmeshfactory.h"
#include "gmanrayinterface.h"
#include "gmanrayobjectmanager.h"
#include "gmanraypolygon.h"
#include "gmanraypolygonmesh.h"
#include "gmantransform.h"

namespace {

// Four collinear points: the factory's nverts rules reject only a negative
// or overflowing count, so this reaches the manager, but the outer loop is
// degenerate.
std::vector<GMANPoint> collinearFour() {
  return {GMANPoint(0, 0, 0), GMANPoint(1, 0, 0), GMANPoint(2, 0, 0), GMANPoint(3, 0, 0)};
}

// Builds its mesh through the factory function Polygon calls, then calls
// getRSPolygonMesh, as raypolygonhole_test.cpp's own runGetRSPolygonDirect
// does.
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
  std::optional<GMANPolygonMesh> const mesh = gman::polygonMesh(nverts, dictionary, 1, tokens, parms, nullptr);
  GMANOptions options;
  GMANAttributes attr;
  GMANTransform transform;
  GMANRayObjectManager mgr;
  check(mesh.has_value(), "runGetRSPolygonDirect: the factory builds a mesh");
  if (!mesh.has_value()) {
    return nullptr;
  }
  return mgr.getRSPolygonMesh(*mesh, &options, &attr, &transform);
}

void testDegeneratePolygonTracesAsEmptyStub() {
  GMANPrimitive* prim = runGetRSPolygonDirect(collinearFour());
  GMANRayPolygon const* polygon = dynamic_cast<GMANRayPolygon const*>(prim);
  GMANRayPolygonMesh const* mesh = dynamic_cast<GMANRayPolygonMesh const*>(prim);
  check(polygon == nullptr, "degenerate Polygon: not a GMANRayPolygon");
  check(mesh == nullptr, "degenerate Polygon: not a GMANRayPolygonMesh");
  delete prim;
}

} // namespace

int main() {
  testDegeneratePolygonTracesAsEmptyStub();

  return checkSummary("a degenerate Polygon traces as the empty stub");
}
