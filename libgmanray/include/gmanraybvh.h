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

#pragma once

#include <array>
#include <cstddef>
#include <limits>
#include <vector>

#include "gman/rayexport.h"
#include "gmanbbox.h"
#include "gmanpoint.h"
#include "gmanray.h"
#include "gmanrayinterface.h"
#include "gmanworldmanager.h"

/*
 * A bounding volume hierarchy over every GMANRayInterface worldManager
 * holds, so nearestHit finds the nearest ray-primitive hit by pruning
 * subtrees a ray's own interval clears, rather than testing every
 * primitive in turn. build() discards any tree a prior call built and
 * walks worldManager once, splitting each range of primitives by binned
 * surface-area heuristic over their own bboxes: 16 bins over the range's
 * centroid extent on each axis, the cheapest split by box area times
 * primitive count winning, the lower axis then the lower bin on a tie. A
 * range with no centroid extent, or no split of finite cost, splits at the
 * object median instead, and no tree grows deeper than the traversal stack
 * holds. nearestHit then traverses the tree it built, touching only
 * that tree and a local stack -- const, re-entrant, no shared mutable
 * state of its own, the same contract GMANRayInterface::intersect and
 * GMANRayOccluder::transmission keep, which is what lets a caller invoke
 * any of the three from concurrent gman::parallelFor workers. GMANRAY_EXPORT,
 * along with GMANRayOccluder: the path tracer's own API.
 */
class GMANRAY_EXPORT GMANRayBVH {
public:
  // Discards any tree a prior call built, then walks worldManager
  // (getFirst/getNext) once, keeping each GMANRayInterface primitive by
  // its own insertion order for nearestHit's tie-break -- except a
  // polygon mesh primitive, which contributes each of its own faces as
  // its own entry instead of the mesh itself, so a mesh culls per face.
  // The dynamic_cast is defensive -- GMANRayObjectManager's factories
  // guarantee every primitive this renderer's world manager holds is
  // already a GMANRayInterface, the polygon mesh type included.
  void build(GMANWorldManager& worldManager);

  // Finds the nearest hit within ray's own [tmin, tmax]. On a hit,
  // writes it through hit/hitPrimitive and returns true; on a miss,
  // leaves both untouched and returns false. On an exact t tie between
  // two candidates, the earlier-inserted primitive wins, regardless of
  // traversal order. primitiveTests, when non-null, is incremented once
  // per primitive-level intersect() call this traversal makes (hit or
  // miss) -- an out-parameter rather than a member, so two calls stay
  // re-entrant.
  bool nearestHit(GMANRay const& ray, GMANHit& hit, GMANRayInterface const*& hitPrimitive,
                  std::size_t* primitiveTests = nullptr) const;

  // The same search, also incrementing boxTests, when non-null, once per
  // node-box test the traversal makes, the root's included.
  bool nearestHit(GMANRay const& ray, GMANHit& hit, GMANRayInterface const*& hitPrimitive, std::size_t* primitiveTests,
                  std::size_t* boxTests) const;

  // The depth of the deepest leaf, the root at 0; an empty tree reports 0.
  // Always below the bit width of std::size_t.
  std::size_t treeDepth() const { return deepestLeaf; }

private:
  // One primitive, its own camera-space bbox and centroid cached at
  // build() time, and the order it was added in.
  struct Entry {
    GMANRayInterface const* primitive = nullptr;
    GMANBBox bbox;
    GMANPoint centroid;
    std::size_t insertionIndex = 0;
  };

  // A leaf (leaf true) holds [primStart, primStart + primCount) of
  // primitives, tested directly against intersect() with no further box
  // test; an internal node (leaf false) holds two child indices into
  // nodes. bbox is always the union of what the node holds.
  struct Node {
    GMANBBox bbox;
    std::array<RtFloat, 3> lo = {};
    std::array<RtFloat, 3> hi = {};
    bool leaf = false;
    int left = -1;
    int right = -1;
    std::size_t primStart = 0;
    std::size_t primCount = 0;

    void setBounds(GMANBBox const& bounds);
  };

  struct PreparedRay {
    std::array<RtFloat, 3> origin;
    std::array<RtFloat, 3> inverseDirection;
    std::array<bool, 3> parallel;
    RtFloat tMin;
    RtFloat tMax;

    explicit PreparedRay(GMANRay const& ray);

