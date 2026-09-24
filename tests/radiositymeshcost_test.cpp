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
 * Counts GMANParametric::getLocation calls GMANRadiosityMesh::build's own
 * resolution search makes for one primitive, and bounds them. A search
 * that grows nu and nv one step at a time, rebuilding and re-measuring the
 * whole grid every step, costs O(kMaxDivisions^3) calls once a primitive's
 * true resolution reaches the cap; this pins a bound far below that.
 */

#include <cstddef>
#include <cstdio>

#include "check.h"
#include "gmanlinearworldmanager.h"
#include "gmanparameterlist.h"
#include "gmanradiositymesh.h"
#include "gmanraycylinder.h"

namespace {

// A cylinder counting its own getLocation calls, forwarding to the real
// implementation so the mesh it feeds is the genuine one.
class CountingCylinder : public GMANRayCylinder {
public:
  using GMANRayCylinder::GMANRayCylinder;

  GMANPoint getLocation(double u, double v) override {
    ++evaluationCount;
    return GMANRayCylinder::getLocation(u, v);
  }

  std::size_t evaluationCount = 0;
};

} // namespace

int main() {
  RtFloat const radius = 1.0f, zmin = -1.0f, zmax = 1.0f;

  // Far below either the circumference (2*pi*radius) or the height (2.0)
  // divided by kMaxDivisions, so the search must drive both nu and nv to
  // the cap: the search's own worst case, and the one a linear search pays
  // for most.
  RtFloat const maxEdgeLength = 0.0005f;

  auto* cylinder = new CountingCylinder(radius, zmin, zmax, 360.0, GMANParameterList());
  GMANLinearWorldManager worldManager;
  worldManager.add(cylinder);

  GMANRadiosityMesh mesh;
  mesh.build(worldManager, maxEdgeLength);

  check(mesh.getElementCount() == GMANRadiosityMesh::kMaxDivisions * GMANRadiosityMesh::kMaxDivisions,
        "both axes reach kMaxDivisions at this edge length");

  // B, derived from a doubling-then-bisection search over nu and nv
  // (renderers/radiosity/gmanradiositymesh.cpp's resolveAxisCount and
  // chooseParametricResolution). maxEdgeLength here is unreachable even at
  // the cap, so every axis resolve takes exactly ceil(log2(kMaxDivisions)) +
  // 1 = 9 probes (doubling from 1 to the cap; bisection never runs, since
  // the cap itself never satisfies the bound). Each probe rebuilds a
  // (candidate+1) x (otherAxis+1) grid and re-measures both its own axes'
  // edges, for 3*candidate*otherAxis + 2*candidate + 2*otherAxis + 1 calls;
  // summed over the doubling sequence 1, 2, 4, ..., 256 (9 terms, summing to
  // 511), one axis resolve against a fixed otherAxis costs exactly
  // 1551*otherAxis + 1031 calls.
  //
  // A cylinder's own two axes never couple (its circumference is the same
  // at every height, its height edges the same at every angle), so
  // chooseParametricResolution's relaxation settles in exactly 2 rounds: the
  // first resolves nu against nv = 1, then nv against the resolved nu = 256;
  // the second re-resolves both against 256 and finds neither changed. That
  // is 3 resolves against otherAxis = 256 and 1 against otherAxis = 1, plus
  // one final (257 x 257) grid build:
  //   B = 3*(1551*256 + 1031) + (1551*1 + 1031) + 257*257
  //     = 3*398087 + 2582 + 66049 = 1262892
  // rounded up for platform slack. A one-step-at-a-time search makes
  // 17007488 calls for this same cylinder, over 13x above B, so it fails
  // this check.
  constexpr std::size_t B = 1300000;

  std::printf("resolution search: cylinder evaluationCount=%zu B=%zu\n", cylinder->evaluationCount, B);
  check(cylinder->evaluationCount <= B,
        "the resolution search costs at most B getLocation calls, not a cubic one-step search's");

  return checkSummary("GMANRadiosityMesh's resolution search stays within its own O(N^2 log N) bound");
}
