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

#include <climits>
#include <cstddef>
#include <numeric>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "gmandictionary.h"
#include "gmanlog.h"
#include "gmanparameterlist.h"
#include "gmanpolygoninternal.h"
#include "gmanpolygonmesh.h"
#include "gmanpolygonmeshfactory.h"
#include "ri.h"

namespace {

// loopOffsets[k] is where loop k's own run of point indices starts in
// loopIndices, k counting sequentially across every face's own loops in
// (face, loop) order; the last entry is the total point-index count.
std::vector<std::size_t> loopOffsetsFrom(std::vector<RtInt> const& faceLoopCounts) {
  std::vector<std::size_t> offsets;
  offsets.reserve(faceLoopCounts.size() + 1);
  offsets.push_back(0);
  for (RtInt const count : faceLoopCounts) {
    offsets.push_back(offsets.back() + (std::size_t)count);
  }
  return offsets;
}

// The first negative nverts[i] in nverts[0, count), then an nverts sum
// above INT_MAX / 3: the rule pair Polygon, GeneralPolygon and the two
// Points requests each apply to their own nverts. Warns once naming
// request and the broken rule and returns std::nullopt on failure; on
// success returns the validated sum.
std::optional<long long> validateNvertsSum(char const* request, RtInt count, RtInt const* nverts) {
  long long total = 0;
  for (RtInt i = 0; i < count; i++) {
    if (nverts[i] < 0) {
      warning("{}: nverts[{}] = {} is negative; ignoring.", request, i, nverts[i]);
      return std::nullopt;
    }
    total += nverts[i];
  }
  if (total > (long long)INT_MAX / 3) {
    warning("{}: nverts sums to {}, times 3 overflows RtInt; ignoring.", request, total);
    return std::nullopt;
  }
  return total;
}

// PointsPolygons and PointsGeneralPolygons' shared rules, applied to
// nverts[0, nvertsLen) and the verts it indexes: the first negative
// nverts[i], an nverts sum above INT_MAX / 3, the first negative verts[i],
// then a vertex count (1 + max(verts)) whose triple overflows RtInt. On
// success, sets facevarying (sum nverts) and vertex and returns true; on
// failure, warns once naming request and the broken rule and returns
// false.
bool validatePointsIndices(char const* request, RtInt nvertsLen, RtInt const* nverts, RtInt const* verts,
                           RtInt& facevarying, RtInt& vertex) {
  std::optional<long long> const total = validateNvertsSum(request, nvertsLen, nverts);
  if (!total) {
    return false;
  }

  long long maxVert = -1;
  for (long long i = 0; i < *total; i++) {
    if (verts[i] < 0) {
      warning("{}: verts[{}] = {} is negative; ignoring.", request, i, verts[i]);
      return false;
    }
    if (verts[i] > maxVert) {
      maxVert = verts[i];
    }
  }
  long long const vertexCount = maxVert + 1;
  if (vertexCount * 3 > (long long)INT_MAX) {
    warning("{}: vertex count {} (1 + max(verts)), times 3 overflows "
            "RtInt; ignoring.",
            request, vertexCount);
    return false;
  }

  facevarying = (RtInt)*total;
  vertex = (RtInt)vertexCount;
  return true;
}

} // namespace

GMANPolygonMesh::GMANPolygonMesh(GMANParameterList parameterList, std::size_t pointCount,
                                 std::vector<RtInt> faceLoopCounts, std::vector<std::size_t> faceOffsets,
                                 std::vector<RtInt> loopIndices)
    : parameterList(std::move(parameterList)), pointCount(pointCount), faceLoopCounts(std::move(faceLoopCounts)),
      faceOffsets(std::move(faceOffsets)), loopIndices(std::move(loopIndices)),
      loopOffsets(loopOffsetsFrom(this->faceLoopCounts)) {}

std::span<RtFloat const> GMANPolygonMesh::points() const {
  RtFloat const* const p = gman::floatArray(parameterList, RI_P);
  if (p == nullptr) {
    return {};
  }
  return std::span<RtFloat const>(p, 3 * pointCount);
}

std::size_t GMANPolygonMesh::faceCount() const { return faceOffsets.empty() ? 0 : faceOffsets.size() - 1; }

std::span<RtInt const> GMANPolygonMesh::face(std::size_t i) const {
  return std::span<RtInt const>(faceLoopCounts.data() + faceOffsets[i], faceOffsets[i + 1] - faceOffsets[i]);
}

std::span<RtInt const> GMANPolygonMesh::loop(std::size_t i, std::size_t j) const {
  std::size_t const k = faceOffsets[i] + j;
  return std::span<RtInt const>(loopIndices.data() + loopOffsets[k], loopOffsets[k + 1] - loopOffsets[k]);
}

GMANParameterList const& GMANPolygonMesh::parameters() const { return parameterList; }

