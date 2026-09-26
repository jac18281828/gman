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
class GMANRayInterface;
class GMANRayPolygon;
class GMANRayPolygonMesh;

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

// surfacePoint()'s own answer, camera space: the true-surface point P,
// its unit normal N, sign-agreed with the element's normal, and the area
// density dA / (ds dt) across the element's grid cell.
struct GMANRadiositySurfacePoint {
  GMANPoint P;
  GMANVector N;
  RtFloat density = 0;
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
 * primitive that is one of the seven quadric GMANRayInterface classes, a
 * GMANRayPolygon, or a GMANRayPolygonMesh. A mesh dices face by face,
 * through its own getFaceCount()/getFace(); each face is keyed for
 * locate() by its own address, the same address a GMANRayBVH hit reports,
 * so the mesh primitive itself is never a lookup key. Anything else -- an
 * unsupported primitive, or a quadric whose object-to-camera placement
 * will not invert -- is skipped and counted (getSkippedCount()), never
 * silently dropped. A degenerate polygon or mesh face is not skipped; it
 * dices to zero elements, since its own intersect() never hits it either.
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
 * every cell clipped against the outer loop and, for a GeneralPolygon,
 * against each hole in turn -- a cell's own area is its clipped outer
 * area minus its clipped hole areas, and a cell whose net area falls at
 * or under the dicer's own minimum is dropped, the fate a cell entirely
 * outside the outer loop already meets. The kept elements sum to the
 * outer loop's own area minus its holes', up to rounding; holes are
 * assumed inside the outer loop and mutually non-overlapping
 * (RiGeneralPolygon's own contract), so no union is computed. A clipped
 * cell keeps its own four unclipped corner nodes regardless, each flagged
 * inside only when it falls inside the outer loop and outside every hole,
 * so it still has four to interpolate between.
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
 * (hit.u, hit.v) clamped into the grid; for a polygon or mesh-face hit,
 * from the hit point's own in-plane coordinates. It returns false for a
 * primitive the mesh does not hold.
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

  // Primitives build() walked but could not dice: not one of the nine
  // supported classes, or a quadric singular under its own placement.
  std::size_t getSkippedCount() const { return skippedCount; }

  // Primitives build() diced with nu or nv pinned at kMaxDivisions,
  // rather than the count maxEdgeLength itself would call for: each face
  // of a GMANRayPolygonMesh counts as its own primitive here.
  std::size_t getCappedCount() const { return cappedCount; }

  bool locate(GMANHit const& hit, GMANRadiosityLocation& location) const;

  // The ray primitive element was diced from: for a GMANRayPolygonMesh,
  // the face, the pointer a GMANRayBVH hit reports.
  GMANRayInterface const* getElementPrimitive(std::size_t element) const;

  // The true surface at (s, t) in [0, 1]^2 across element's grid cell.
  // A quadric's point and normal are its own getLocation/getNormal at the
  // cell's (u, v), through its placement and the placement's inverse
  // transpose, and its density is |dP/ds x dP/dt| by central difference.
  // A polygon's point is the in-plane point, its normal the plane normal
  // and its density the full cell's area; false, with point untouched,
  // where that point lies outside the outer loop or inside a hole.
  bool surfacePoint(std::size_t element, double s, double t, GMANRadiositySurfacePoint& point) const;

  // True when nodes a and b belong to the same primitive (for a
  // GMANRayPolygonMesh, the same face) and coincide within 1e-6 of its
  // gman::primitiveMagnitude: a closed quadric's u seam and a pole's row.
  // Groups are that relation's transitive closure, fixed at build().
  bool sameNode(std::size_t a, std::size_t b) const;

private:
  struct PrimitiveMesh;

  // Where an element came from: its primitiveMeshes index and grid cell.
  struct ElementCell;

  // Records each of the latest primitive's own elements' cells, read from
  // its cellToElement.
  void recordElementCells();

  // Joins each coincident node pair of the latest primitive into one
  // group in nodeGroups.
  void groupCoincidentNodes();

  // Common tail of diceParametric and dicePolygon: recordElementCells()
  // then groupCoincidentNodes() over the primitive just appended.
  void finishPrimitiveMesh();

  // The quad-facet element over four corner node indices, already
  // appended to nodes. A pole cell -- two adjacent corners coincident --
  // degrades one of its two triangles to zero area on its own; see the
  // .cpp for why that needs no separate case.
  GMANRadiosityElement buildFacetElement(std::array<std::size_t, 4> const& corners) const;

  // Dices primitive, already known to be one of the nine diceable
  // classes; returns false on the one further way it can still fail (a
  // quadric whose placement will not invert).
  bool diceOne(GMANPrimitive* primitive, RtFloat maxEdgeLength);

  void diceParametric(GMANPrimitive* primitive, GMANParametric& parametric, GMANMatrix4 const& objectToCamera,
                      GMANMatrix4 const& cameraToObject, RtFloat maxEdgeLength);

  // Appends a parametric primitive's own nu x nv facet elements over its
  // freshly appended node grid, recording each cell's index in
  // cellToElement.
  void appendParametricElements(std::size_t nu, std::size_t nv, std::size_t nodeOffset,
                                std::vector<std::size_t>& cellToElement);

  void dicePolygon(GMANRayPolygon const& polygon, RtFloat maxEdgeLength);

  // Fills mesh with a polygon primitive's own basis, grid and per-cell
  // bookkeeping, ready for appendPolygonElements and finishPrimitiveMesh.
  void fillPolygonPrimitiveMesh(GMANRayPolygon const& polygon, GMANPoint const& basisOrigin, GMANVector const& basisE0,
                                GMANVector const& basisE1, GMANVector const& normal, double uMin, double vMin,
                                std::size_t nu, std::size_t nv, double duStep, double dvStep, std::size_t nodeOffset,
                                std::size_t elementOffset, PrimitiveMesh& mesh) const;

  // Dices each of mesh's own faces as its own GMANRayPolygon, keyed for
  // locate() by that face's own address, never the mesh's.
  void diceMesh(GMANRayPolygonMesh const& mesh, RtFloat maxEdgeLength);

  // The polygon branch of surfacePoint: the in-plane point and plane
  // normal, false when the point falls outside the outer loop or inside a
  // hole.
  bool polygonSurfacePoint(PrimitiveMesh const& mesh, ElementCell const& cell, double s, double t,
                           GMANRadiositySurfacePoint& point) const;

  // The quadric branch of surfacePoint: point and normal from
  // getLocation/getNormal, density by central difference.
  GMANRadiositySurfacePoint quadricSurfacePoint(PrimitiveMesh const& mesh, ElementCell const& cell, double s,
                                                double t) const;

  std::vector<GMANRadiosityNode> nodes;
  std::vector<GMANRadiosityElement> elements;
  std::vector<PrimitiveMesh> primitiveMeshes;
  std::vector<ElementCell> elementCells;

  // Per node, the lowest node index of its sameNode group.
  std::vector<std::size_t> nodeGroups;
  std::size_t skippedCount = 0;
  std::size_t cappedCount = 0;
};
