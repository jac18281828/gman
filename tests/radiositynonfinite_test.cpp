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
 * GMANRadiosityMesh converts a polygon's extent over the maximum edge length
 * into a cell count, and a hit's position into a cell, without leaving the
 * integer type's range: an edge length so small that the quotient overflows a
 * float caps the polygon at kMaxDivisions cells along each axis. A debug
 * build's UBSan traps the out-of-range conversion the guards prevent.
 */

#include <cstddef>
#include <limits>
#include <string>
#include <vector>

#include "check.h"
#include "gmanlinearworldmanager.h"
#include "gmanparameterlist.h"
#include "gmanpoint.h"
#include "gmanradiositymesh.h"
#include "gmanray.h"
#include "gmanraypolygon.h"
#include "ri.h"

namespace {

std::vector<GMANPoint> square(RtFloat half) {
  return {GMANPoint(-half, -half, 6.0f), GMANPoint(half, -half, 6.0f), GMANPoint(half, half, 6.0f),
          GMANPoint(-half, half, 6.0f)};
}

void testQuotientBeyondFloatRange() {
  GMANLinearWorldManager world;
  world.add(new GMANRayPolygon(square(4.0f), GMANParameterList()));
  GMANRadiosityMesh mesh;
  mesh.build(world, std::numeric_limits<RtFloat>::min());
  check(mesh.getElementCount() == GMANRadiosityMesh::kMaxDivisions * GMANRadiosityMesh::kMaxDivisions,
        "a polygon over an edge length of 1e-38 divides at kMaxDivisions along each axis");
}

void testHugeExtentOverTinyEdge() {
  GMANLinearWorldManager world;
  world.add(new GMANRayPolygon(square(1e5f), GMANParameterList()));
  GMANRadiosityMesh mesh;
  mesh.build(world, 1e-34f);
  check(mesh.getElementCount() == GMANRadiosityMesh::kMaxDivisions * GMANRadiosityMesh::kMaxDivisions,
        "a polygon 2e5 wide over an edge length of 1e-34 divides at kMaxDivisions along each axis");
}

void testOrdinaryDivisionIsUnchanged() {
  GMANLinearWorldManager world;
  world.add(new GMANRayPolygon(square(4.0f), GMANParameterList()));
  GMANRadiosityMesh mesh;
  mesh.build(world, 2.0f);
  check(mesh.getElementCount() == 16, "an 8 by 8 polygon over an edge length of 2 divides into 16 elements");
}

void testLocateFarBeyondThePolygon() {
  GMANLinearWorldManager world;
  auto* polygon = new GMANRayPolygon(square(4.0f), GMANParameterList());
  world.add(polygon);
  GMANRadiosityMesh mesh;
  mesh.build(world, 2.0f);

  for (RtFloat const far : {1e30f, -1e30f, std::numeric_limits<RtFloat>::max()}) {
    GMANHit hit;
    hit.primitive = polygon;
    hit.point = GMANPoint(far, 0.0f, 6.0f);
    GMANRadiosityLocation location;
    bool const located = mesh.locate(hit, location);
    check(located, "a hit at x of " + std::to_string(far) + " resolves to an element");
    check(!located || location.element < mesh.getElementCount(),
          "a hit at x of " + std::to_string(far) + " resolves to an element inside the mesh");
  }
}

} // namespace

int main() {
  testQuotientBeyondFloatRange();
  testHugeExtentOverTinyEdge();
  testOrdinaryDivisionIsUnchanged();
  testLocateFarBeyondThePolygon();
  return checkSummary("radiositynonfinite: all checks passed");
}
