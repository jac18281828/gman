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
#include <array>
#include <cmath>
#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

#include "gmanparameterlist.h"
#include "gmanpoint.h"
#include "gmanpolygon.h"
#include "gmanprimitives.h"
#include "gmanray.h"
#include "gmanraybbox.h"
#include "gmanrayfinitehit.h"
#include "gmanraypolygon.h"
#include "gmanvector.h"
#include "ri.h"

namespace {

// A dimensionless quantity -- ray.getDirection() and the polygon's own
// normal are both unit length -- so a ray this close to parallel with the
// polygon's plane reads the same tolerance at any scale, the same idiom
// gmanpatchpolyobjectmanager.cpp's kTriangulationTolerance uses for the
// same reason.
constexpr RtFloat kParallelTolerance = (RtFloat)1.0e-6;

// A hit within this fraction of the outer ring's extent of its boundary
// counts as on it. A ratio, so it reads the same at any scale: about 25
// times the float error a hit point carries a few extents from the camera,
// and far under the gap of a real miss.
constexpr RtFloat kBoundaryTolerance = (RtFloat)1.0e-5;

constexpr double kCollapseTolerance = 1.0e-6;

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
  if (!((RtFloat)fabs(d) >= kParallelTolerance)) {
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

// The fan triangle (v0, vb, vc) of ring that holds hitPoint, fan-triangulated
// from vertex 0 ((v0, vi, vi+1) for i = 1..n-2) and projected the same way
// insidePolygon is, with hitPoint's barycentric weights in it. valid is
// false when every fan triangle is degenerate.
struct FanTriangle {
  std::size_t b = 1;
  std::size_t c = 2;
  RtFloat wa = 0.0;
  RtFloat wb = 0.0;
  RtFloat wc = 0.0;
  bool valid = false;
};

FanTriangle fanTriangleAt(std::vector<GMANPoint> const& ring, int axis, GMANPoint const& hitPoint) {
  RtFloat px, py;
  project(hitPoint, axis, px, py);
  RtFloat ax, ay;
  project(ring[0], axis, ax, ay);

  FanTriangle fan;
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
    fan.wa = triWa;
    fan.wb = triWb;
    fan.wc = triWc;
    fan.b = i;
    fan.c = i + 1;
    fan.valid = true;
    if (insideTriangle(fan.wa, fan.wb, fan.wc))
      break;
  }
  return fan;
}

// Interpolates each vertex's own (s, t) across fan. Exact for a triangle; for
// a per-vertex (s, t) that is itself affine in position, any triangulation
// gives the identical result at any interior point.
std::pair<RtFloat, RtFloat> interpolateTexCoord(std::vector<std::pair<RtFloat, RtFloat>> const& texCoords,
                                                FanTriangle const& fan) {
  RtFloat const s = fan.wa * texCoords[0].first + fan.wb * texCoords[fan.b].first + fan.wc * texCoords[fan.c].first;
  RtFloat const t = fan.wa * texCoords[0].second + fan.wb * texCoords[fan.b].second + fan.wc * texCoords[fan.c].second;
  return {s, t};
}

std::optional<GMANVector> interpolateNormal(std::vector<GMANVector> const& normals, FanTriangle const& fan) {
  if (normals.empty() || !fan.valid)
    return std::nullopt;

  GMANVector blend = normals[0] * fan.wa + normals[fan.b] * fan.wb + normals[fan.c] * fan.wc;
  RtFloat const magnitude = blend.magnitude();
  if (!(magnitude > (RtFloat)0.0) || !std::isfinite(magnitude))
    return std::nullopt;
  return blend / magnitude;
}

std::optional<GMANPoint> smoothSurfaceOrigin(std::vector<GMANPoint> const& ring, std::vector<GMANVector> const& normals,
                                             FanTriangle const& fan, GMANPoint const& hitPoint) {
  std::array<std::size_t, 3> const corners = {0, fan.b, fan.c};
  std::array<RtFloat, 3> const weights = {fan.wa, fan.wb, fan.wc};
  GMANVector lift;
  for (std::size_t i = 0; i < corners.size(); ++i) {
    GMANVector const& normal = normals[corners[i]];
    RtFloat const above = std::max((RtFloat)0.0, GMANVector(hitPoint, ring[corners[i]]).dot(normal));
    lift += normal * (weights[i] * above);
  }
  GMANPoint const origin(hitPoint.getX() + lift.getX(), hitPoint.getY() + lift.getY(), hitPoint.getZ() + lift.getZ());
  if (!std::isfinite(origin.getX()) || !std::isfinite(origin.getY()) || !std::isfinite(origin.getZ()))
    return std::nullopt;
  return origin;
}

void fanTangents(std::vector<GMANPoint> const& ring, std::vector<std::pair<RtFloat, RtFloat>> const& texCoords,
                 FanTriangle const& fan, GMANVector& dPds, GMANVector& dPdt) {
  dPds = GMANVector();
  dPdt = GMANVector();
  if (!fan.valid)
    return;

  double const ds1 = (double)texCoords[fan.b].first - texCoords[0].first;
  double const dt1 = (double)texCoords[fan.b].second - texCoords[0].second;
  double const ds2 = (double)texCoords[fan.c].first - texCoords[0].first;
  double const dt2 = (double)texCoords[fan.c].second - texCoords[0].second;
  double const determinant = ds1 * dt2 - ds2 * dt1;
  if (std::fabs(determinant) <= kCollapseTolerance * (std::fabs(ds1 * dt2) + std::fabs(ds2 * dt1)))
    return;

  GMANVector const edge1(ring[0], ring[fan.b]);
  GMANVector const edge2(ring[0], ring[fan.c]);
  for (std::size_t axis = 0; axis < GMANVector::NCOORDS; ++axis) {
    dPds[axis] = (RtFloat)((dt2 * edge1[axis] - dt1 * edge2[axis]) / determinant);
    dPdt[axis] = (RtFloat)((ds1 * edge2[axis] - ds2 * edge1[axis]) / determinant);
  }
}

// Even-odd (crossing number) test on the ring's projection to the plane
// dominantAxis names: correct for the convex polygon RiPolygon promises
// and, matching the z-buffer's own ear-clipped fill rule, for a concave or
// self-inconsistent one too. Half-open: a point on the ring's max side is
// outside and one on its min side inside; onBoundary closes the outer ring.
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

// The squared distance from (px, py) to the segment (ax, ay)-(bx, by); a
// zero-length segment is the point (ax, ay).
RtFloat squaredDistanceToSegment(RtFloat px, RtFloat py, RtFloat ax, RtFloat ay, RtFloat bx, RtFloat by) {
  RtFloat const dx = bx - ax;
  RtFloat const dy = by - ay;
  RtFloat const lengthSquared = dx * dx + dy * dy;
  RtFloat along = (RtFloat)0.0;
  if (lengthSquared > (RtFloat)0.0)
    along = std::clamp(((px - ax) * dx + (py - ay) * dy) / lengthSquared, (RtFloat)0.0, (RtFloat)1.0);
  RtFloat const ex = px - (ax + along * dx);
  RtFloat const ey = py - (ay + along * dy);
  return ex * ex + ey * ey;
}

// Whether p's projection lies within kBoundaryTolerance times the ring's
// extent of any segment of the ring's own projection. Two polygons that
// share an edge each admit a point on it, so no ray between them escapes.
bool onBoundary(std::vector<GMANPoint> const& ring, int axis, GMANPoint const& p) {
  RtFloat px, py;
  project(p, axis, px, py);

  RtFloat const tolerance = kBoundaryTolerance * gman::boundingBoxExtent(ring);
  RtFloat const toleranceSquared = tolerance * tolerance;
  std::size_t const n = ring.size();
  for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
    RtFloat xi, yi, xj, yj;
    project(ring[i], axis, xi, yi);
    project(ring[j], axis, xj, yj);
    if (squaredDistanceToSegment(px, py, xi, yi, xj, yj) <= toleranceSquared)
      return true;
  }
  return false;
}

} // namespace

