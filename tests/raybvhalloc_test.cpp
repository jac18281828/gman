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
 * GMANRayBVH::nearestHit's own traversal, and GMANRayOccluder::transmission's
 * walk across it, make no heap allocation. A global operator new/delete
 * override counts every allocation this process makes (paramlist_test.cpp's
 * own technique); a zero delta across a batch of calls is evidence only once
 * the counter is shown to see the library at all, by first checking that
 * build() -- which does allocate -- moves it. AddressSanitizer replaces
 * allocation at a level this override cannot see, so the counting checks
 * skip (and print so) under it; the nearestHit and transmission calls
 * themselves still run either way.
 *
 * The fixture: 20 spheres at x = 0, 10, ..., 190 (alternating y = +2/-2,
 * radius 1), plus 3 more overlapping spheres isolated at x = -1000 (used
 * for the hit and transmission checks below). nth_element's own median
 * split always sends the smaller half of a range's x values left and the
 * larger half right, so a node's right child is always sized ceil(count /
 * 2) -- at least as large, hence at least as tall, as its left. A ray
 * travelling in -x visits the larger-x (right) child of every node first,
 * its own box's near face being closer that way, so taking the nearer
 * child at every level follows the tree's single deepest path. Over these
 * 23 spheres that path runs root(23) -> right(12) -> right(6) -> a
 * 3-primitive leaf, three levels below the root, so the traversal's own
 * stack peaks at 4 entries: one pending sibling per level (3) plus the two
 * children just pushed at the deepest one. testPeakStack's own probe ray
 * starts past every one of the 20 spheres' own x and travels toward -x
 * across their whole span at y = 0, z = 0: every node's box along that
 * path still includes y = 0 (the alternation), but no individual sphere
 * does (each one offset past its own radius), so the ray reaches that
 * full depth without ever finding a hit that would shrink its own
 * remaining interval.
 */

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <new>

#include "check.h"
#include "gmanlightsourcemgr.h"
#include "gmanlinearworldmanager.h"
#include "gmanmatrix4.h"
#include "gmanparameterlist.h"
#include "gmanray.h"
#include "gmanraybvh.h"
#include "gmanrayoccluder.h"
#include "gmanraysphere.h"
#include "gmantransform.h"

#if defined(__SANITIZE_ADDRESS__)
#define GMAN_ADDRESS_SANITIZED 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define GMAN_ADDRESS_SANITIZED 1
#endif
#endif
#ifndef GMAN_ADDRESS_SANITIZED
#define GMAN_ADDRESS_SANITIZED 0
#endif

namespace {

// Counts every operator new call this process makes, so a batch of calls
// can be bracketed and its own delta checked against 0.
std::atomic<long> allocationCount{0};

} // namespace

void* operator new(std::size_t n) {
  void* p = std::malloc(n != 0 ? n : 1);
  if (p == nullptr) {
    throw std::bad_alloc();
  }
  ++allocationCount;
  return p;
}

void operator delete(void* p) noexcept { std::free(p); }

// GCC's -Wsized-deallocation requires the sized form once the unsized one
// is replaced; both just forward, since this allocator counts calls, not
// sizes.
void operator delete(void* p, std::size_t) noexcept { ::operator delete(p); }

void* operator new[](std::size_t n) { return ::operator new(n); }

void operator delete[](void* p) noexcept { ::operator delete(p); }

void operator delete[](void* p, std::size_t) noexcept { ::operator delete(p); }

