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
#include <optional>
#include <span>
#include <vector>

#include "gmanparameterlist.h"
#include "ri.h"

class GMANPolygonMesh;

// The four factory functions, one per polygon request: the only functions
// that can build a GMANPolygonMesh. Declared here, ahead of the class, so
// the friend declarations below name them; libgman/gmanpolygonmeshfactory.h
// declares them again for callers, with the same signatures. Internal to
// libgman and not GMAN_EXPORT: an embedder linking against the release
// library fails to link a call to any of the four.
namespace gman {
std::optional<GMANPolygonMesh> polygonMesh(RtInt nverts, GMANDictionary& dictionary, RtInt n, RtToken tokens[],
                                           RtPointer parms[], RtInt const* counts);
std::optional<GMANPolygonMesh> generalPolygonMesh(RtInt nloops, RtInt const nverts[], GMANDictionary& dictionary,
                                                  RtInt n, RtToken tokens[], RtPointer parms[], RtInt const* counts);
std::optional<GMANPolygonMesh> pointsPolygonsMesh(RtInt npolys, RtInt const nverts[], RtInt const verts[],
                                                  GMANDictionary& dictionary, RtInt n, RtToken tokens[],
                                                  RtPointer parms[], RtInt const* counts);
std::optional<GMANPolygonMesh> pointsGeneralPolygonsMesh(RtInt npolys, RtInt const nloops[], RtInt const nverts[],
                                                         RtInt const verts[], GMANDictionary& dictionary, RtInt n,
                                                         RtToken tokens[], RtPointer parms[], RtInt const* counts);
} // namespace gman

// The four polygon requests -- Polygon, GeneralPolygon, PointsPolygons and
// PointsGeneralPolygons -- normalised to RISpec's own PointsGeneralPolygons
// shape: a set of faces, each one or more loops of point indices into a
// shared "P". Only the factory functions above build one, each validating
// its request's indices first, so every index a mesh holds is in range.
class GMAN_EXPORT GMANPolygonMesh {
public:
  // The "P" parameter (parameters() holds it too): object-space points,
  // three floats per point, every index loop() returns in range.
  std::span<RtFloat const> points() const;

  // The number of faces; may be 0.
  std::size_t faceCount() const;

  // Face i's own loops, i < faceCount(): one entry per loop, that loop's
  // point count. Loop 0 is the outer ring; any later loop is a hole. At
  // least one entry.
  std::span<RtInt const> face(std::size_t i) const;

  // Loop j of face i, j < face(i).size(): face(i)[j] point indices, each
  // into points().
  std::span<RtInt const> loop(std::size_t i, std::size_t j) const;

  // The parameter list the factory built: vertex, varying, uniform and
  // facevarying lookups on RISpec's own indexing for this mesh's request.
  GMANParameterList const& parameters() const;

private:
  friend std::optional<GMANPolygonMesh> gman::polygonMesh(RtInt nverts, GMANDictionary& dictionary, RtInt n,
                                                          RtToken tokens[], RtPointer parms[], RtInt const* counts);
  friend std::optional<GMANPolygonMesh> gman::generalPolygonMesh(RtInt nloops, RtInt const nverts[],
                                                                 GMANDictionary& dictionary, RtInt n, RtToken tokens[],
                                                                 RtPointer parms[], RtInt const* counts);
  friend std::optional<GMANPolygonMesh> gman::pointsPolygonsMesh(RtInt npolys, RtInt const nverts[],
                                                                 RtInt const verts[], GMANDictionary& dictionary,
                                                                 RtInt n, RtToken tokens[], RtPointer parms[],
                                                                 RtInt const* counts);
  friend std::optional<GMANPolygonMesh> gman::pointsGeneralPolygonsMesh(RtInt npolys, RtInt const nloops[],
                                                                        RtInt const nverts[], RtInt const verts[],
                                                                        GMANDictionary& dictionary, RtInt n,
                                                                        RtToken tokens[], RtPointer parms[],
                                                                        RtInt const* counts);

  // parameterList is the parameter list the factory built; pointCount is
  // the number of points its "P" holds. faceLoopCounts holds every face's
  // own loop sizes, concatenated in face order; faceOffsets (faceCount + 1
  // entries) marks where each face's run starts in faceLoopCounts, the
  // last entry its total length. loopIndices holds every loop's own point
  // indices, concatenated in the same (face, loop) order; loopOffsets,
  // indexed the same way as faceLoopCounts, marks where each loop's own
  // run starts in loopIndices, the last entry its total length.
  GMANPolygonMesh(GMANParameterList parameterList, std::size_t pointCount, std::vector<RtInt> faceLoopCounts,
                  std::vector<std::size_t> faceOffsets, std::vector<RtInt> loopIndices);

  GMANParameterList parameterList;
  std::size_t pointCount;
  std::vector<RtInt> faceLoopCounts;
  std::vector<std::size_t> faceOffsets;
  std::vector<RtInt> loopIndices;
  std::vector<std::size_t> loopOffsets;
};
