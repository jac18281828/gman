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

#include "gmanpolygon.h"
#include "gmanraybbox.h"
#include "gmanraypolygon.h"

namespace {

// A dimensionless quantity -- ray.getDirection() and the polygon's own
// normal are both unit length -- so a ray this close to parallel with the
// polygon's plane reads the same tolerance at any scale, the same idiom
// gmanpatchpolyobjectmanager.cpp's kTriangulationTolerance uses for the
// same reason.
constexpr RtFloat kParallelTolerance = (RtFloat)1.0e-6;

// The axis the normal's own largest component names: projecting the ring
// and the hit point onto the other two keeps the even-odd test below from
// collapsing a polygon nearly edge-on to one of the three coordinate
// planes into a degenerate 2D shape.
int dominantAxis(GMANVector const& n) {
  RtFloat const ax = (RtFloat)fabs(n.getX());
  RtFloat const ay = (RtFloat)fabs(n.getY());
  RtFloat const az = (RtFloat)fabs(n.getZ());
  if (ax >= ay && ax >= az)
    return 0;
  if (ay >= az)
    return 1;
  return 2;
}

// p's two coordinates other than axis, in a fixed order every ring vertex
// and the hit point alike are projected through.
void project(GMANPoint const& p, int axis, RtFloat& a, RtFloat& b) {
  if (axis == 0) {
    a = p.getY();
    b = p.getZ();
  } else if (axis == 1) {
    a = p.getX();
    b = p.getZ();
  } else {
    a = p.getX();
    b = p.getY();
  }
}

// Barycentric weights of (px, py) in the 2D triangle (a, b, c); not
// clamped, so a point outside the triangle still returns a well-defined
// extrapolation rather than a failure. Returns false, leaving wa/wb/wc
// untouched, when the triangle's own area is too close to zero to divide
// by -- a's own fan vertex collinear with this pair -- so a caller never
// mistakes a degenerate triangle's leftover weights for containment.
bool barycentric2D(RtFloat px, RtFloat py, RtFloat ax, RtFloat ay, RtFloat bx, RtFloat by, RtFloat cx, RtFloat cy,
                   RtFloat& wa, RtFloat& wb, RtFloat& wc) {
  RtFloat const d = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy);
  if ((RtFloat)fabs(d) < kParallelTolerance) {
    return false;
  }
  wa = ((by - cy) * (px - cx) + (cx - bx) * (py - cy)) / d;
  wb = ((cy - ay) * (px - cx) + (ax - cx) * (py - cy)) / d;
  wc = (RtFloat)1.0 - wa - wb;
  return true;
}

// True when (wa, wb, wc) place a point inside its own triangle; a small
// negative tolerance admits a point that lands right on a fan diagonal.
bool insideTriangle(RtFloat wa, RtFloat wb, RtFloat wc) {
  constexpr RtFloat kBaryTolerance = (RtFloat)1.0e-4;
  return wa >= -kBaryTolerance && wb >= -kBaryTolerance && wc >= -kBaryTolerance;
}

// Interpolates each vertex's own (s, t) across hitPoint, fan-triangulated
// from vertex 0 ((v0, vi, vi+1) for i = 1..n-2) and projected the same way
// insidePolygon is. Exact for a triangle; for a per-vertex (s, t) that is
// itself affine in position -- every fixture this unit tests -- any
// triangulation gives the identical result at any interior point.
std::pair<RtFloat, RtFloat> interpolateTexCoord(std::vector<GMANPoint> const& ring,
                                                std::vector<std::pair<RtFloat, RtFloat>> const& texCoords, int axis,
                                                GMANPoint const& hitPoint) {
  RtFloat px, py;
  project(hitPoint, axis, px, py);
  RtFloat ax, ay;
  project(ring[0], axis, ax, ay);

  RtFloat wa = 0.0, wb = 0.0, wc = 0.0;
  std::size_t bi = 1, ci = 2;
  std::size_t const n = ring.size();
  for (std::size_t i = 1; i + 1 < n; i++) {
    RtFloat bx, by, cx, cy;
    project(ring[i], axis, bx, by);
    project(ring[i + 1], axis, cx, cy);
    RtFloat triWa, triWb, triWc;
    // A degenerate fan triangle (vertex 0 collinear with this pair, e.g. a
    // pentagon whose first three vertices lie on one edge) never contains
    // the hit -- skip it rather than let insideTriangle read its own
    // leftover weights as containment.
    if (!barycentric2D(px, py, ax, ay, bx, by, cx, cy, triWa, triWb, triWc))
      continue;
    wa = triWa;
    wb = triWb;
    wc = triWc;
    bi = i;
    ci = i + 1;
    if (insideTriangle(wa, wb, wc))
      break;
  }

  RtFloat const s = wa * texCoords[0].first + wb * texCoords[bi].first + wc * texCoords[ci].first;
  RtFloat const t = wa * texCoords[0].second + wb * texCoords[bi].second + wc * texCoords[ci].second;
  return {s, t};
}

// Even-odd (crossing number) test on the ring's projection to the plane
// dominantAxis names: correct for the convex polygon RiPolygon promises
// and, matching the z-buffer's own ear-clipped fill rule, for a concave or
// self-inconsistent one too.
bool insidePolygon(std::vector<GMANPoint> const& ring, int axis, GMANPoint const& p) {
  RtFloat px, py;
  project(p, axis, px, py);

  bool inside = false;
  std::size_t const n = ring.size();
  for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
    RtFloat xi, yi, xj, yj;
    project(ring[i], axis, xi, yi);
    project(ring[j], axis, xj, yj);
    if ((yi > py) != (yj > py)) {
      RtFloat const xCross = xi + (xj - xi) * (py - yi) / (yj - yi);
      if (px < xCross)
        inside = !inside;
    }
  }
  return inside;
}

} // namespace

