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
 * sampled normal.
 */

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "check.h"
#include "gmanemitter.h"
#include "gmanlinearworldmanager.h"
#include "gmanmath.h"
#include "gmanparameterlist.h"
#include "gmanprimitives.h"
#include "gmanraydisk.h"
#include "gmanraysphere.h"
#include "gmansampling.h"
#include "gmanshading.h"
#include "gmanvector.h"
#include "ri.h"
#include "samplingstats.h"

namespace {

constexpr std::uint32_t kSeed = 0x51ed270bu;
constexpr double kPi = 3.14159265358979323846;

// Builds a one-primitive world tagging sphere as an area light and returns
// gman::emitters' own count for it -- the eligibility gman::emitters itself
// applies, not a copy of it.
std::size_t emittedCount(GMANRaySphere* sphere) {
  GMANLight light(GMAN_LIGHT_AREA, GMANColor(4.0f, 4.0f, 4.0f), GMANPoint(), GMANVector());
  gman::Appearance appearance;
  appearance.areaLight = &light;
  sphere->setAppearance(appearance);

  GMANLinearWorldManager world;
  world.add(sphere);
  return gman::emitters(world).size();
}

std::size_t emittedCount(GMANRayDisk* disk) {
  GMANLight light(GMAN_LIGHT_AREA, GMANColor(4.0f, 4.0f, 4.0f), GMANPoint(), GMANVector());
  gman::Appearance appearance;
  appearance.areaLight = &light;
  disk->setAppearance(appearance);

  GMANLinearWorldManager world;
  world.add(disk);
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
  double const expectedFullSphere = 4.0 * kPi * 2.0 * 2.0;
  check(std::fabs(fullSphere.area() - expectedFullSphere) < 1e-5,
        "area: a full sphere's area is within 1e-5 of 4*pi*radius^2");

  GMANSphere zone(2.0f, -1.0f, 1.0f, 180.0f, GMANParameterList());
  double const expectedZone = 2.0 * (1.0 - (-1.0)) * kPi;
  check(std::fabs(zone.area() - expectedZone) < 1e-5, "area: a zone's area is exactly radius*(zmax-zmin)*pi");

  GMANDisk fullDisk(0.0f, 3.0f, 360.0f, GMANParameterList());
  double const expectedFullDisk = kPi * 3.0 * 3.0;
  check(std::fabs(fullDisk.area() - expectedFullDisk) < 1e-5, "area: a full disk's area is within 1e-5 of pi*radius^2");

  GMANDisk halfDisk(0.0f, 3.0f, 180.0f, GMANParameterList());
  check(std::fabs(halfDisk.area() - expectedFullDisk * 0.5) < 1e-5, "area: a half disk's area is half the full disk's");
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
  double const thetaMaxRadians = 180.0 / 360.0 * 2.0 * kPi;
  std::vector<std::uint32_t> thetaBins(kBins, 0);
  std::vector<std::uint32_t> rSquaredBins(kBins, 0);
  for (std::uint32_t i = 0; i < kDraws; ++i) {
    gman::Sample2D const uv = gman::sample2D(kSeed, 1, 0, i, kDraws, 0u);
    GMANVector normal;
    GMANPoint const p = halfDisk.samplePoint(uv.u1, uv.u2, normal);
    double theta = std::atan2((double)p.getY(), (double)p.getX());
    if (theta < 0.0) {
      theta += 2.0 * kPi;
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

} // namespace

int main() {
  testAreaClosedForm();
  testQuadratureCrossCheck();
  testUniformByArea();
  testDegeneratePrimitives();
  testSampledNormal();

  return checkSummary("GMANSphere and GMANDisk sample uniformly by area, matching their own closed-form area");
}
