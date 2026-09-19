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

#include "gmanmath.h"
#include "gmanpolygon.h"
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

GMANRayPolygon::GMANRayPolygon(std::vector<GMANPoint> verts, GMANParameterList pl)
    : GMANPolygon((RtInt)verts.size(), pl), vertices(std::move(verts)),
      degenerate(gman::isDegeneratePolygon(vertices)) {
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
  if (!insidePolygon(vertices, dominantAxis(normal), hitPoint))
    return false;

  hit.t = t;
  hit.point = hitPoint;
  hit.normal = normal;
  // A polygon has no parametric surface of its own; interpolated texture
  // coordinates wait for a later unit.
  hit.u = 0.0;
  hit.v = 0.0;
  hit.primitive = this;
  return true;
}