namespace gman {

// Each point's own resolved (s, t): default its object-space "P" -- the
// same pre-CTM floats a request's own factory reads, not vertices, already
// camera space by the time it reaches here -- "st" then "s"/"t"
// overriding, GMANPatchPolyObjectManager's own polygon rule
// (resolvePolygonTextureCoordinates). Returns count entries of (0, 0) when
// pl carries no "P": a direct construction bypassing the object manager,
// or (for a mesh) a request whose "P" is already known present.
std::vector<std::pair<RtFloat, RtFloat>> resolvePointTexCoords(GMANParameterList const& pl, std::size_t count) {
  std::vector<std::pair<RtFloat, RtFloat>> texCoords(count, {(RtFloat)0.0, (RtFloat)0.0});
  RtFloat* p = (RtFloat*)pl.getPointer(gman::standardDictionary().getTokenId(RI_P));
  if (!p)
    return texCoords;

  RtFloat* sArr = (RtFloat*)pl.getPointer(gman::standardDictionary().getTokenId(RI_S));
  RtFloat* tArr = (RtFloat*)pl.getPointer(gman::standardDictionary().getTokenId(RI_T));
  RtFloat* stArr = (RtFloat*)pl.getPointer(gman::standardDictionary().getTokenId(RI_ST));
  for (std::size_t i = 0; i < count; i++) {
    RtFloat const objX = p[3 * i];
    RtFloat const objY = p[3 * i + 1];
    RtFloat s = objX;
    RtFloat t = objY;
    if (stArr) {
      s = stArr[2 * i];
      t = stArr[2 * i + 1];
    }
    if (sArr)
      s = sArr[i];
    if (tArr)
      t = tArr[i];
    texCoords[i] = {s, t};
  }
  return texCoords;
}

} // namespace gman

GMANRayPolygon::GMANRayPolygon(std::vector<GMANPoint> outer, GMANParameterList pl)
    : GMANRayPolygon(std::move(outer), {}, pl) {}

GMANRayPolygon::GMANRayPolygon(std::vector<GMANPoint> outer, std::vector<std::vector<GMANPoint>> holeLoops,
                               GMANParameterList pl)
    : GMANPolygon((RtInt)outer.size(), pl), vertices(std::move(outer)), holes(std::move(holeLoops)) {
  texCoords = gman::resolvePointTexCoords(pl, vertices.size());
  initGeometry();
}

GMANRayPolygon::GMANRayPolygon(std::vector<GMANPoint> outer, std::vector<std::vector<GMANPoint>> holeLoops,
                               std::vector<std::pair<RtFloat, RtFloat>> outerTexCoords)
    : GMANPolygon((RtInt)outer.size(), GMANParameterList()), vertices(std::move(outer)), holes(std::move(holeLoops)),
      texCoords(std::move(outerTexCoords)) {
  initGeometry();
}

void GMANRayPolygon::initGeometry() {
  degenerate = gman::isDegeneratePolygon(vertices);

  // Already camera space (RiPolygonV bakes the CTM in before the factory
  // returns): the componentwise min and max of vertices needs no corner
  // transform, unlike every other ray primitive here. Computed regardless
  // of degenerate -- plain min/max, unlike the normal below, divides by
  // nothing and so never needs the guard.
  if (!vertices.empty()) {
    GMANPoint minP = vertices[0];
    GMANPoint maxP = vertices[0];
    for (GMANPoint const& v : vertices) {
      minP = gman::pointMin(minP, v);
      maxP = gman::pointMax(maxP, v);
    }
    bbox = gman::padBBox(minP, maxP);
  }

  if (degenerate)
    return;

  // Dividing by the magnitude already computed here, rather than calling
  // GMANVector::normalize(), matters for the same reason it does in
  // buildFace: that method leaves a vector unchanged below RI_EPSILON
  // (1e-10), an absolute threshold a small-but-valid polygon's raw normal
  // can fall under even though isDegeneratePolygon's ratio guard has
  // already judged it non-degenerate.
  GMANVector n = gman::newellNormal(vertices);
  RtFloat const magnitude = n.magnitude();
  normal = n / magnitude;
}

bool GMANRayPolygon::intersect(const GMANRay& ray, GMANHit& hit) const {
  if (degenerate)
    return false;

  RtFloat const denom = ray.getDirection().dot(normal);
  if ((RtFloat)fabs(denom) < kParallelTolerance)
    return false;

  RtFloat const t = GMANVector(ray.getOrigin(), vertices[0]).dot(normal) / denom;
  if (t < ray.getTMin() || t > ray.getTMax())
    return false;

  GMANPoint const hitPoint = ray.pointAt(t);
  int const axis = dominantAxis(normal);
  if (!insidePolygon(vertices, axis, hitPoint))
    return false;
  // Inside the outer loop and outside every hole, each tested on its own:
  // overlapping holes stay empty rather than refilled by a combined
  // crossing count, and a hole reaching outside the outer loop adds
  // nothing.
  for (std::vector<GMANPoint> const& hole : holes) {
    if (insidePolygon(hole, axis, hitPoint))
      return false;
  }

  hit.t = t;
  hit.point = hitPoint;
  hit.normal = normal;
  // Each vertex's own resolved (s, t) (see the constructor), interpolated
  // across the hit.
  std::pair<RtFloat, RtFloat> const uv = interpolateTexCoord(vertices, texCoords, axis, hitPoint);
  hit.u = uv.first;
  hit.v = uv.second;
  hit.primitive = this;
  return true;
}
