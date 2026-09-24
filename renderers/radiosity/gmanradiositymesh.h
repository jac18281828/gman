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
#include <vector>

#include "gmanpoint.h"
#include "gmanray.h"
#include "gmanvector.h"
#include "gmanworldmanager.h"

class GMANParametric;
class GMANRayPolygon;

// One grid corner, shared by up to four elements. position and normal are
// camera space. inside is false only for a polygon grid node clipping
// found outside the polygon -- kept anyway so its cell's other corners
// still have four to interpolate between.
struct GMANRadiosityNode {
  GMANPoint position;
  GMANVector normal;
  bool inside = true;
};

// One grid cell: a facet over its four corner nodes -- two triangles, one
// at a pole cell where a corner pair coincides, so its own area is the
// other triangle's alone. corners follow the grid's own (u, v) winding:
// (i, j), (i+1, j), (i+1, j+1), (i, j+1).
struct GMANRadiosityElement {
  RtFloat area = 0;
  GMANPoint centre;
  GMANVector normal;
  std::array<std::size_t, 4> corners{};
};

// locate()'s own answer: an element and, in the same corner order as
// GMANRadiosityElement::corners, the four bilinear weights (summing to 1)
// that reproduce the hit point from its four corner positions.
struct GMANRadiosityLocation {
  std::size_t element = 0;
  std::array<std::size_t, 4> corners{};
  std::array<RtFloat, 4> weights{};
};

/*
 * Dices every ray primitive a world manager holds into a camera-space
 * element mesh for a radiosity solver, and locates the element and corner
 * weights under any hit against one. Not GMAN_EXPORT: a test compiles
 * this object library's own sources directly rather than linking an
 * installed one, GMANRayTracer's own reason (renderers/raytracer/
 * gmanraytracerenderer.h).
 *
 * build() walks worldManager once (getFirst()/getNext()), dicing each
 * primitive that is one of the seven quadric GMANRayInterface classes or a
 * GMANRayPolygon. Anything else -- an unsupported primitive, or a quadric
 * whose object-to-camera placement will not invert -- is skipped and
 * counted (getSkippedCount()), never silently dropped. A degenerate
 * polygon is not skipped; it dices to zero elements, since its own
 * intersect() never hits it either.
 *
 * Each diced primitive gets an (nu + 1) x (nv + 1) node grid and nu x nv
 * elements. nu and nv are the smallest counts, capped at kMaxDivisions
 * each, that keep every element edge at or under build()'s own
 * maxEdgeLength, measured between camera-space corner nodes; an edge
 * collapsing to a point (a pole) is exempt. A quadric's nodes come from
 * GMANParametric::getLocation(u, v) through its own getObjectToCamera(),
 * u and v spanning [0, 1]; its node normals come from getNormal() through
 * the placement's inverse transpose (AGENTS.md's own normal-transform
 * rule), normalized. A polygon dices over an orthonormal in-plane basis
 * (e0 along its first non-degenerate edge, e1 = normal x e0) instead,
 * every cell clipped against the polygon -- a cell entirely outside is
 * dropped, and a surviving cell's own area is its clipped area, so the
 * kept elements sum to the polygon's own area up to rounding. A clipped
 * cell keeps its own four unclipped corner nodes regardless (flagging one
 * outside if clipping found it so), so it still has four to interpolate
 * between.
 *
 * An element's own area is the sum of its two triangles' own areas: a
 * facet quantity, so its error against a curved primitive's true area
 * falls at second order in the edge length. Its normal is the unit
 * direction of the triangle pair's own area vector, sign-agreed with the
 * average of its corners' normals; its centre is the pair's
 * area-weighted centroid.
 *
 * locate() is const and touches no shared mutable state, so a solver and
 * the ray tracer's own shading can call it from concurrent
 * gman::parallelFor workers. For a quadric hit, the element comes from
 * (hit.u, hit.v) clamped into the grid; for a polygon hit, from the hit
 * point's own in-plane coordinates. It returns false for a primitive the
 * mesh does not hold.
 */
class GMANRadiosityMesh {
public:
  // A bound on memory for a huge primitive, not a resolution guarantee: nu
  // and nv each stop growing here even if maxEdgeLength is not yet met.
  static constexpr std::size_t kMaxDivisions = 256;

  GMANRadiosityMesh();
  ~GMANRadiosityMesh();
  GMANRadiosityMesh(GMANRadiosityMesh const&) = delete;
  GMANRadiosityMesh& operator=(GMANRadiosityMesh const&) = delete;
  GMANRadiosityMesh(GMANRadiosityMesh&&) noexcept;
  GMANRadiosityMesh& operator=(GMANRadiosityMesh&&) noexcept;

  void build(GMANWorldManager& worldManager, RtFloat maxEdgeLength);

  std::size_t getNodeCount() const { return nodes.size(); }
  std::size_t getElementCount() const { return elements.size(); }
  GMANRadiosityNode const& getNode(std::size_t index) const { return nodes[index]; }
  GMANRadiosityElement const& getElement(std::size_t index) const { return elements[index]; }

  // Primitives build() walked but could not dice: not one of the eight
  // supported classes, or a quadric singular under its own placement.
  std::size_t getSkippedCount() const { return skippedCount; }

  bool locate(GMANHit const& hit, GMANRadiosityLocation& location) const;

private:
  struct PrimitiveMesh;

  // The quad-facet element over four corner node indices, already
  // appended to nodes. A pole cell -- two adjacent corners coincident --
  // degrades one of its two triangles to zero area on its own; see the
  // .cpp for why that needs no separate case.
  GMANRadiosityElement buildFacetElement(std::array<std::size_t, 4> const& corners) const;

  // Dices primitive, already known to be one of the eight diceable
  // classes; returns false on the one further way it can still fail (a
  // quadric whose placement will not invert).
  bool diceOne(GMANPrimitive* primitive, RtFloat maxEdgeLength);

  void diceParametric(GMANPrimitive* primitive, GMANParametric& parametric, GMANMatrix4 const& objectToCamera,
                      GMANMatrix4 const& cameraToObject, RtFloat maxEdgeLength);

  void dicePolygon(GMANRayPolygon& polygon, RtFloat maxEdgeLength);

  std::vector<GMANRadiosityNode> nodes;
  std::vector<GMANRadiosityElement> elements;
  std::vector<PrimitiveMesh> primitiveMeshes;
  std::size_t skippedCount = 0;
};