namespace gman {

std::optional<GMANPolygonMesh> polygonMesh(RtInt nverts, GMANDictionary& dictionary, RtInt n, RtToken tokens[],
                                           RtPointer parms[], RtInt const* counts) {
  if (!validateNvertsSum("Polygon", 1, &nverts)) {
    return std::nullopt;
  }

  GMANParameterList list(dictionary, n, tokens, parms, nverts, nverts, 1, 1, counts);
  if (floatArray(list, RI_P) == nullptr) {
    return std::nullopt;
  }

  std::vector<RtInt> faceLoopCounts{nverts};
  std::vector<std::size_t> faceOffsets{0, 1};
  std::vector<RtInt> loopIndices((std::size_t)nverts);
  std::iota(loopIndices.begin(), loopIndices.end(), RtInt{0});

  return GMANPolygonMesh(std::move(list), (std::size_t)nverts, std::move(faceLoopCounts), std::move(faceOffsets),
                         std::move(loopIndices));
}

std::optional<GMANPolygonMesh> generalPolygonMesh(RtInt nloops, RtInt const nverts[], GMANDictionary& dictionary,
                                                  RtInt n, RtToken tokens[], RtPointer parms[], RtInt const* counts) {
  if (nloops < 1) {
    warning("GeneralPolygon: nloops = {} is invalid; ignoring.", nloops);
    return std::nullopt;
  }

  std::optional<long long> const total = validateNvertsSum("GeneralPolygon", nloops, nverts);
  if (!total) {
    return std::nullopt;
  }
  RtInt const vertex = (RtInt)*total;

  GMANParameterList list(dictionary, n, tokens, parms, vertex, vertex, 1, vertex, counts);
  if (floatArray(list, RI_P) == nullptr) {
    return std::nullopt;
  }

  std::vector<RtInt> faceLoopCounts(nverts, nverts + nloops);
  std::vector<std::size_t> faceOffsets{0, (std::size_t)nloops};
  std::vector<RtInt> loopIndices((std::size_t)vertex);
  std::iota(loopIndices.begin(), loopIndices.end(), RtInt{0});

  return GMANPolygonMesh(std::move(list), (std::size_t)vertex, std::move(faceLoopCounts), std::move(faceOffsets),
                         std::move(loopIndices));
}

std::optional<GMANPolygonMesh> pointsPolygonsMesh(RtInt npolys, RtInt const nverts[], RtInt const verts[],
                                                  GMANDictionary& dictionary, RtInt n, RtToken tokens[],
                                                  RtPointer parms[], RtInt const* counts) {
  if (npolys < 0) {
    warning("PointsPolygons: npolys = {} is invalid; ignoring.", npolys);
    return std::nullopt;
  }

  RtInt facevarying = 0, vertex = 0;
  if (!validatePointsIndices("PointsPolygons", npolys, nverts, verts, facevarying, vertex)) {
    return std::nullopt;
  }

  GMANParameterList list(dictionary, n, tokens, parms, vertex, vertex, npolys, facevarying, counts);
  if (floatArray(list, RI_P) == nullptr) {
    return std::nullopt;
  }

  std::vector<RtInt> faceLoopCounts(nverts, nverts + npolys);
  std::vector<std::size_t> faceOffsets((std::size_t)npolys + 1);
  std::iota(faceOffsets.begin(), faceOffsets.end(), std::size_t{0});
  std::vector<RtInt> loopIndices(verts, verts + facevarying);

  return GMANPolygonMesh(std::move(list), (std::size_t)vertex, std::move(faceLoopCounts), std::move(faceOffsets),
                         std::move(loopIndices));
}

std::optional<GMANPolygonMesh> pointsGeneralPolygonsMesh(RtInt npolys, RtInt const nloops[], RtInt const nverts[],
                                                         RtInt const verts[], GMANDictionary& dictionary, RtInt n,
                                                         RtToken tokens[], RtPointer parms[], RtInt const* counts) {
  if (npolys < 0) {
    warning("PointsGeneralPolygons: npolys = {} is invalid; ignoring.", npolys);
    return std::nullopt;
  }

  long long totalLoops = 0;
  for (RtInt i = 0; i < npolys; i++) {
    if (nloops[i] < 1) {
      warning("PointsGeneralPolygons: nloops[{}] = {} is invalid; ignoring.", i, nloops[i]);
      return std::nullopt;
    }
    totalLoops += nloops[i];
  }
  if (totalLoops > (long long)INT_MAX) {
    warning("PointsGeneralPolygons: nloops sums to {}, overflows RtInt; ignoring.", totalLoops);
    return std::nullopt;
  }

  RtInt facevarying = 0, vertex = 0;
  if (!validatePointsIndices("PointsGeneralPolygons", (RtInt)totalLoops, nverts, verts, facevarying, vertex)) {
    return std::nullopt;
  }

  GMANParameterList list(dictionary, n, tokens, parms, vertex, vertex, npolys, facevarying, counts);
  if (floatArray(list, RI_P) == nullptr) {
    return std::nullopt;
  }

  std::vector<RtInt> faceLoopCounts(nverts, nverts + totalLoops);
  std::vector<std::size_t> faceOffsets((std::size_t)npolys + 1);
  faceOffsets[0] = 0;
  for (RtInt i = 0; i < npolys; i++) {
    faceOffsets[(std::size_t)i + 1] = faceOffsets[(std::size_t)i] + (std::size_t)nloops[i];
  }
  std::vector<RtInt> loopIndices(verts, verts + facevarying);

  return GMANPolygonMesh(std::move(list), (std::size_t)vertex, std::move(faceLoopCounts), std::move(faceOffsets),
                         std::move(loopIndices));
}

} // namespace gman
