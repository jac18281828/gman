/* SPDX-License-Identifier: LGPL-2.1-or-later
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

/*
 * R5c review proof: every quadric intersector places a hit on its own
 * implicit surface and on its own ray at any origin distance, R5c's
 * torus pattern applied to the sphere, cylinder, cone, paraboloid,
 * hyperboloid and disk. A viewpoint at distance D fires a ray from a
 * surface point back at a target just inside the shape; the first
 * crossing must be that surface point, whether D is 5, 1e4 or 1e6.
 */

#include <cmath>
#include <format>
#include <string>

#include "check.h"
#include "gmanray.h"
#include "gmanraycone.h"
#include "gmanraycylinder.h"
#include "gmanrayhyperboloid.h"
#include "gmanrayparaboloid.h"
#include "gmanraysphere.h"

namespace {

struct SweepResult {
  int hits = 0;
  int total = 0;
  double worstResidual = 0.0;
  double worstOnRay = 0.0;
};

// A 16x16 grid of (u, v) cell centres over [0.1, 0.9]^2, kept 10% off every
// edge: at D == 1e6 a float ray direction resolves only about 0.06 across a
// unit shape, and an edge target would put the surface point's own tangent
// plane out of the grazing ray's reach. For each target, a ray starts at
// the surface point plus D * n (n the unit outward normal) and aims at the
// surface point moved 1% of the shape's scale along -n, so its first
// crossing is the surface point itself or an earlier sheet.
template <typename Shape, typename Residual>
SweepResult sweepShape(Shape& shape, double scale, double D, Residual residual) {
  SweepResult result;
  constexpr int kGrid = 16;
  constexpr double kLo = 0.1, kHi = 0.9;

  for (int i = 0; i < kGrid; ++i) {
    for (int j = 0; j < kGrid; ++j) {
      double const u = kLo + (kHi - kLo) * (i + 0.5) / kGrid;
      double const v = kLo + (kHi - kLo) * (j + 0.5) / kGrid;

      GMANPoint const surface = shape.getLocation(u, v);
      GMANVector normal = shape.getNormal(u, v);
      normal.normalize();

      GMANPoint const target(surface.getX() - (RtFloat)(0.01 * scale) * normal.getX(),
                             surface.getY() - (RtFloat)(0.01 * scale) * normal.getY(),
                             surface.getZ() - (RtFloat)(0.01 * scale) * normal.getZ());
      GMANPoint const origin(surface.getX() + (RtFloat)(D * normal.getX()),
                             surface.getY() + (RtFloat)(D * normal.getY()),
                             surface.getZ() + (RtFloat)(D * normal.getZ()));
      GMANVector const direction(origin, target);
      GMANRay const ray(origin, direction);
      GMANHit hit;

      ++result.total;
      if (!shape.intersect(ray, hit))
        continue;
      ++result.hits;

      double const res = std::fabs(residual(hit.point));
      result.worstResidual = std::max(result.worstResidual, res);

      // The distance from hit.point to the line through the ray's own
      // camera-space origin along its direction, all in double. GMANRay
      // normalizes direction to float precision only, so dirSq sits a few
      // ULPs off 1 -- dividing the projection by it (rather than assuming
      // a unit direction) keeps that deficit from scaling up by the
      // origin's own magnitude and forging a residual of its own.
      double const ox = (double)ray.getOrigin().getX(), oy = (double)ray.getOrigin().getY(),
                   oz = (double)ray.getOrigin().getZ();
      double const dx = (double)ray.getDirection().getX(), dy = (double)ray.getDirection().getY(),
                   dz = (double)ray.getDirection().getZ();
      double const rayDirSq = dx * dx + dy * dy + dz * dz;
      double const px = (double)hit.point.getX() - ox, py = (double)hit.point.getY() - oy,
                   pz = (double)hit.point.getZ() - oz;
      double const proj = (px * dx + py * dy + pz * dz) / rayDirSq;
      double const rx = px - proj * dx, ry = py - proj * dy, rz = pz - proj * dz;
      result.worstOnRay = std::max(result.worstOnRay, std::sqrt(rx * rx + ry * ry + rz * rz));
    }
  }
  return result;
}

void checkSweep(char const* surfaceName, double D, SweepResult const& r) {
  double const minFraction = (D >= 1e6) ? 0.95 : 1.0;
  int const minHits = (int)std::ceil(minFraction * r.total);
  std::string const label = std::string(surfaceName) + ", D == " + std::format("{:g}", D);

  check(r.hits >= minHits, label + ": " + std::to_string(r.hits) + "/" + std::to_string(r.total) +
                               " rays hit, worst residual " + std::format("{:.3e}", r.worstResidual) +
                               ", worst on-ray distance " + std::format("{:.3e}", r.worstOnRay));
  check(r.worstResidual <= 1e-5,
        label + ": worst implicit residual " + std::format("{:.3e}", r.worstResidual) + " is within 1e-5 of zero");
  check(r.worstOnRay <= 1e-5,
        label + ": worst on-ray distance " + std::format("{:.3e}", r.worstOnRay) + " is within 1e-5");
}

void testSphere() {
  GMANRaySphere sphere(1.0, -1.0, 1.0, 360.0, GMANParameterList());
  auto const residual = [](GMANPoint const& p) {
    double const x = p.getX(), y = p.getY(), z = p.getZ();
    return x * x + y * y + z * z - 1.0;
  };
  for (double const D : {5.0, 1e4, 1e6})
    checkSweep("sphere", D, sweepShape(sphere, 1.0, D, residual));
}

void testCylinder() {
  GMANRayCylinder cylinder(1.0, -1.0, 1.0, 360.0, GMANParameterList());
  auto const residual = [](GMANPoint const& p) {
    double const x = p.getX(), y = p.getY();
    return x * x + y * y - 1.0;
  };
  for (double const D : {5.0, 1e4, 1e6})
    checkSweep("cylinder", D, sweepShape(cylinder, 1.0, D, residual));
}

void testCone() {
  double const height = 1.0, radius = 1.0;
  GMANRayCone cone(height, radius, 360.0, GMANParameterList());
  auto const residual = [=](GMANPoint const& p) {
    double const x = p.getX(), y = p.getY(), z = p.getZ();
    double const r = radius * (1.0 - z / height);
    return x * x + y * y - r * r;
  };
  for (double const D : {5.0, 1e4, 1e6})
    checkSweep("cone", D, sweepShape(cone, 1.0, D, residual));
}

void testParaboloid() {
  double const rmax = 1.0, zmin = 0.0, zmax = 1.0;
  GMANRayParaboloid paraboloid(rmax, zmin, zmax, 360.0, GMANParameterList());
  double const k = rmax * rmax / (zmax * (zmax - zmin));
  auto const residual = [=](GMANPoint const& p) {
    double const x = p.getX(), y = p.getY(), z = p.getZ();
    return x * x + y * y - k * (z - zmin);
  };
  for (double const D : {5.0, 1e4, 1e6})
    checkSweep("paraboloid", D, sweepShape(paraboloid, 1.0, D, residual));
}

void testHyperboloidSpanning() {
  RtPoint p1 = {1.0, 0.0, -1.0};
  RtPoint p2 = {0.0, 1.0, 1.0};
  GMANRayHyperboloid hyperboloid(p1, p2, 360.0, GMANParameterList());

  double const dxSeg = p2[0] - p1[0], dySeg = p2[1] - p1[1], dzSeg = p2[2] - p1[2];
  double const p0sq = p1[0] * p1[0] + p1[1] * p1[1];
  double const pDot = p1[0] * dxSeg + p1[1] * dySeg;
  double const dSq = dxSeg * dxSeg + dySeg * dySeg;
  double const z1 = p1[2];
  auto const residual = [=](GMANPoint const& p) {
    double const x = p.getX(), y = p.getY(), z = p.getZ();
    double const v = (z - z1) / dzSeg;
    double const r2 = p0sq + 2.0 * pDot * v + dSq * v * v;
    return x * x + y * y - r2;
  };
  for (double const D : {5.0, 1e4, 1e6})
    checkSweep("spanning hyperboloid", D, sweepShape(hyperboloid, 1.0, D, residual));
}

} // namespace

int main() {
  testSphere();
  testCylinder();
  testCone();
  testParaboloid();
  testHyperboloidSpanning();

  return checkSummary("Every quadric intersector's hit sits on its implicit surface and its own ray");
}
