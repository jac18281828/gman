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

#include "gmanmath.h"
#include "gmanraybbox.h"
#include "gmanraybvh.h"

namespace {

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

// The ray's own ray-box slab test, clamped to [tMinLimit, tMaxLimit]:
// entry/exit narrow to the box's own intersection with that interval, and
// the box overlaps it exactly when entry <= exit on return. A direction
// component of exactly 0 -- +0.0 and -0.0 alike -- skips the division
// (1.0/-0.0 is -infinity, which would fold an origin exactly on that
// axis's near face into the wrong sign and reject a ray intersect()
// would accept) and instead tests the ray's fixed coordinate on that
// axis against [lo, hi] directly: outside it, the box is missed
// regardless of the other axes; inside it (the on-a-face-plane case
// included), the axis imposes no constraint, the same conservative
// outcome the settled NaN-counts-as-overlap rule names, reached here
// without ever producing a NaN.
bool slabIntersect(GMANBBox const& box, GMANRay const& ray, RtFloat tMinLimit, RtFloat tMaxLimit, RtFloat& entryOut,
                   RtFloat& exitOut) {
  GMANPoint const lo = box.getMin();
  GMANPoint const hi = box.getMax();
  GMANPoint const& origin = ray.getOrigin();
  GMANVector const& direction = ray.getDirection();

  RtFloat const originArr[3] = {origin.getX(), origin.getY(), origin.getZ()};
  RtFloat const dirArr[3] = {direction.getX(), direction.getY(), direction.getZ()};
  RtFloat const loArr[3] = {lo.getX(), lo.getY(), lo.getZ()};
  RtFloat const hiArr[3] = {hi.getX(), hi.getY(), hi.getZ()};

  RtFloat entry = tMinLimit;
  RtFloat exit = tMaxLimit;
  for (int axis = 0; axis < 3; ++axis) {
    if (dirArr[axis] == (RtFloat)0.0) {
      if (originArr[axis] < loArr[axis] || originArr[axis] > hiArr[axis]) {
        entryOut = entry;
        exitOut = exit;
        return false;
      }
      continue;
    }
    RtFloat const invD = (RtFloat)1.0 / dirArr[axis];
    RtFloat t0 = (loArr[axis] - originArr[axis]) * invD;
    RtFloat t1 = (hiArr[axis] - originArr[axis]) * invD;
    if (t0 > t1)
      std::swap(t0, t1);
    entry = GMANMax(entry, t0);
    exit = GMANMin(exit, t1);
  }
  entryOut = entry;
  exitOut = exit;
  return entry <= exit;
}

} // namespace

void GMANRayBVH::build(GMANWorldManager& worldManager) {
  primitives.clear();
  nodes.clear();
  rootIndex = -1;

  std::size_t index = 0;
  for (GMANPrimitive* primitive = worldManager.getFirst(); primitive; primitive = worldManager.getNext()) {
    GMANRayInterface const* rayPrimitive = dynamic_cast<GMANRayInterface const*>(primitive);
    if (!rayPrimitive) {
      continue;
    }
    Entry entry;
    entry.primitive = rayPrimitive;
    entry.bbox = rayPrimitive->getBBox();
    entry.centroid = boxCentroid(entry.bbox);
    entry.insertionIndex = index++;
    primitives.push_back(entry);
  }

  if (primitives.empty()) {
    return;
  }
  rootIndex = buildRange(0, primitives.size());
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

int GMANRayBVH::buildRange(std::size_t start, std::size_t count) {
  Node node;
  node.bbox = rangeBounds(start, count);

  if (count <= kLeafSize) {
    node.leaf = true;
    node.primStart = start;
    node.primCount = count;
    nodes.push_back(node);
    return (int)nodes.size() - 1;
  }

  // The split axis is the one along which this range's own centroids have
  // the greatest extent -- not the box's spatial midpoint, the object
  // median along that axis (std::nth_element's own partition).
  int const axis = splitAxis(start, count);
  auto centroidOnAxis = [axis](GMANPoint const& p) { return axis == 0 ? p.getX() : axis == 1 ? p.getY() : p.getZ(); };

  std::size_t const mid = start + count / 2;
  std::nth_element(primitives.begin() + (long)start, primitives.begin() + (long)mid,
                   primitives.begin() + (long)(start + count), [&centroidOnAxis](Entry const& a, Entry const& b) {
                     return centroidOnAxis(a.centroid) < centroidOnAxis(b.centroid);
                   });

  int const left = buildRange(start, mid - start);
  int const right = buildRange(mid, start + count - mid);

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

void GMANRayBVH::pushChildren(Node const& node, GMANRay const& ray, Search const& search,
                              std::vector<int>& stack) const {
  Node const& leftNode = nodes[(std::size_t)node.left];
  Node const& rightNode = nodes[(std::size_t)node.right];
  RtFloat const limit = search.found ? GMANMin(ray.getTMax(), search.hit.t) : ray.getTMax();
  RtFloat leftEntry = 0.0, leftExit = 0.0, rightEntry = 0.0, rightExit = 0.0;
  bool const leftHit = slabIntersect(leftNode.bbox, ray, ray.getTMin(), limit, leftEntry, leftExit);
  bool const rightHit = slabIntersect(rightNode.bbox, ray, ray.getTMin(), limit, rightEntry, rightExit);

  // The nearer child -- by actual box-entry distance, not tree
  // structure -- goes on top of the (LIFO) stack, so it pops, and so is
  // visited, first.
  if (leftHit && rightHit) {
    if (leftEntry <= rightEntry) {
      stack.push_back(node.right);
      stack.push_back(node.left);
    } else {
      stack.push_back(node.left);
      stack.push_back(node.right);
    }
  } else if (leftHit) {
    stack.push_back(node.left);
  } else if (rightHit) {
    stack.push_back(node.right);
  }
}

bool GMANRayBVH::nearestHit(GMANRay const& ray, GMANHit& hit, GMANRayInterface const*& hitPrimitive,
                            std::size_t* primitiveTests) const {
  if (rootIndex < 0) {
    return false;
  }

  Search search;
  search.primitiveTests = primitiveTests;

  // A local stack: nearestHit's whole traversal state lives here, so two
  // calls against the same tree never interfere -- re-entrant, with no
  // shared mutable state of its own.
  std::vector<int> stack;
  stack.push_back(rootIndex);

  while (!stack.empty()) {
    int const nodeIndex = stack.back();
    stack.pop_back();
    Node const& node = nodes[(std::size_t)nodeIndex];

    RtFloat const limit = search.found ? GMANMin(ray.getTMax(), search.hit.t) : ray.getTMax();
    RtFloat entry = 0.0, exit = 0.0;
    if (!slabIntersect(node.bbox, ray, ray.getTMin(), limit, entry, exit)) {
      continue;
    }

    if (node.leaf) {
      testLeaf(node, ray, search);
    } else {
      pushChildren(node, ray, search, stack);
    }
  }

  if (search.found) {
    hit = search.hit;
    hitPrimitive = search.hitPrimitive;
  }
  return search.found;
}
