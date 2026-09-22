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

#include <cstddef>
#include <vector>

#include "gmanbbox.h"
#include "gmanpoint.h"
#include "gmanray.h"
#include "gmanrayinterface.h"
#include "gmanworldmanager.h"

/*
 * A bounding volume hierarchy over every GMANRayInterface worldManager
 * holds, replacing GMANLinearWorldManager's own shared-cursor walk.
 * build() discards any tree a prior call built and walks worldManager
 * once, an object-median split over each primitive's own bbox; nearestHit
 * then traverses the tree it built, touching only that tree and a local
 * stack -- const, re-entrant, no shared mutable state of its own. Not
 * GMAN_EXPORT: used only within gman_raytracer_objects and by its own
 * tests, which compile these sources directly rather than linking the
 * installed library (GMANRayOccluder's own precedent).
 */
class GMANRayBVH {
public:
  // Discards any tree a prior call built, then walks worldManager
  // (getFirst/getNext) once, keeping each GMANRayInterface primitive by
  // its own insertion order for nearestHit's tie-break. The dynamic_cast
  // is defensive -- GMANRayObjectManager's factories guarantee every
  // primitive this renderer's world manager holds is already one -- the
  // same guarantee the removed walkWorldManager relied on.
  void build(GMANWorldManager& worldManager);

  // Finds the nearest hit within ray's own [tmin, tmax], written through
  // hit/hitPrimitive exactly as the removed walkWorldManager's contract
  // was. On an exact t tie between two candidates, the earlier-inserted
  // primitive wins, regardless of traversal order. primitiveTests, when
  // non-null, is incremented once per primitive-level intersect() call
  // this traversal makes (hit or miss) -- an out-parameter rather than a
  // member, so two calls stay re-entrant.
  bool nearestHit(GMANRay const& ray, GMANHit& hit, GMANRayInterface const*& hitPrimitive,
                  std::size_t* primitiveTests = nullptr) const;

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
    bool leaf = false;
    int left = -1;
    int right = -1;
    std::size_t primStart = 0;
    std::size_t primCount = 0;
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
  };

  // Every leaf holds at most this many primitives -- pinned, not left to
  // construction's discretion, so a visited leaf's own intersect() count
  // is exact rather than approximate.
  static constexpr std::size_t kLeafSize = 4;

  std::vector<Entry> primitives;
  std::vector<Node> nodes;
  int rootIndex = -1;

  // Builds the subtree over primitives[start, start + count), reordering
  // that range in place (nth_element's own partition) around an
  // object-median split on the axis of greatest centroid extent, and
  // returns its node's index in nodes.
  int buildRange(std::size_t start, std::size_t count);

  // The union of primitives[start, start + count)'s own boxes.
  GMANBBox rangeBounds(std::size_t start, std::size_t count) const;

  // The axis along which primitives[start, start + count)'s own
  // centroids have the greatest extent -- buildRange's split axis.
  int splitAxis(std::size_t start, std::size_t count) const;

  // Tests every primitive node (a leaf) holds against ray, updating
  // search with the nearest (ties keeping the earlier-inserted
  // primitive) and counting each intersect() call.
  void testLeaf(Node const& node, GMANRay const& ray, Search& search) const;

  // Tests node's two children against ray (clamped to search's own best
  // hit so far) and pushes whichever overlap onto stack, farther-entry
  // child first so the nearer one -- by actual box-entry distance, not
  // tree structure -- pops, and so is visited, first.
  void pushChildren(Node const& node, GMANRay const& ray, Search const& search, std::vector<int>& stack) const;
};