    // The ray's slab test against node's box, clamped to [tMin, limit]:
    // entry narrows to the box's own intersection with that interval, and
    // the box overlaps it exactly when entry <= exit. A parallel axis skips
    // the division (1.0/-0.0 is -infinity, which would fold an origin
    // exactly on that axis's near face into the wrong sign) and instead
    // tests the origin's fixed coordinate against [lo, hi] directly: outside
    // it, the box is missed regardless of the other axes; inside it, the
    // axis imposes no constraint.
    bool overlaps(Node const& node, RtFloat limit, RtFloat& entry) const;
  };

  struct StackEntry {
    int node = -1;
    RtFloat entry = 0.0;
  };

  // nearestHit's own running state, threaded through testLeaf and
  // pushChildren rather than held on the class: a local to each call, so
  // two calls against the same tree never share it.
  struct Search {
    bool found = false;
    GMANHit hit;
    GMANRayInterface const* hitPrimitive = nullptr;
    std::size_t bestInsertionIndex = 0;
    std::size_t* primitiveTests = nullptr;
    std::size_t* boxTests = nullptr;

    RtFloat limit(PreparedRay const& ray) const;
    bool entersBox(PreparedRay const& ray, Node const& node, RtFloat& entry) const;
  };

  // Every leaf holds at most this many primitives -- pinned, not left to
  // construction's discretion, so a visited leaf's own intersect() count
  // is exact rather than approximate.
  static constexpr std::size_t kLeafSize = 4;

  // No leaf lies deeper than this: buildRange lets a split take a child
  // only when the child holds few enough primitives for a median split to
  // finish within the levels left (see childCapacity), so a depth-d node
  // never holds more than kLeafSize << (kMaxTreeDepth - d) primitives, and
  // the root, with at most what a std::size_t holds, fits.
  static constexpr std::size_t kMaxTreeDepth = std::numeric_limits<std::size_t>::digits - 1;

  // nearestHit's own local traversal stack never holds more entries than
  // this. A node at depth d is popped with at most one pending sibling
  // per level above it, d in all, and pushes two children, so the stack
  // peaks at d + 2 entries. An internal node lies above a leaf, so d is
  // at most kMaxTreeDepth - 1 = digits - 2, and the stack never needs more
  // than digits entries; the capacity of digits + 1 leaves one to spare.
  static constexpr std::size_t kMaxStackCapacity = std::numeric_limits<std::size_t>::digits + 1;

  std::vector<Entry> primitives;
  std::vector<Node> nodes;
  int rootIndex = -1;
  std::size_t deepestLeaf = 0;

  // Builds the subtree over primitives[start, start + count), reordering
  // that range in place around the split splitBySurfaceArea or, failing
  // that, splitAtMedian picks, and returns its node's index in nodes. depth
  // is the subtree root's depth below the tree's root.
  int buildRange(std::size_t start, std::size_t count, std::size_t depth);

  // The most primitives a child of a node at depth may hold and still
  // finish, split at medians, within kMaxTreeDepth levels.
  static std::size_t childCapacity(std::size_t depth);

  // Partitions primitives[start, start + count) around the cheapest binned
  // surface-area split whose children both fit childCapacity(depth), and
  // writes the first index of the upper half to mid. Returns false, leaving
  // mid alone, when no axis has centroid extent or no split has finite cost.
  bool splitBySurfaceArea(std::size_t start, std::size_t count, std::size_t depth, std::size_t& mid);

  // Partitions primitives[start, start + count) at the object median along
  // splitAxis and returns the first index of the upper half.
  std::size_t splitAtMedian(std::size_t start, std::size_t count);

  // The union of primitives[start, start + count)'s own boxes.
  GMANBBox rangeBounds(std::size_t start, std::size_t count) const;

  // The axis along which primitives[start, start + count)'s own
  // centroids have the greatest extent -- splitAtMedian's split axis.
  int splitAxis(std::size_t start, std::size_t count) const;

  // Tests every primitive node (a leaf) holds against ray, updating
  // search with the nearest (ties keeping the earlier-inserted
  // primitive) and counting each intersect() call.
  void testLeaf(Node const& node, GMANRay const& ray, Search& search) const;

  // Tests node's two children against ray (clamped to search's own best
  // hit so far) and pushes whichever overlap onto stack (stack[0,
  // stackSize) already held, stackSize entries) with their box-entry
  // distances, farther-entry child first so the nearer one -- by actual
  // box-entry distance, not tree structure -- pops, and so is visited,
  // first. Writes past stackSize with no bounds check: kMaxStackCapacity's
  // own comment proves a write here never reaches the array's end.
  void pushChildren(Node const& node, PreparedRay const& ray, Search const& search,
                    std::array<StackEntry, kMaxStackCapacity>& stack, std::size_t& stackSize) const;
};
