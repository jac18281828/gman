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
 * GMANSphere's and GMANDisk's own area() and samplePoint(): a closed-form
 * area matching the analytic formula and an independent finite-difference
 * quadrature of the Jacobian; a uniform-by-area sampler proven by binned
 * histograms; a degenerate primitive documented as ineligible; and the
 * sampled normal. Also a polygon mesh's emission through gman::emitters and
 * gman::samplePoint: its area, a uniform draw by area, the choice of a fan
 * triangle, the draw's normal and the meshes ineligible to emit.
 */

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <memory>
#include <numbers>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "check.h"
#include "gmancolor.h"
#include "gmanemitter.h"
#include "gmanlinearworldmanager.h"
#include "gmanmath.h"
#include "gmanparameterlist.h"
#include "gmanpoint.h"
#include "gmanprimitives.h"
#include "gmanraydisk.h"
#include "gmanraypolygon.h"
#include "gmanraypolygonmesh.h"
#include "gmanraysphere.h"
#include "gmansampling.h"
#include "gmanshading.h"
#include "gmanvector.h"
#include "ri.h"
#include "samplingstats.h"

namespace {

constexpr std::uint32_t kSeed = 0x51ed270bu;
constexpr std::uint32_t kMeshDraws = 1u << 16;
constexpr int kMeshBinsPerSide = 8;

// Builds a one-primitive world tagging shape as an area light and returns
// gman::emitters' own count for it -- the eligibility gman::emitters itself
// applies, not a copy of it.
template <class Shape> std::size_t emittedCount(Shape* shape) {
  GMANLight light(GMAN_LIGHT_AREA, GMANColor(4.0f, 4.0f, 4.0f), GMANPoint(), GMANVector());
  gman::Appearance appearance;
  appearance.areaLight = &light;
  shape->setAppearance(appearance);

  GMANLinearWorldManager world;
  world.add(shape);
  return gman::emitters(world).size();
}

// Central-difference |dP/du x dP/dv| at (u, v), step h -- an
// implementation of the Jacobian independent of area()'s own closed form.
template <class Parametric> double jacobianAt(Parametric& shape, double u, double v, double h) {
  GMANPoint const pu1 = shape.getLocation(u + h, v);
  GMANPoint const pu0 = shape.getLocation(u - h, v);
  GMANPoint const pv1 = shape.getLocation(u, v + h);
  GMANPoint const pv0 = shape.getLocation(u, v - h);

  GMANVector const dpdu((pu1.getX() - pu0.getX()) / (2.0 * h), (pu1.getY() - pu0.getY()) / (2.0 * h),
                        (pu1.getZ() - pu0.getZ()) / (2.0 * h));
  GMANVector const dpdv((pv1.getX() - pv0.getX()) / (2.0 * h), (pv1.getY() - pv0.getY()) / (2.0 * h),
                        (pv1.getZ() - pv0.getZ()) / (2.0 * h));
  GMANVector const cross = dpdu.cross(dpdv);
  return std::sqrt(cross.dot(cross));
}

// The midpoint-rule quadrature of the Jacobian over the unit square, at an
// n x n grid -- the "converges to the analytic value" proof for a partial
// shape, independent of area()'s own formula.
template <class Parametric> double quadratureArea(Parametric& shape, int n) {
  constexpr double kStep = 1.0 / 1024.0;
  double sum = 0.0;
  for (int j = 0; j < n; ++j) {
    double const v = ((double)j + 0.5) / (double)n;
    for (int i = 0; i < n; ++i) {
      double const u = ((double)i + 0.5) / (double)n;
      sum += jacobianAt(shape, u, v, kStep);
    }
  }
  return sum / ((double)n * (double)n);
}

// Closed-form area.
void testAreaClosedForm() {
  GMANSphere fullSphere(2.0f, -2.0f, 2.0f, 360.0f, GMANParameterList());
  double const expectedFullSphere = 4.0 * std::numbers::pi * 2.0 * 2.0;
  check(std::fabs(fullSphere.area() - expectedFullSphere) < 1e-5,
        "area: a full sphere's area is within 1e-5 of 4*pi*radius^2");

  GMANSphere zone(2.0f, -1.0f, 1.0f, 180.0f, GMANParameterList());
  double const expectedZone = 2.0 * (1.0 - (-1.0)) * std::numbers::pi;
  check(std::fabs(zone.area() - expectedZone) < 1e-5, "area: a zone's area is exactly radius*(zmax-zmin)*pi");

  GMANDisk fullDisk(0.0f, 3.0f, 360.0f, GMANParameterList());
  double const expectedFullDisk = std::numbers::pi * 3.0 * 3.0;
  check(std::fabs(fullDisk.area() - expectedFullDisk) < 1e-5, "area: a full disk's area is within 1e-5 of pi*radius^2");

  GMANDisk halfDisk(0.0f, 3.0f, 180.0f, GMANParameterList());
  check(std::fabs(halfDisk.area() - expectedFullDisk * 0.5) < 1e-5, "area: a half disk's area is half the full disk's");
}

// The thetamax clamp in area(): a value above 360 degrees reads as a full
// sweep, never as extra surface.
void testThetaMaxAreaClamp() {
  GMANSphere sphere(1.0f, -1.0f, 1.0f, 720.0f, GMANParameterList());
  double const expectedSphere = 4.0 * std::numbers::pi;
  check(std::fabs(sphere.area() - expectedSphere) / expectedSphere < 1e-5,
        "thetamax clamp: a sphere at thetamax 720 still reads area 4*pi");

  GMANDisk disk(0.0f, 1.0f, 720.0f, GMANParameterList());
  double const expectedDisk = std::numbers::pi;
  check(std::fabs(disk.area() - expectedDisk) / expectedDisk < 1e-5,
        "thetamax clamp: a unit disk at thetamax 720 still reads area pi");
}

// The thetamax clamp in samplePoint(): a raw 720 still wraps the circle
// exactly twice and so still draws uniformly, hiding a missing clamp; 540
// does not -- a raw 540 puts two thirds of its draws in the first half of
// the circle, while the clamped value (360) draws four equal quadrants.
void testThetaMaxSampleClamp() {
  constexpr std::uint32_t kDraws = 1u << 16;
  constexpr int kBins = 4;
  double const expectedPerBin = (double)kDraws / (double)kBins;
  double const sigma = std::sqrt(expectedPerBin * (1.0 - 1.0 / (double)kBins));

  auto azimuthBin = [](GMANPoint const& p) {
    double theta = std::atan2((double)p.getY(), (double)p.getX());
    if (theta < 0.0) {
      theta += 2.0 * std::numbers::pi;
    }
    int bin = (int)(theta / (2.0 * std::numbers::pi) * kBins);
    return bin < 0 ? 0 : (bin >= kBins ? kBins - 1 : bin);
  };

  GMANSphere sphere(2.0f, -2.0f, 2.0f, 540.0f, GMANParameterList());
  std::vector<std::uint32_t> sphereBins(kBins, 0);
  for (std::uint32_t i = 0; i < kDraws; ++i) {
    gman::Sample2D const uv = gman::sample2D(kSeed, 4, 0, i, kDraws, 0u);
    GMANVector normal;
    GMANPoint const p = sphere.samplePoint(uv.u1, uv.u2, normal);
    ++sphereBins[azimuthBin(p)];
  }
  bool sphereBinsOk = true;
  for (int b = 0; b < kBins; ++b) {
    if (std::fabs((double)sphereBins[b] - expectedPerBin) > 5.0 * sigma) {
      sphereBinsOk = false;
    }
  }
  check(sphereBinsOk,
        "thetamax clamp: a sphere at thetamax 540 draws four equal azimuth bins, each within 5 sigma of N/4");

  GMANDisk disk(0.0f, 3.0f, 540.0f, GMANParameterList());
  std::vector<std::uint32_t> diskBins(kBins, 0);
  for (std::uint32_t i = 0; i < kDraws; ++i) {
    gman::Sample2D const uv = gman::sample2D(kSeed, 5, 0, i, kDraws, 0u);
    GMANVector normal;
    GMANPoint const p = disk.samplePoint(uv.u1, uv.u2, normal);
    ++diskBins[azimuthBin(p)];
  }
  bool diskBinsOk = true;
  for (int b = 0; b < kBins; ++b) {
    if (std::fabs((double)diskBins[b] - expectedPerBin) > 5.0 * sigma) {
      diskBinsOk = false;
    }
  }
  check(diskBinsOk, "thetamax clamp: a disk at thetamax 540 draws four equal azimuth bins, each within 5 sigma of N/4");
}

// An independent quadrature cross-check on the zone and half disk.
void testQuadratureCrossCheck() {
  constexpr int kGrid = 512;

  GMANSphere zone(2.0f, -1.0f, 1.0f, 180.0f, GMANParameterList());
  double const quadZone = quadratureArea(zone, kGrid);
  double const relZone = std::fabs(quadZone - zone.area()) / zone.area();
  std::printf("quadrature: zone area %.6f vs closed form %.6f (%.2e relative)\n", quadZone, zone.area(), relZone);
  check(relZone < 1e-3, "quadrature: the zone's quadrature area is within 1e-3 relative of area()");

  GMANDisk halfDisk(0.0f, 3.0f, 180.0f, GMANParameterList());
  double const quadHalfDisk = quadratureArea(halfDisk, kGrid);
  double const relHalfDisk = std::fabs(quadHalfDisk - halfDisk.area()) / halfDisk.area();
  std::printf("quadrature: half disk area %.6f vs closed form %.6f (%.2e relative)\n", quadHalfDisk, halfDisk.area(),
              relHalfDisk);
  check(relHalfDisk < 1e-3, "quadrature: the half disk's quadrature area is within 1e-3 relative of area()");
}

// Uniform by area, proven by a 16-bin histogram against the binomial
// standard deviation.
void testUniformByArea() {
  constexpr std::uint32_t kDraws = 1u << 16;
  constexpr int kBins = 16;

  GMANSphere zone(2.0f, -1.0f, 1.0f, 180.0f, GMANParameterList());
  std::vector<std::uint32_t> zBins(kBins, 0);
  for (std::uint32_t i = 0; i < kDraws; ++i) {
    gman::Sample2D const uv = gman::sample2D(kSeed, 0, 0, i, kDraws, 0u);
    GMANVector normal;
    GMANPoint const p = zone.samplePoint(uv.u1, uv.u2, normal);
    double const t = ((double)p.getZ() - (-1.0)) / (1.0 - (-1.0));
    int bin = (int)(t * kBins);
    bin = bin < 0 ? 0 : (bin >= kBins ? kBins - 1 : bin);
    ++zBins[bin];
  }
  double const expectedPerBin = (double)kDraws / (double)kBins;
  double const sigma = std::sqrt(expectedPerBin * (1.0 - 1.0 / (double)kBins));
  bool zoneBinsOk = true;
  for (int b = 0; b < kBins; ++b) {
    if (std::fabs((double)zBins[b] - expectedPerBin) > 5.0 * sigma) {
      zoneBinsOk = false;
    }
  }
  check(zoneBinsOk, "uniform by area: the zone's 16 z-bins are each within 5 sigma of N/16");

  // The half disk's own draws feed two independent histograms: theta,
  // uniform on [0, thetaMax) regardless of radius, and r^2, uniform on
  // [0, radius^2] under an area-uniform draw (the identity
  // r = radius * sqrt(u1) itself rests on) -- theta alone never exercises
  // that identity, since a bug confined to the radial draw changes no
  // angle.
  GMANDisk halfDisk(0.0f, 3.0f, 180.0f, GMANParameterList());
  double const diskRadius = 3.0;
  double const thetaMaxRadians = 180.0 / 360.0 * 2.0 * std::numbers::pi;
  std::vector<std::uint32_t> thetaBins(kBins, 0);
  std::vector<std::uint32_t> rSquaredBins(kBins, 0);
  for (std::uint32_t i = 0; i < kDraws; ++i) {
    gman::Sample2D const uv = gman::sample2D(kSeed, 1, 0, i, kDraws, 0u);
    GMANVector normal;
    GMANPoint const p = halfDisk.samplePoint(uv.u1, uv.u2, normal);
    double theta = std::atan2((double)p.getY(), (double)p.getX());
    if (theta < 0.0) {
      theta += 2.0 * std::numbers::pi;
    }
    double const t = theta / thetaMaxRadians;
    int bin = (int)(t * kBins);
    bin = bin < 0 ? 0 : (bin >= kBins ? kBins - 1 : bin);
    ++thetaBins[bin];

    double const rSquared = (double)p.getX() * (double)p.getX() + (double)p.getY() * (double)p.getY();
    double const tr = rSquared / (diskRadius * diskRadius);
    int rBin = (int)(tr * kBins);
    rBin = rBin < 0 ? 0 : (rBin >= kBins ? kBins - 1 : rBin);
    ++rSquaredBins[rBin];
  }
  bool thetaBinsOk = true;
  bool rSquaredBinsOk = true;
  for (int b = 0; b < kBins; ++b) {
    if (std::fabs((double)thetaBins[b] - expectedPerBin) > 5.0 * sigma) {
      thetaBinsOk = false;
    }
    if (std::fabs((double)rSquaredBins[b] - expectedPerBin) > 5.0 * sigma) {
      rSquaredBinsOk = false;
    }
  }
  check(thetaBinsOk, "uniform by area: the half disk's 16 theta-bins are each within 5 sigma of N/16");
  check(rSquaredBinsOk, "uniform by area: the half disk's 16 r^2-bins are each within 5 sigma of N/16");
}

// Degenerate primitives never reach gman::emitters: a non-positive radius,
// an empty or reversed z range, a z range reaching outside the sphere's own
// surface, or a non-positive thetamax, for both a sphere and a disk.
void testDegeneratePrimitives() {
  check(emittedCount(new GMANRaySphere(0.0f, -1.0f, 1.0f, 360.0f, GMANParameterList())) == 0,
        "degenerate: sphere radius 0 is ineligible");
  check(emittedCount(new GMANRaySphere(2.0f, 1.0f, -1.0f, 360.0f, GMANParameterList())) == 0,
        "degenerate: sphere zmax <= zmin is ineligible");
  check(emittedCount(new GMANRaySphere(2.0f, -1.0f, 3.0f, 360.0f, GMANParameterList())) == 0,
        "degenerate: sphere abs(zmax) > radius is ineligible");
  check(emittedCount(new GMANRaySphere(2.0f, -3.0f, 1.0f, 360.0f, GMANParameterList())) == 0,
        "degenerate: sphere abs(zmin) > radius is ineligible");
  check(emittedCount(new GMANRaySphere(2.0f, -1.0f, 1.0f, 0.0f, GMANParameterList())) == 0,
        "degenerate: sphere thetamax 0 is ineligible");
  check(emittedCount(new GMANRaySphere(2.0f, -1.0f, 1.0f, -10.0f, GMANParameterList())) == 0,
        "degenerate: a negative sphere thetamax is ineligible");
  check(emittedCount(new GMANRaySphere(2.0f, -2.0f, 2.0f, 360.0f, GMANParameterList())) == 1,
        "degenerate: a full sphere stays eligible, as a control");

  check(emittedCount(new GMANRayDisk(0.0f, 0.0f, 360.0f, GMANParameterList())) == 0,
        "degenerate: disk radius 0 is ineligible");
  check(emittedCount(new GMANRayDisk(0.0f, -1.0f, 360.0f, GMANParameterList())) == 0,
        "degenerate: a negative disk radius is ineligible");
  check(emittedCount(new GMANRayDisk(0.0f, 3.0f, 0.0f, GMANParameterList())) == 0,
        "degenerate: disk thetamax 0 is ineligible");
  check(emittedCount(new GMANRayDisk(0.0f, 3.0f, -10.0f, GMANParameterList())) == 0,
        "degenerate: a negative disk thetamax is ineligible");
  check(emittedCount(new GMANRayDisk(0.0f, 3.0f, 360.0f, GMANParameterList())) == 1,
        "degenerate: a full disk stays eligible, as a control");

  RtFloat const nan = std::numeric_limits<RtFloat>::quiet_NaN();
  check(emittedCount(new GMANRaySphere(nan, -1.0f, 1.0f, 360.0f, GMANParameterList())) == 0,
        "degenerate: a NaN sphere radius is ineligible");
  check(emittedCount(new GMANRayDisk(0.0f, nan, 360.0f, GMANParameterList())) == 0,
        "degenerate: a NaN disk radius is ineligible");
}

// The sampled normal.
void testSampledNormal() {
  constexpr std::uint32_t kDraws = 1u << 8;

  GMANSphere fullSphere(2.0f, -2.0f, 2.0f, 360.0f, GMANParameterList());
  bool sphereNormalOk = true;
  for (std::uint32_t i = 0; i < kDraws; ++i) {
    gman::Sample2D const uv = gman::sample2D(kSeed, 2, 0, i, kDraws, 0u);
    GMANVector normal;
    GMANPoint const p = fullSphere.samplePoint(uv.u1, uv.u2, normal);
    GMANVector expected(p.getX(), p.getY(), p.getZ());
    expected.normalize();
    double const d = std::fabs((double)normal.dot(expected) - 1.0);
    if (!(d < 1e-6)) {
      sphereNormalOk = false;
    }
  }
  check(sphereNormalOk, "normal: a full sphere's sampled normal is normalize(point) within 1e-6");

  GMANDisk fullDisk(0.0f, 3.0f, 360.0f, GMANParameterList());
  bool diskNormalOk = true;
  for (std::uint32_t i = 0; i < kDraws; ++i) {
    gman::Sample2D const uv = gman::sample2D(kSeed, 3, 0, i, kDraws, 0u);
    GMANVector normal;
    fullDisk.samplePoint(uv.u1, uv.u2, normal);
    if (normal.getX() != 0.0f || normal.getY() != 0.0f || normal.getZ() != -1.0f) {
      diskNormalOk = false;
    }
  }
  check(diskNormalOk, "normal: a full disk's sampled normal is exactly (0, 0, -1) at every draw");
}

struct FaceSpec {
  std::vector<GMANPoint> outer;
  std::vector<std::vector<GMANPoint>> holes;
};

// A one-mesh world tagged as one area light, built as getRSPolygonMesh builds
// one: the appearance sits on the mesh and on each face. The world owns the
// mesh, which owns its faces.
struct MeshWorld {
  GMANLight light{GMAN_LIGHT_AREA, GMANColor(4.0f, 4.0f, 4.0f), GMANPoint(), GMANVector()};
  GMANLinearWorldManager world;
  GMANRayPolygonMesh const* mesh = nullptr;

