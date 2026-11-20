/* SPDX-License-Identifier: LGPL-2.1-or-later */

/* This is part of GMAN, a RenderMan-compatible renderer.
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

#include <algorithm>
#include <cmath>
#include <limits>

#include "gmanmath.h"
#include "gmanraybbox.h"
#include "gmanraybvh.h"
#include "gmanraypolygonmesh.h"

namespace {

constexpr std::size_t kBinCount = 16;

// A camera-space axis coordinate too close to +/-RI_INFINITY (the
// default box's own bound) to average safely is not expected -- every
// assigned box is finite -- but the fallback to 0 costs nothing and
// guards the arithmetic rather than trusting the invariant blindly.
RtFloat centroidAxis(RtFloat lo, RtFloat hi) {
  RtFloat const mid = (lo + hi) * (RtFloat)0.5;
  return std::isfinite(mid) ? mid : (RtFloat)0.0;
}

GMANPoint boxCentroid(GMANBBox const& box) {
  GMANPoint const lo = box.getMin();
  GMANPoint const hi = box.getMax();
  return GMANPoint(centroidAxis(lo.getX(), hi.getX()), centroidAxis(lo.getY(), hi.getY()),
                   centroidAxis(lo.getZ(), hi.getZ()));
}

RtFloat axisValue(GMANPoint const& p, int axis) { return axis == 0 ? p.getX() : axis == 1 ? p.getY() : p.getZ(); }

RtFloat surfaceArea(GMANBBox const& box) {
  GMANPoint const lo = box.getMin();
  GMANPoint const hi = box.getMax();
  RtFloat const dx = hi.getX() - lo.getX();
  RtFloat const dy = hi.getY() - lo.getY();
  RtFloat const dz = hi.getZ() - lo.getZ();
  return (RtFloat)2.0 * (dx * dy + dy * dz + dz * dx);
}

GMANBBox unionOf(GMANBBox const& a, GMANBBox const& b) {
  return GMANBBox(gman::pointMin(a.getMin(), b.getMin()), gman::pointMax(a.getMax(), b.getMax()));
}

std::size_t binOf(RtFloat centroid, RtFloat low, RtFloat extent) {
  RtFloat const scaled = (centroid - low) / extent * (RtFloat)kBinCount;
  return (std::size_t)GMANMin(GMANMax(scaled, (RtFloat)0.0), (RtFloat)(kBinCount - 1));
}

struct Bin {
  std::size_t count = 0;
  GMANBBox bounds;

  void add(GMANBBox const& box) { merge({1, box}); }

  void merge(Bin const& other) {
    if (other.count == 0) {
      return;
    }
    bounds = count == 0 ? other.bounds : unionOf(bounds, other.bounds);
    count += other.count;
  }

  RtFloat weightedArea() const { return count == 0 ? (RtFloat)0.0 : surfaceArea(bounds) * (RtFloat)count; }
};

} // namespace

std::size_t GMANRayBVH::childCapacity(std::size_t depth) {
  std::size_t const levelsBelowChild = kMaxTreeDepth - depth - 1;
  if (levelsBelowChild + 2 >= std::numeric_limits<std::size_t>::digits) {
    return std::numeric_limits<std::size_t>::max();
  }
  return kLeafSize << levelsBelowChild;
}

void GMANRayBVH::Node::setBounds(GMANBBox const& bounds) {
  bbox = bounds;
  GMANPoint const min = bounds.getMin();
  GMANPoint const max = bounds.getMax();
  lo = {min.getX(), min.getY(), min.getZ()};
  hi = {max.getX(), max.getY(), max.getZ()};
}

GMANRayBVH::PreparedRay::PreparedRay(GMANRay const& ray)
    : origin{ray.getOrigin().getX(), ray.getOrigin().getY(), ray.getOrigin().getZ()}, inverseDirection{}, parallel{},
      tMin(ray.getTMin()), tMax(ray.getTMax()) {
  GMANVector const& rayDirection = ray.getDirection();
  RtFloat const direction[3] = {rayDirection.getX(), rayDirection.getY(), rayDirection.getZ()};
  for (std::size_t axis = 0; axis < 3; ++axis) {
    parallel[axis] = direction[axis] == (RtFloat)0.0;
    if (!parallel[axis]) {
      inverseDirection[axis] = (RtFloat)1.0 / direction[axis];
    }
  }
}

bool GMANRayBVH::PreparedRay::overlaps(Node const& node, RtFloat limit, RtFloat& entryOut) const {
  RtFloat entry = tMin;
  RtFloat exit = limit;
  for (std::size_t axis = 0; axis < 3; ++axis) {
    if (parallel[axis]) {
      if (origin[axis] < node.lo[axis] || origin[axis] > node.hi[axis]) {
        entryOut = entry;
        return false;
      }
      continue;
    }
    RtFloat t0 = (node.lo[axis] - origin[axis]) * inverseDirection[axis];
    RtFloat t1 = (node.hi[axis] - origin[axis]) * inverseDirection[axis];
    if (t0 > t1)
      std::swap(t0, t1);
    entry = GMANMax(entry, t0);
    exit = GMANMin(exit, t1);
  }
  entryOut = entry;
  return entry <= exit;
}

RtFloat GMANRayBVH::Search::limit(PreparedRay const& ray) const { return found ? GMANMin(ray.tMax, hit.t) : ray.tMax; }

bool GMANRayBVH::Search::entersBox(PreparedRay const& ray, Node const& node, RtFloat& entry) const {
  if (boxTests) {
    ++*boxTests;
  }
  return ray.overlaps(node, limit(ray), entry);
}

void GMANRayBVH::build(GMANWorldManager& worldManager) {
  primitives.clear();
  nodes.clear();
  rootIndex = -1;
  deepestLeaf = 0;

  std::size_t index = 0;
  // Builds one Entry from a ray primitive and appends it, keeping the
  // insertion order build()'s own tie-break in testLeaf relies on. A
  // lambda local to build() rather than a free function: Entry is private,
  // and a member function's own scope is what gives this the access it
  // needs.
  auto addEntry = [this, &index](GMANRayInterface const* primitive) {
    Entry entry;
    entry.primitive = primitive;
    entry.bbox = primitive->getBBox();
    entry.centroid = boxCentroid(entry.bbox);
    entry.insertionIndex = index++;
    primitives.push_back(entry);
  };

  for (GMANPrimitive* primitive = worldManager.getFirst(); primitive; primitive = worldManager.getNext()) {
    // A mesh contributes each of its own faces as its own entry, never
    // itself: a house-sized mesh then culls per face, at no cost to the
    // exported interface, which still sees one primitive per request.
    GMANRayPolygonMesh const* mesh = dynamic_cast<GMANRayPolygonMesh const*>(primitive);
    if (mesh) {
      for (std::size_t i = 0; i < mesh->getFaceCount(); ++i) {
        addEntry(&mesh->getFace(i));
      }
      continue;
    }

    GMANRayInterface const* rayPrimitive = dynamic_cast<GMANRayInterface const*>(primitive);
    if (!rayPrimitive) {
      continue;
    }
    addEntry(rayPrimitive);
  }

  if (primitives.empty()) {
    return;
  }
  rootIndex = buildRange(0, primitives.size(), 0);
}

GMANBBox GMANRayBVH::rangeBounds(std::size_t start, std::size_t count) const {
  GMANPoint boxMin = primitives[start].bbox.getMin();
  GMANPoint boxMax = primitives[start].bbox.getMax();
  for (std::size_t i = start + 1; i < start + count; ++i) {
    boxMin = gman::pointMin(boxMin, primitives[i].bbox.getMin());
    boxMax = gman::pointMax(boxMax, primitives[i].bbox.getMax());
  }
  return GMANBBox(boxMin, boxMax);
}

int GMANRayBVH::splitAxis(std::size_t start, std::size_t count) const {
  GMANPoint centroidMin = primitives[start].centroid;
  GMANPoint centroidMax = centroidMin;
  for (std::size_t i = start + 1; i < start + count; ++i) {
    GMANPoint const& c = primitives[i].centroid;
    centroidMin = gman::pointMin(centroidMin, c);
    centroidMax = gman::pointMax(centroidMax, c);
  }
  RtFloat const extent[3] = {centroidMax.getX() - centroidMin.getX(), centroidMax.getY() - centroidMin.getY(),
                             centroidMax.getZ() - centroidMin.getZ()};
  int axis = 0;
  if (extent[1] > extent[axis])
    axis = 1;
  if (extent[2] > extent[axis])
    axis = 2;
  return axis;
}

std::size_t GMANRayBVH::splitAtMedian(std::size_t start, std::size_t count) {
  int const axis = splitAxis(start, count);
  std::size_t const mid = start + count / 2;
  std::nth_element(
      primitives.begin() + (long)start, primitives.begin() + (long)mid, primitives.begin() + (long)(start + count),
      [axis](Entry const& a, Entry const& b) { return axisValue(a.centroid, axis) < axisValue(b.centroid, axis); });
  return mid;
}

bool GMANRayBVH::splitBySurfaceArea(std::size_t start, std::size_t count, std::size_t depth, std::size_t& mid) {
  GMANPoint centroidMin = primitives[start].centroid;
  GMANPoint centroidMax = centroidMin;
  for (std::size_t i = start + 1; i < start + count; ++i) {
    centroidMin = gman::pointMin(centroidMin, primitives[i].centroid);
    centroidMax = gman::pointMax(centroidMax, primitives[i].centroid);
  }

  std::size_t const capacity = childCapacity(depth);
  RtFloat bestCost = std::numeric_limits<RtFloat>::infinity();
  int bestAxis = -1;
  std::size_t bestBin = 0;
  RtFloat bestLow = 0.0;
  RtFloat bestExtent = 0.0;
  for (int axis = 0; axis < 3; ++axis) {
    RtFloat const low = axisValue(centroidMin, axis);
    RtFloat const extent = axisValue(centroidMax, axis) - low;
    if (!(extent > (RtFloat)0.0) || !std::isfinite(extent)) {
      continue;
    }
    std::array<Bin, kBinCount> bins;
    for (std::size_t i = start; i < start + count; ++i) {
      bins[binOf(axisValue(primitives[i].centroid, axis), low, extent)].add(primitives[i].bbox);
    }

    // upperCost[plane] prices the bins above plane, the one between bin
    // plane and bin plane + 1.
    std::array<RtFloat, kBinCount - 1> upperCost;
    Bin upper;
    for (std::size_t plane = kBinCount - 1; plane-- > 0;) {
      upper.merge(bins[plane + 1]);
      upperCost[plane] = upper.weightedArea();
    }

    Bin lower;
    for (std::size_t plane = 0; plane + 1 < kBinCount; ++plane) {
      lower.merge(bins[plane]);
      std::size_t const upperCount = count - lower.count;
      if (lower.count == 0 || upperCount == 0 || GMANMax(lower.count, upperCount) > capacity) {
        continue;
      }
      RtFloat const cost = lower.weightedArea() + upperCost[plane];
      if (std::isfinite(cost) && cost < bestCost) {
        bestCost = cost;
        bestAxis = axis;
        bestBin = plane;
        bestLow = low;
        bestExtent = extent;
      }
    }
  }
  if (bestAxis < 0) {
    return false;
  }

  auto const middle = std::partition(
      primitives.begin() + (long)start, primitives.begin() + (long)(start + count),
      [&](Entry const& entry) { return binOf(axisValue(entry.centroid, bestAxis), bestLow, bestExtent) <= bestBin; });
  mid = (std::size_t)(middle - primitives.begin());
  return true;
}

int GMANRayBVH::buildRange(std::size_t start, std::size_t count, std::size_t depth) {
  Node node;
  node.setBounds(rangeBounds(start, count));

  if (count <= kLeafSize) {
    node.leaf = true;
    node.primStart = start;
    node.primCount = count;
    deepestLeaf = std::max(deepestLeaf, depth);
    nodes.push_back(node);
    return (int)nodes.size() - 1;
  }

  std::size_t mid = 0;
  if (!splitBySurfaceArea(start, count, depth, mid)) {
    mid = splitAtMedian(start, count);
  }

  int const left = buildRange(start, mid - start, depth + 1);
  int const right = buildRange(mid, start + count - mid, depth + 1);

  node.leaf = false;
  node.left = left;
  node.right = right;
  nodes.push_back(node);
  return (int)nodes.size() - 1;
}

void GMANRayBVH::testLeaf(Node const& node, GMANRay const& ray, Search& search) const {
  for (std::size_t i = node.primStart; i < node.primStart + node.primCount; ++i) {
    Entry const& e = primitives[i];
    GMANHit candidate;
    if (search.primitiveTests) {
      ++*search.primitiveTests;
    }
    if (!e.primitive->intersect(ray, candidate)) {
      continue;
    }
    bool const better = !search.found || candidate.t < search.hit.t ||
                        (candidate.t == search.hit.t && e.insertionIndex < search.bestInsertionIndex);
    if (better) {
      search.hit = candidate;
      search.hitPrimitive = e.primitive;
      search.bestInsertionIndex = e.insertionIndex;
      search.found = true;
    }
  }
}

void GMANRayBVH::pushChildren(Node const& node, PreparedRay const& ray, Search const& search,
                              std::array<StackEntry, kMaxStackCapacity>& stack, std::size_t& stackSize) const {
  RtFloat leftEntry = 0.0, rightEntry = 0.0;
  bool const leftHit = search.entersBox(ray, nodes[(std::size_t)node.left], leftEntry);
  bool const rightHit = search.entersBox(ray, nodes[(std::size_t)node.right], rightEntry);

  // The nearer child -- by actual box-entry distance, not tree
  // structure -- goes on top of the (LIFO) stack, so it pops, and so is
  // visited, first.
  if (leftHit && rightHit) {
    if (leftEntry <= rightEntry) {
      stack[stackSize++] = {node.right, rightEntry};
      stack[stackSize++] = {node.left, leftEntry};
    } else {
      stack[stackSize++] = {node.left, leftEntry};
      stack[stackSize++] = {node.right, rightEntry};
    }
  } else if (leftHit) {
    stack[stackSize++] = {node.left, leftEntry};
  } else if (rightHit) {
    stack[stackSize++] = {node.right, rightEntry};
  }
}

bool GMANRayBVH::nearestHit(GMANRay const& ray, GMANHit& hit, GMANRayInterface const*& hitPrimitive,
                            std::size_t* primitiveTests) const {
  return nearestHit(ray, hit, hitPrimitive, primitiveTests, nullptr);
}

bool GMANRayBVH::nearestHit(GMANRay const& ray, GMANHit& hit, GMANRayInterface const*& hitPrimitive,
                            std::size_t* primitiveTests, std::size_t* boxTests) const {
  if (rootIndex < 0) {
    return false;
  }

  PreparedRay const prepared(ray);
  Search search;
  search.primitiveTests = primitiveTests;
  search.boxTests = boxTests;

  RtFloat rootEntry = 0.0;
  if (!search.entersBox(prepared, nodes[(std::size_t)rootIndex], rootEntry)) {
    return false;
  }

  // A local, fixed-capacity stack: nearestHit's whole traversal state
  // lives here, so two calls against the same tree never interfere --
  // re-entrant, with no shared mutable state of its own, and no heap
  // allocation. kMaxStackCapacity's own comment proves no tree build()
  // makes ever needs more room than this holds.
  std::array<StackEntry, kMaxStackCapacity> stack;
  std::size_t stackSize = 0;
  stack[stackSize++] = {rootIndex, rootEntry};

  while (stackSize > 0) {
    StackEntry const pending = stack[--stackSize];
    if (pending.entry > search.limit(prepared)) {
      continue;
    }

    Node const& node = nodes[(std::size_t)pending.node];
    if (node.leaf) {
      testLeaf(node, ray, search);
    } else {
      pushChildren(node, prepared, search, stack, stackSize);
    }
  }

  if (search.found) {
    hit = search.hit;
    hitPrimitive = search.hitPrimitive;
  }
  return search.found;
}