namespace {

constexpr std::size_t kStackSphereCount = 20;
constexpr RtFloat kStackSpacing = 10.0f;
constexpr RtFloat kStackOffset = 2.0f;
constexpr RtFloat kStackRadius = 1.0f;

constexpr std::size_t kOverlapSphereCount = 3;
constexpr RtFloat kOverlapX = -1000.0f;
constexpr RtFloat kOverlapSpacing = 3.0f;
constexpr RtFloat kOverlapRadius = 2.0f;
constexpr RtFloat kOverlapZStart = 10.0f;

// The stated peak: see this file's own leading comment for how the fixture
// forces it.
constexpr std::size_t kStatedPeakStack = 4;

GMANTransform makeTransform(GMANMatrix4 matrix) {
  GMANOneMatrix storage(matrix);
  return GMANTransform(storage);
}

GMANTransform translated(RtFloat dx, RtFloat dy, RtFloat dz) {
  GMANMatrix4 m;
  m.trans(dx, dy, dz);
  return makeTransform(m);
}

// Every sphere's Os below 1 in some channel, so a transmission walk that
// crosses one keeps going rather than cutting to black.
void setPartlyTransparent(GMANRayInterface* primitive) {
  gman::Appearance appearance;
  appearance.Os = GMANColor(0.5f, 0.5f, 0.5f);
  primitive->setAppearance(appearance);
}

// Builds the fixture described in this file's own leading comment: 20
// x-spread, y-alternating spheres (the peak-stack probe's own target) plus
// 3 more, overlapping along z well away from them (the hit/transmission
// probes' own target).
void buildFixture(GMANLinearWorldManager& worldManager) {
  for (std::size_t i = 0; i < kStackSphereCount; ++i) {
    RtFloat const x = kStackSpacing * (RtFloat)i;
    RtFloat const y = (i % 2 == 0) ? kStackOffset : -kStackOffset;
    auto* sphere =
        new GMANRaySphere(kStackRadius, -kStackRadius, kStackRadius, 360.0, GMANParameterList(), translated(x, y, 0.0));
    setPartlyTransparent(sphere);
    worldManager.add(sphere);
  }

  for (std::size_t i = 0; i < kOverlapSphereCount; ++i) {
    RtFloat const z = kOverlapZStart + kOverlapSpacing * (RtFloat)i;
    auto* sphere = new GMANRaySphere(kOverlapRadius, -kOverlapRadius, kOverlapRadius, 360.0, GMANParameterList(),
                                     translated(kOverlapX, 0.0, z));
    setPartlyTransparent(sphere);
    worldManager.add(sphere);
  }
}

// The probe ray this file's own leading comment describes: starts past
// every x-spread sphere's own x and travels toward -x across their whole
// span at y = 0, z = 0, always missing (each sphere's own y offset clears
// its radius) while driving the traversal stack to its stated peak.
GMANRay peakStackRay() {
  RtFloat const originX = kStackSpacing * (RtFloat)(kStackSphereCount - 1) + kStackSpacing + 5.0f;
  return GMANRay(GMANPoint(originX, 0.0, 0.0), GMANVector(-1.0, 0.0, 0.0), 0.0, originX + 10.0f);
}

// A ray that hits the overlap cluster's own nearest sphere face.
GMANRay overlapHitRay() { return GMANRay(GMANPoint(kOverlapX, 0.0, -10.0), GMANVector(0.0, 0.0, 1.0)); }

// A ray that clears every primitive in the fixture.
GMANRay clearMissRay() { return GMANRay(GMANPoint(0.0, 1000.0, 0.0), GMANVector(1.0, 0.0, 0.0)); }

void testPeakStack(GMANRayBVH const& bvh) {
  GMANRay const ray = peakStackRay();
  GMANHit hit;
  GMANRayInterface const* prim = nullptr;
  bool const found = bvh.nearestHit(ray, hit, prim);
  check(!found, "peak stack: the probe ray clears every x-spread sphere, as its own construction requires");
  std::printf("peak stack: fixture states k=%zu (see this file's own leading comment for how it is forced)\n",
              kStatedPeakStack);
}

void testHitAndMissBatch(GMANRayBVH const& bvh) {
  GMANRay const hitRay = overlapHitRay();
  GMANRay const missRay = clearMissRay();
  GMANRay const peakRay = peakStackRay();

  // The counted region holds only the library calls: check()'s own
  // std::string argument can allocate, so building and checking the
  // messages happens outside it, from the results captured here.
  constexpr int kBatchCount = 3;
  bool hitFound[kBatchCount];
  bool missFound[kBatchCount];
  bool peakFound[kBatchCount];

  long const before = allocationCount.load();
  for (int i = 0; i < kBatchCount; ++i) {
    GMANHit hit;
    GMANRayInterface const* prim = nullptr;
    hitFound[i] = bvh.nearestHit(hitRay, hit, prim);

    GMANHit missHit;
    GMANRayInterface const* missPrim = nullptr;
    missFound[i] = bvh.nearestHit(missRay, missHit, missPrim);

    GMANHit peakHit;
    GMANRayInterface const* peakPrim = nullptr;
    peakFound[i] = bvh.nearestHit(peakRay, peakHit, peakPrim);
  }
  long const after = allocationCount.load();

  for (int i = 0; i < kBatchCount; ++i) {
    check(hitFound[i], "hit/miss batch: the overlap-cluster ray hits");
    check(!missFound[i], "hit/miss batch: the clear-miss ray misses");
    check(!peakFound[i], "hit/miss batch: the peak-stack ray misses");
  }

#if GMAN_ADDRESS_SANITIZED
  std::printf("skip: a batch of nearestHit calls (hits and misses) makes no heap allocation (ASan-built)\n");
#else
  check(after == before, "a batch of nearestHit calls (hits and misses) makes no heap allocation");
#endif
}

void testTransmissionBatch(GMANRayOccluder const& occluder) {
  GMANLight const light(GMAN_LIGHT_DISTANT, GMANColor(1.0f, 1.0f, 1.0f), GMANPoint(), GMANVector(0.0, 0.0, -1.0));
  GMANPoint const P(kOverlapX, 0.0, 0.0);
  GMANVector const towardLight(0.0, 0.0, 1.0);
  constexpr RtFloat kDistance = 100.0f;

  // Crosses all 3 overlapping spheres (6 surfaces), 0.5 opacity each:
  // 0.5^6, comfortably above the negligible-transmission cutoff, so the
  // walk never cuts short.
  GMANColor const expected(0.015625f, 0.015625f, 0.015625f);

  // The counted region holds only the library call, for the same reason
  // as testHitAndMissBatch above.
  constexpr int kBatchCount = 3;
  GMANColor results[kBatchCount];

  long const before = allocationCount.load();
  for (int i = 0; i < kBatchCount; ++i) {
    results[i] = occluder.transmission(light, P, towardLight, GMANVector(), kDistance, 0.0f);
  }
  long const after = allocationCount.load();

  for (int i = 0; i < kBatchCount; ++i) {
    check(results[i].getRed() == expected.getRed() && results[i].getGreen() == expected.getGreen() &&
              results[i].getBlue() == expected.getBlue(),
          "transmission batch: the walk crosses all 3 overlapping spheres");
  }

#if GMAN_ADDRESS_SANITIZED
  std::printf("skip: a batch of transmission walks (each crossing multiple spheres) makes no heap allocation "
              "(ASan-built)\n");
#else
  check(after == before, "a batch of transmission walks (each crossing multiple spheres) makes no heap allocation");
#endif
}

} // namespace

int main() {
  GMANLinearWorldManager worldManager;
  buildFixture(worldManager);

  GMANRayBVH bvh;
  long const beforeBuild = allocationCount.load();
  bvh.build(worldManager);
  long const afterBuild = allocationCount.load();

#if GMAN_ADDRESS_SANITIZED
  std::printf("skip: the counter sees build()'s own allocations (ASan-built)\n");
#else
  check(afterBuild > beforeBuild, "the counter sees build()'s own allocations");
#endif

  GMANRayOccluder occluder(bvh);

  testPeakStack(bvh);
  testHitAndMissBatch(bvh);
  testTransmissionBatch(occluder);

  return checkSummary("GMANRayBVH::nearestHit and GMANRayOccluder::transmission: no heap allocation");
}
