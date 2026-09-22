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
// the box overlaps it exactly when entry <= exit on return. An
// axis-aligned ray whose origin lies exactly on a box-face plane produces
// 0 * inf (NaN) on that axis's t0/t1; treated as no constraint there
// (skipped rather than folded into entry/exit) rather than a rejection,
// so this test never rejects a ray intersect() would accept.
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
    RtFloat const invD = (RtFloat)1.0 / dirArr[axis];
    RtFloat t0 = (loArr[axis] - originArr[axis]) * invD;
    RtFloat t1 = (hiArr[axis] - originArr[axis]) * invD;
    if (t0 > t1)
      std::swap(t0, t1);
    if (!std::isnan(t0))
      entry = GMANMax(entry, t0);
    if (!std::isnan(t1))
      exit = GMANMin(exit, t1);
  }
  entryOut = entry;
  exitOut = exit;
  return entry <= exit;
}

} // namespace

void GMANRayBVH::build(GMANWorldManager& worldManager) {
  primitives_.clear();
  nodes_.clear();
  rootIndex_ = -1;

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
    primitives_.push_back(entry);
  }

  if (primitives_.empty()) {
    return;
  }
  rootIndex_ = buildRange(0, primitives_.size());
}

int GMANRayBVH::buildRange(std::size_t start, std::size_t count) {
  GMANPoint boxMin = primitives_[start].bbox.getMin();
  GMANPoint boxMax = primitives_[start].bbox.getMax();
  for (std::size_t i = start + 1; i < start + count; ++i) {
    GMANPoint const pMin = primitives_[i].bbox.getMin();
    GMANPoint const pMax = primitives_[i].bbox.getMax();
    boxMin = GMANPoint(GMANMin(boxMin.getX(), pMin.getX()), GMANMin(boxMin.getY(), pMin.getY()),
                       GMANMin(boxMin.getZ(), pMin.getZ()));
    boxMax = GMANPoint(GMANMax(boxMax.getX(), pMax.getX()), GMANMax(boxMax.getY(), pMax.getY()),
                       GMANMax(boxMax.getZ(), pMax.getZ()));
  }

  Node node;
  node.bbox = GMANBBox(boxMin, boxMax);

  if (count <= kLeafSize) {
    node.leaf = true;
    node.primStart = start;
    node.primCount = count;
    nodes_.push_back(node);
    return (int)nodes_.size() - 1;
  }

  // The split axis is the one along which this range's own centroids have
  // the greatest extent -- not the box's spatial midpoint, the object
  // median along that axis (std::nth_element's own partition).
  GMANPoint centroidMin = primitives_[start].centroid;
  GMANPoint centroidMax = centroidMin;
  for (std::size_t i = start + 1; i < start + count; ++i) {
    GMANPoint const& c = primitives_[i].centroid;
    centroidMin = GMANPoint(GMANMin(centroidMin.getX(), c.getX()), GMANMin(centroidMin.getY(), c.getY()),
                            GMANMin(centroidMin.getZ(), c.getZ()));
    centroidMax = GMANPoint(GMANMax(centroidMax.getX(), c.getX()), GMANMax(centroidMax.getY(), c.getY()),
                            GMANMax(centroidMax.getZ(), c.getZ()));
  }
  RtFloat const extent[3] = {centroidMax.getX() - centroidMin.getX(), centroidMax.getY() - centroidMin.getY(),
                             centroidMax.getZ() - centroidMin.getZ()};
  int axis = 0;
  if (extent[1] > extent[axis])
    axis = 1;
  if (extent[2] > extent[axis])
    axis = 2;

  auto centroidOnAxis = [axis](GMANPoint const& p) { return axis == 0 ? p.getX() : axis == 1 ? p.getY() : p.getZ(); };

  std::size_t const mid = start + count / 2;
  std::nth_element(primitives_.begin() + (long)start, primitives_.begin() + (long)mid,
                   primitives_.begin() + (long)(start + count), [&centroidOnAxis](Entry const& a, Entry const& b) {
                     return centroidOnAxis(a.centroid) < centroidOnAxis(b.centroid);
                   });

  int const left = buildRange(start, mid - start);
  int const right = buildRange(mid, start + count - mid);

  node.leaf = false;
  node.left = left;
  node.right = right;
  nodes_.push_back(node);
  return (int)nodes_.size() - 1;
}

bool GMANRayBVH::nearestHit(GMANRay const& ray, GMANHit& hit, GMANRayInterface const*& hitPrimitive,
                            std::size_t* primitiveTests) const {
  hitPrimitive = nullptr;
  if (rootIndex_ < 0) {
    return false;
  }

  bool found = false;
  std::size_t bestInsertionIndex = 0;

  // A local stack: nearestHit's whole traversal state lives here, so two
  // calls against the same tree never interfere -- re-entrant, unlike the
  // removed walkWorldManager's shared cursor.
  std::vector<int> stack;
  stack.push_back(rootIndex_);

  while (!stack.empty()) {
    int const nodeIndex = stack.back();
    stack.pop_back();
    Node const& node = nodes_[(std::size_t)nodeIndex];

    RtFloat const limit = found ? GMANMin(ray.getTMax(), hit.t) : ray.getTMax();
    RtFloat entry = 0.0, exit = 0.0;
    if (!slabIntersect(node.bbox, ray, ray.getTMin(), limit, entry, exit)) {
      continue;
    }

    if (node.leaf) {
      for (std::size_t i = node.primStart; i < node.primStart + node.primCount; ++i) {
        Entry const& e = primitives_[i];
        GMANHit candidate;
        if (primitiveTests) {
          ++*primitiveTests;
        }
        if (!e.primitive->intersect(ray, candidate)) {
          continue;
        }
        bool const better =
            !found || candidate.t < hit.t || (candidate.t == hit.t && e.insertionIndex < bestInsertionIndex);
        if (better) {
          hit = candidate;
          hitPrimitive = e.primitive;
          bestInsertionIndex = e.insertionIndex;
          found = true;
        }
      }
      continue;
    }

    Node const& leftNode = nodes_[(std::size_t)node.left];
    Node const& rightNode = nodes_[(std::size_t)node.right];
    RtFloat const childLimit = found ? GMANMin(ray.getTMax(), hit.t) : ray.getTMax();
    RtFloat leftEntry = 0.0, leftExit = 0.0, rightEntry = 0.0, rightExit = 0.0;
    bool const leftHit = slabIntersect(leftNode.bbox, ray, ray.getTMin(), childLimit, leftEntry, leftExit);
    bool const rightHit = slabIntersect(rightNode.bbox, ray, ray.getTMin(), childLimit, rightEntry, rightExit);

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

  return found;
}