  explicit MeshWorld(std::vector<FaceSpec> const& specs) {
    gman::Appearance appearance;
    appearance.areaLight = &light;
    std::vector<std::unique_ptr<GMANRayPolygon>> faces;
    for (FaceSpec const& spec : specs) {
      std::vector<std::pair<RtFloat, RtFloat>> const texCoords(spec.outer.size(),
                                                               std::pair<RtFloat, RtFloat>(0.0f, 0.0f));
      auto face = std::make_unique<GMANRayPolygon>(spec.outer, spec.holes, texCoords);
      face->setAppearance(appearance);
      faces.push_back(std::move(face));
    }
    auto owned = std::make_unique<GMANRayPolygonMesh>(std::move(faces));
    owned->setAppearance(appearance);
    mesh = owned.get();
    world.add(owned.release());
  }
};

// The one emitter gman::emitters enumerates for w; empty, and a failed check,
// when it enumerates any other count.
std::optional<gman::Emitter> soleEmitter(MeshWorld& w, std::string const& label) {
  std::vector<gman::Emitter> const list = gman::emitters(w.world);
  check(list.size() == 1 && list[0].shape == w.mesh && list[0].light == &w.light,
        label + ": the mesh is one area emitter naming its own light");
  if (list.size() != 1) {
    return std::nullopt;
  }
  return list[0];
}

std::size_t meshEmitterCount(std::vector<FaceSpec> const& specs) {
  MeshWorld w(specs);
  return gman::emitters(w.world).size();
}

// The square of half-width half at height y, wound (-half, y, -half) (-half,
// y, half) (half, y, half) (half, y, -half): its plane normal is +y.
std::vector<GMANPoint> squareLoop(RtFloat half, RtFloat y) {
  return {GMANPoint(-half, y, -half), GMANPoint(-half, y, half), GMANPoint(half, y, half), GMANPoint(half, y, -half)};
}

FaceSpec solidFace(std::vector<GMANPoint> outer) { return FaceSpec{std::move(outer), {}}; }

void testPolygonArea() {
  MeshWorld w({solidFace({GMANPoint(-1.0f, 0.0f, -1.5f), GMANPoint(-1.0f, 0.0f, 1.5f), GMANPoint(1.0f, 0.0f, 1.5f),
                          GMANPoint(1.0f, 0.0f, -1.5f)})});
  std::optional<gman::Emitter> const emitter = soleEmitter(w, "polygon area");
  if (!emitter) {
    return;
  }

  constexpr std::uint32_t kDraws = 1u << 8;
  bool areaMatches = true;
  for (std::uint32_t i = 0; i < kDraws; ++i) {
    gman::Sample2D const uv = gman::sample2D(kSeed, 6, 0, i, kDraws, 0u);
    gman::EmitterPoint const ep = gman::samplePoint(*emitter, uv.u1, uv.u2);
    double const area = 1.0 / (double)ep.pdf;
    if (!(std::fabs(area - 6.0) / 6.0 < 1e-5)) {
      areaMatches = false;
    }
  }
  check(areaMatches, "polygon area: 1 / pdf of every draw on a 2 x 3 rectangle is 6 within 1e-5 relative");
}

// Whether every draw of emitter lies on the square [-1, 1] in x and z and the
// draws fill an 8 x 8 grid over it, each bin within 5 sigma of N / 64.
bool drawsFillSquareEvenly(gman::Emitter const& emitter, RtInt dim) {
  constexpr int kBins = kMeshBinsPerSide * kMeshBinsPerSide;
  std::vector<std::uint32_t> bins(kBins, 0);
  bool onSquare = true;
  for (std::uint32_t i = 0; i < kMeshDraws; ++i) {
    gman::Sample2D const uv = gman::sample2D(kSeed, dim, 0, i, kMeshDraws, 0u);
    gman::EmitterPoint const ep = gman::samplePoint(emitter, uv.u1, uv.u2);
    double const x = ep.point.getX();
    double const z = ep.point.getZ();
    if (std::fabs(x) > 1.0 + 1e-5 || std::fabs(z) > 1.0 + 1e-5) {
      onSquare = false;
    }
    int const column = std::clamp((int)((x + 1.0) / 2.0 * kMeshBinsPerSide), 0, kMeshBinsPerSide - 1);
    int const row = std::clamp((int)((z + 1.0) / 2.0 * kMeshBinsPerSide), 0, kMeshBinsPerSide - 1);
    ++bins[row * kMeshBinsPerSide + column];
  }
  double const expectedPerBin = (double)kMeshDraws / (double)kBins;
  double const sigma = std::sqrt(expectedPerBin * (1.0 - 1.0 / (double)kBins));
  bool evenly = true;
  for (std::uint32_t count : bins) {
    if (std::fabs((double)count - expectedPerBin) > 5.0 * sigma) {
      evenly = false;
    }
  }
  return onSquare && evenly;
}

void testPolygonUniformDraw() {
  MeshWorld w({solidFace(squareLoop(1.0f, 0.5f))});
  std::optional<gman::Emitter> const emitter = soleEmitter(w, "polygon uniform draw");
  if (!emitter) {
    return;
  }
  check(drawsFillSquareEvenly(*emitter, 7),
        "polygon uniform draw: 2^16 draws on a square fill its 8 x 8 bins, each within 5 sigma of N/64");
}

bool insideTriangle(double px, double pz, GMANPoint const& a, GMANPoint const& b, GMANPoint const& c) {
  auto const side = [&](GMANPoint const& from, GMANPoint const& to) {
    return ((double)to.getX() - from.getX()) * (pz - from.getZ()) -
           ((double)to.getZ() - from.getZ()) * (px - from.getX());
  };
  double const ab = side(a, b);
  double const bc = side(b, c);
  double const ca = side(c, a);
  return (ab > 0.0 && bc > 0.0 && ca > 0.0) || (ab < 0.0 && bc < 0.0 && ca < 0.0);
}

void testPolygonTriangleChoice() {
  GMANPoint const a(0.0f, 0.5f, 0.0f);
  GMANPoint const b(1.0f, 0.5f, 0.0f);
  GMANPoint const c(1.0f, 0.5f, 1.0f);
  GMANPoint const d(-1.0f, 0.5f, 2.0f);
  MeshWorld w({solidFace({a, b, c, d})});
  std::optional<gman::Emitter> const emitter = soleEmitter(w, "polygon triangle choice");
  if (!emitter) {
    return;
  }

  std::uint32_t inFirst = 0;
  for (std::uint32_t i = 0; i < kMeshDraws; ++i) {
    gman::Sample2D const uv = gman::sample2D(kSeed, 8, 0, i, kMeshDraws, 0u);
    gman::EmitterPoint const ep = gman::samplePoint(*emitter, uv.u1, uv.u2);
    if (insideTriangle(ep.point.getX(), ep.point.getZ(), a, b, c)) {
      ++inFirst;
    }
  }
  double const share = (double)inFirst / (double)kMeshDraws;
  double const sigma = std::sqrt(0.25 * 0.75 / (double)kMeshDraws);
  std::printf("polygon triangle choice: the first fan triangle holds %.6f of the draws, expected 0.25\n", share);
  checkNear(share, 0.25, sigma, 0.0,
            "polygon triangle choice: the first of two fan triangles of areas 1 : 3 takes 1/4 of the draws");
}

void testPolygonNormal() {
  GMANPoint const origin(0.5f, -0.25f, 2.0f);
  GMANVector const u(1.0f, 0.5f, -0.25f);
  GMANVector const v(-0.25f, 1.0f, 0.5f);
  auto const corner = [&](RtFloat s, RtFloat t) {
    return GMANPoint(origin.getX() + s * u.getX() + t * v.getX(), origin.getY() + s * u.getY() + t * v.getY(),
                     origin.getZ() + s * u.getZ() + t * v.getZ());
  };
  std::vector<GMANPoint> const tilted = {corner(-1.0f, -1.5f), corner(-1.0f, 1.5f), corner(1.0f, 1.5f),
                                         corner(1.0f, -1.5f)};
  GMANVector expected = GMANVector(tilted[0], tilted[1]).cross(GMANVector(tilted[0], tilted[2]));
  expected.normalize();
  double extent = 0.0;
  for (GMANPoint const& vertex : tilted) {
    GMANVector offset(tilted[0], vertex);
    extent = std::max(extent, (double)offset.magnitude());
  }

  MeshWorld tiltedWorld({solidFace(tilted)});
  std::optional<gman::Emitter> const tiltedEmitter = soleEmitter(tiltedWorld, "polygon normal");
  if (!tiltedEmitter) {
    return;
  }
  bool normalMatches = true;
  bool onPlane = true;
  constexpr std::uint32_t kDraws = 1u << 8;
  for (std::uint32_t i = 0; i < kDraws; ++i) {
    gman::Sample2D const uv = gman::sample2D(kSeed, 9, 0, i, kDraws, 0u);
    gman::EmitterPoint const ep = gman::samplePoint(*tiltedEmitter, uv.u1, uv.u2);
    GMANVector const drawn(ep.normal.getX(), ep.normal.getY(), ep.normal.getZ());
    if (!((drawn - expected).dot(drawn - expected) < 1e-12)) {
      normalMatches = false;
    }
    double const height = expected.dot(GMANVector(tilted[0], ep.point));
    if (!(std::fabs(height) <= 1e-5 * extent)) {
      onPlane = false;
    }
  }
  check(normalMatches, "polygon normal: every draw on a tilted quad carries the face's plane normal within 1e-6");
  check(onPlane, "polygon normal: every draw lies on the face's plane within 1e-5 of its extent");

  std::vector<GMANPoint> const up = squareLoop(1.0f, 1.5f);
  MeshWorld upWorld({solidFace(up)});
  std::optional<gman::Emitter> const upEmitter = soleEmitter(upWorld, "polygon normal, up winding");
  std::vector<GMANPoint> const down(up.rbegin(), up.rend());
  MeshWorld downWorld({solidFace(down)});
  std::optional<gman::Emitter> const downEmitter = soleEmitter(downWorld, "polygon normal, down winding");
  if (!upEmitter || !downEmitter) {
    return;
  }
  GMANNormal const upNormal = gman::samplePoint(*upEmitter, 0.3f, 0.6f).normal;
  GMANNormal const downNormal = gman::samplePoint(*downEmitter, 0.3f, 0.6f).normal;
  check(std::fabs(upNormal.getX()) < 1e-6 && std::fabs(upNormal.getY() - 1.0) < 1e-6 &&
            std::fabs(upNormal.getZ()) < 1e-6,
        "polygon normal: a square wound (-a, y, -b) (-a, y, b) (a, y, b) (a, y, -b) reports (0, 1, 0)");
  check(std::fabs(downNormal.getX()) < 1e-6 && std::fabs(downNormal.getY() + 1.0) < 1e-6 &&
            std::fabs(downNormal.getZ()) < 1e-6,
        "polygon normal: the reverse winding reports (0, -1, 0)");
}

void testPolygonEligibility() {
  RtFloat const nan = std::numeric_limits<RtFloat>::quiet_NaN();
  RtFloat const infinity = std::numeric_limits<RtFloat>::infinity();

  FaceSpec const holed{squareLoop(2.0f, 0.0f), {squareLoop(0.5f, 0.0f)}};
  check(meshEmitterCount({holed}) == 0, "polygon eligibility: a face with a hole is ineligible");

  FaceSpec const concave = solidFace({GMANPoint(4.0f, 0.0f, 0.0f), GMANPoint(1.0f, 0.0f, 1.0f),
                                      GMANPoint(0.0f, 0.0f, 4.0f), GMANPoint(0.0f, 0.0f, 0.0f)});
  check(meshEmitterCount({concave}) == 0,
        "polygon eligibility: a concave face whose fan leaves the face is ineligible");

  FaceSpec const sound = solidFace(squareLoop(1.0f, 0.0f));
  std::vector<GMANPoint> withNan = squareLoop(1.0f, 1.0f);
  withNan[3] = GMANPoint(1.0f, nan, -1.0f);
  check(meshEmitterCount({sound, solidFace(withNan)}) == 0,
        "polygon eligibility: a second face holding a NaN vertex makes the mesh ineligible");
  std::vector<GMANPoint> withInfinity = squareLoop(1.0f, 1.0f);
  withInfinity[3] = GMANPoint(1.0f, infinity, -1.0f);
  check(meshEmitterCount({sound, solidFace(withInfinity)}) == 0,
        "polygon eligibility: a second face holding an infinite vertex makes the mesh ineligible");

  check(meshEmitterCount({sound, solidFace(squareLoop(1.0f, 1.0f))}) == 1,
        "polygon eligibility: a two-face mesh of sound faces is one emitter");

  std::vector<GMANPoint> repeated = squareLoop(1.0f, 0.0f);
  GMANPoint const second = repeated[1];
  repeated.insert(repeated.begin() + 2, second);
  MeshWorld repeatedWorld({solidFace(repeated)});
  std::optional<gman::Emitter> const repeatedEmitter =
      soleEmitter(repeatedWorld, "polygon eligibility, repeated vertex");
  if (!repeatedEmitter) {
    return;
  }
  check(drawsFillSquareEvenly(*repeatedEmitter, 10),
        "polygon eligibility: a square with one vertex repeated draws evenly over its 8 x 8 bins");
}

} // namespace

int main() {
  testAreaClosedForm();
  testThetaMaxAreaClamp();
  testQuadratureCrossCheck();
  testUniformByArea();
  testThetaMaxSampleClamp();
  testDegeneratePrimitives();
  testSampledNormal();
  testPolygonArea();
  testPolygonUniformDraw();
  testPolygonTriangleChoice();
  testPolygonNormal();
  testPolygonEligibility();

  return checkSummary("GMANSphere and GMANDisk sample uniformly by area, matching their own closed-form area, and a "
                      "polygon mesh emits uniformly by area along its faces' normals");
}