namespace gman {

// Each point's own resolved (s, t), through polygonTexCoords, against its
// object-space "P" -- the same pre-CTM floats a request's own factory
// reads, not vertices, already camera space by the time it reaches here.
// Returns count entries of (0, 0) when pl carries no "P": a direct
// construction bypassing the object manager, or (for a mesh) a request
// whose "P" is already known present.
std::vector<std::pair<RtFloat, RtFloat>> resolvePointTexCoords(GMANParameterList const& pl, std::size_t count) {
  RtFloat* p = gman::floatArray(pl, RI_P);
  if (!p)
    return std::vector<std::pair<RtFloat, RtFloat>>(count, {(RtFloat)0.0, (RtFloat)0.0});
  return polygonTexCoords(pl, p, count);
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
                               std::vector<std::pair<RtFloat, RtFloat>> outerTexCoords,
                               std::vector<GMANVector> outerNormals)
    : GMANPolygon((RtInt)outer.size(), GMANParameterList()), vertices(std::move(outer)), holes(std::move(holeLoops)),
      texCoords(std::move(outerTexCoords)), vertexNormals(std::move(outerNormals)) {
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

bool GMANRayPolygon::intersect(GMANRay const& ray, GMANHit& hit) const {
  if (degenerate)
    return false;

  RtFloat const denom = ray.getDirection().dot(normal);
  if (!((RtFloat)fabs(denom) >= kParallelTolerance))
    return false;

  RtFloat const t = GMANVector(ray.getOrigin(), vertices[0]).dot(normal) / denom;
  if (!(ray.getTMin() <= t && t <= ray.getTMax()))
    return false;

  GMANPoint const hitPoint = ray.pointAt(t);
  int const axis = dominantAxis(normal);
  // On or inside the outer loop, whose boundary closes (insidePolygon alone
  // is half-open), and outside every hole, each tested on its own:
  // overlapping holes stay empty rather than refilled by a combined
  // crossing count, and a hole reaching outside the outer loop adds
  // nothing. A hole's rim stays strict.
  if (!insidePolygon(vertices, axis, hitPoint) && !onBoundary(vertices, axis, hitPoint))
    return false;
  for (std::vector<GMANPoint> const& hole : holes) {
    if (insidePolygon(hole, axis, hitPoint))
      return false;
  }

  GMANHit candidate;
  candidate.t = t;
  candidate.point = hitPoint;
  candidate.normal = normal;
  // Each vertex's own resolved (s, t) (see the constructor), interpolated
  // across the hit.
  FanTriangle const fan = fanTriangleAt(vertices, axis, hitPoint);
  std::pair<RtFloat, RtFloat> const uv = interpolateTexCoord(texCoords, fan);
  candidate.u = uv.first;
  candidate.v = uv.second;
  fanTangents(vertices, texCoords, fan, candidate.dPdu, candidate.dPdv);
  candidate.shadingNormal = interpolateNormal(vertexNormals, fan);
  if (candidate.shadingNormal)
    candidate.shadowOrigin = smoothSurfaceOrigin(vertices, vertexNormals, fan, hitPoint);
  candidate.primitive = this;
  return gman::acceptFiniteHit(candidate, hit);
}
