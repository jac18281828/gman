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
 * No NaN enters, crosses or leaves the ray kernel.
 *
 * Untraceable rays: a ray with a non-finite origin or direction component,
 * a direction shorter than half a unit, a NaN tmin or tmax or tmin above
 * tmax misses GMANRayBVH::nearestHit without a box or primitive test.
 *
 * Denormal components: a direction component with no finite reciprocal
 * does the same work and finds the same hit as an exact zero, whichever
 * face of a box the origin sits on.
 *
 * NaN boxes: a primitive whose box carries a NaN bound stays out of the
 * tree, and every other primitive's hits are unchanged. Each build that
 * leaves one or more out logs one warning naming the count.
 *
 * Finite hits: a fixed-seed family of rays with direction components of
 * 1e-25, 1e-38, 1e-40 and 0, from origins on and beside each primitive's
 * box, reaches each of the eight intersectors, directly and through the
 * tree, and no hit carries a non-finite t, point, normal, u, v, dPdu or
 * dPdv.
 */

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <fstream>
#include <limits>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "check.h"
#include "gmanlog.h"
#include "gmanowned.h"
#include "gmanray.h"
#include "gmanraybvh.h"
#include "gmanraycone.h"
#include "gmanraycylinder.h"
#include "gmanraydisk.h"
#include "gmanrayhyperboloid.h"
#include "gmanrayparaboloid.h"
#include "gmanraypolygon.h"
#include "gmanraysphere.h"
#include "gmanraytorus.h"
#include "gmanworld.h"
#include "maketransform.h"

namespace {

constexpr RtFloat kNaN = std::numeric_limits<RtFloat>::quiet_NaN();
constexpr RtFloat kInfinity = std::numeric_limits<RtFloat>::infinity();
constexpr RtFloat kDenormal = 1e-40f;
constexpr RtFloat kSentinelT = -7.0f;
constexpr std::size_t kSampledRays = 4000;
constexpr unsigned kSeed = 20260922;

GMANTransform translated(RtFloat dx, RtFloat dy, RtFloat dz) {
  GMANMatrix4 m;
  m.trans(dx, dy, dz);
  return makeTransform(m);
}

class SolidBox : public GMANRayPrimitive {
public:
  SolidBox(GMANPoint const& lo, GMANPoint const& hi) : lo(lo), hi(hi) { bbox = GMANBBox(lo, hi); }

  bool intersect(GMANRay const& ray, GMANHit& hit) const override {
    double const origin[3] = {ray.getOrigin().getX(), ray.getOrigin().getY(), ray.getOrigin().getZ()};
    double const direction[3] = {ray.getDirection().getX(), ray.getDirection().getY(), ray.getDirection().getZ()};
    double const low[3] = {lo.getX(), lo.getY(), lo.getZ()};
    double const high[3] = {hi.getX(), hi.getY(), hi.getZ()};
    double entry = ray.getTMin();
    double exit = ray.getTMax();
    for (std::size_t axis = 0; axis < 3; ++axis) {
      if (direction[axis] == 0.0) {
        if (origin[axis] < low[axis] || origin[axis] > high[axis]) {
          return false;
        }
        continue;
      }
      double t0 = (low[axis] - origin[axis]) / direction[axis];
      double t1 = (high[axis] - origin[axis]) / direction[axis];
      if (t0 > t1) {
        std::swap(t0, t1);
      }
      entry = std::fmax(entry, t0);
      exit = std::fmin(exit, t1);
    }
    if (!(entry <= exit)) {
      return false;
    }
    hit.t = (RtFloat)entry;
    hit.point = ray.pointAt(hit.t);
    hit.normal = GMANVector(0.0, 0.0, -1.0);
    hit.primitive = this;
    return true;
  }

private:
  GMANPoint lo;
  GMANPoint hi;
};

class NaNBoxPrimitive : public GMANRayPrimitive {
public:
  NaNBoxPrimitive() {
    RtBound const bound = {kNaN, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f};
    bbox = bound;
  }

  bool intersect(GMANRay const&, GMANHit& hit) const override {
    hit = GMANHit();
    hit.t = 1.0f;
    hit.primitive = this;
    return true;
  }
};

struct Probe {
  bool found = false;
  std::size_t boxTests = 0;
  std::size_t primitiveTests = 0;
  GMANHit hit;
  GMANRayPrimitive const* primitive = nullptr;
};

Probe probe(GMANRayBVH const& bvh, GMANRay const& ray) {
  Probe result;
  result.hit.t = kSentinelT;
  result.found = bvh.nearestHit(ray, result.hit, result.primitive, &result.primitiveTests, &result.boxTests);
  return result;
}

bool sameWork(Probe const& a, Probe const& b) {
  return a.found == b.found && a.boxTests == b.boxTests && a.primitiveTests == b.primitiveTests &&
         (!a.found || (a.hit.t == b.hit.t && a.primitive == b.primitive));
}

void expectSameWork(GMANRayBVH const& bvh, GMANPoint const& origin, GMANVector const& denormal, GMANVector const& zero,
                    std::string const& what) {
  Probe const withDenormal = probe(bvh, GMANRay(origin, denormal));
  Probe const withZero = probe(bvh, GMANRay(origin, zero));
  std::printf("%s: denormal found=%d boxTests=%zu primitiveTests=%zu, zero found=%d boxTests=%zu primitiveTests=%zu\n",
              what.c_str(), (int)withDenormal.found, withDenormal.boxTests, withDenormal.primitiveTests,
              (int)withZero.found, withZero.boxTests, withZero.primitiveTests);
  check(withZero.found && withZero.boxTests > 1 && withZero.primitiveTests > 0,
        what + ": the exact-zero ray hits through the tree");
  check(sameWork(withDenormal, withZero), what + ": the denormal ray does the same work and finds the same hit");
}

// ---- untraceable rays ----
void testUntraceableRaysMiss() {
  GMANWorld world;
  for (std::size_t i = 0; i < 5; ++i) {
    world.add(
        gman::make<GMANRaySphere>(0.4, -0.4, 0.4, 360.0, GMANParameterList(), translated((RtFloat)i * 3.0, 0.0, 0.0)));
  }
  GMANRayBVH bvh;
  bvh.build(world);

  GMANPoint const origin(-5.0, 0.0, 0.0);
  GMANVector const along(1.0, 0.0, 0.0);
  Probe const control = probe(bvh, GMANRay(origin, along));
  check(control.found && control.boxTests > 0 && control.primitiveTests > 0,
        "untraceable: the control ray hits through the tree");

  struct Case {
    std::string what;
    GMANRay ray;
  };
  std::vector<Case> const cases = {
      {"a NaN origin x", GMANRay(GMANPoint(kNaN, 0.0, 0.0), along)},
      {"a NaN origin y", GMANRay(GMANPoint(-5.0, kNaN, 0.0), along)},
      {"a NaN origin z", GMANRay(GMANPoint(-5.0, 0.0, kNaN), along)},
      {"an infinite origin", GMANRay(GMANPoint(-kInfinity, 0.0, 0.0), along)},
      {"an infinite direction component", GMANRay(origin, GMANVector(kInfinity, 0.0, 0.0))},
      {"a NaN direction component", GMANRay(origin, GMANVector(1.0, kNaN, 0.0))},
      {"a zero direction", GMANRay(origin, GMANVector(0.0, 0.0, 0.0))},
      {"a direction under the normalization threshold", GMANRay(origin, GMANVector(1e-20, 0.0, 0.0))},
      {"a NaN tMin", GMANRay(origin, along, kNaN, RI_INFINITY)},
      {"a NaN tMax", GMANRay(origin, along, 0.0, kNaN)},
      {"tMin above tMax", GMANRay(origin, along, 10.0, 1.0)},
  };
  for (Case const& c : cases) {
    Probe const result = probe(bvh, c.ray);
    check(!result.found && result.boxTests == 0 && result.primitiveTests == 0 && result.hit.t == kSentinelT,
          "untraceable: " + c.what + " misses without a box or primitive test");

    GMANHit hit;
    hit.t = kSentinelT;
    GMANRayPrimitive const* primitive = nullptr;
    check(!bvh.nearestHit(c.ray, hit, primitive) && hit.t == kSentinelT && primitive == nullptr,
          "untraceable: " + c.what + " misses through the three-argument overload");
  }
}

// ---- a denormal component is an exact zero ----
void testDenormalComponentIsParallel() {
  GMANWorld world;
  GMANPoint rootMin(kInfinity, kInfinity, kInfinity);
  GMANPoint rootMax(-kInfinity, -kInfinity, -kInfinity);
  for (std::size_t i = 0; i < 5; ++i) {
    RtFloat const x = (RtFloat)i * 3.0f;
    auto box = gman::make<SolidBox>(GMANPoint(x - 0.5, -0.5, -0.5), GMANPoint(x + 0.5, 0.5, 0.5));
    GMANPoint const lo = box->getBBox().getMin();
    GMANPoint const hi = box->getBBox().getMax();
    rootMin = GMANPoint(std::fmin(rootMin.getX(), lo.getX()), std::fmin(rootMin.getY(), lo.getY()),
                        std::fmin(rootMin.getZ(), lo.getZ()));
    rootMax = GMANPoint(std::fmax(rootMax.getX(), hi.getX()), std::fmax(rootMax.getY(), hi.getY()),
                        std::fmax(rootMax.getZ(), hi.getZ()));
    world.add(std::move(box));
  }
  GMANRayBVH bvh;
  bvh.build(world);

  expectSameWork(bvh, GMANPoint(-5.0, 0.0, rootMin.getZ()), GMANVector(1.0, 0.0, kDenormal), GMANVector(1.0, 0.0, 0.0),
                 "denormal: origin on the root's minimum z face");
  expectSameWork(bvh, GMANPoint(-5.0, 0.0, rootMax.getZ()), GMANVector(1.0, 0.0, -kDenormal), GMANVector(1.0, 0.0, 0.0),
                 "denormal: origin on the root's maximum z face");
  expectSameWork(bvh, GMANPoint(rootMin.getX(), 0.0, -5.0), GMANVector(kDenormal, 0.0, 1.0), GMANVector(0.0, 0.0, 1.0),
                 "denormal: matches the exact-zero ray from the root's minimum x face");
  expectSameWork(bvh, GMANPoint(rootMax.getX(), 0.0, -5.0), GMANVector(-kDenormal, 0.0, 1.0), GMANVector(0.0, 0.0, 1.0),
                 "denormal: origin on a leaf's maximum x face");
}

// ---- rays at the extremes ----
std::vector<GMANVector> extremeDirections() {
  RtFloat const slopes[7] = {1e-25f, -1e-25f, 1e-38f, -1e-38f, kDenormal, -kDenormal, 0.0f};
  std::vector<GMANVector> directions;
  for (std::size_t main = 0; main < 3; ++main) {
    for (RtFloat const sign : {1.0f, -1.0f}) {
      for (RtFloat const a : slopes) {
        for (RtFloat const b : slopes) {
          RtFloat component[3];
          component[main] = sign;
          component[(main + 1) % 3] = a;
          component[(main + 2) % 3] = b;
          directions.emplace_back(component[0], component[1], component[2]);
        }
      }
    }
  }
  return directions;
}

std::vector<GMANPoint> extremeOrigins(GMANBBox const& box) {
  GMANPoint const lo = box.getMin();
  GMANPoint const hi = box.getMax();
  RtFloat const low[3] = {lo.getX(), lo.getY(), lo.getZ()};
  RtFloat const high[3] = {hi.getX(), hi.getY(), hi.getZ()};
  std::vector<RtFloat> values[3];
  for (std::size_t axis = 0; axis < 3; ++axis) {
    RtFloat const mid = (low[axis] + high[axis]) * 0.5f;
    for (RtFloat const offset : {-1.0f, -0.25f, 0.0f, 0.25f, 1.0f}) {
      values[axis].push_back(mid + offset);
    }
    values[axis].insert(values[axis].end(), {low[axis] - 0.5f, low[axis], high[axis], high[axis] + 0.5f});
  }
  std::vector<GMANPoint> origins;
  for (RtFloat const x : values[0]) {
    for (RtFloat const y : values[1]) {
      for (RtFloat const z : values[2]) {
        origins.emplace_back(x, y, z);
      }
    }
  }
  return origins;
}

std::vector<GMANRay> extremeRays(GMANBBox const& box, std::mt19937& rng) {
  std::vector<GMANPoint> const origins = extremeOrigins(box);
  std::vector<GMANVector> const directions = extremeDirections();
  std::uniform_int_distribution<std::size_t> pickOrigin(0, origins.size() - 1);
  std::uniform_int_distribution<std::size_t> pickDirection(0, directions.size() - 1);
  RtFloat const reaches[2] = {1e30f, RI_INFINITY};
  std::vector<GMANRay> rays;
  for (std::size_t i = 0; i < kSampledRays; ++i) {
    rays.emplace_back(origins[pickOrigin(rng)], directions[pickDirection(rng)], RI_EPSILON, reaches[i % 2]);
  }
  return rays;
}

bool sameHit(Probe const& a, Probe const& b) {
  return a.found == b.found && (!a.found || (a.hit.t == b.hit.t && a.primitive == b.primitive));
}

// ---- a NaN box stays out of the tree ----
void testNaNBoxStaysOut() {
  GMANWorld world;
  std::vector<GMANRayPrimitive*> valid;
  auto addValid = [&](gman::Owned<GMANRayPrimitive> primitive) {
    valid.push_back(primitive.get());
    world.add(std::move(primitive));
  };
  auto nanBoxOwner = gman::make<NaNBoxPrimitive>();
  auto* const nanBox = nanBoxOwner.get();
  addValid(gman::make<GMANRaySphere>(1.0, -1.0, 1.0, 360.0, GMANParameterList(), translated(0.0, 0.0, 0.0)));
  world.add(std::move(nanBoxOwner));
  addValid(gman::make<GMANRayCylinder>(0.75, -1.0, 1.0, 360.0, GMANParameterList(), translated(3.0, 0.0, 0.0)));
  addValid(gman::make<GMANRaySphere>(1.0, -1.0, 1.0, 360.0, GMANParameterList(), translated(6.0, 0.0, 0.0)));

  GMANRayBVH bvh;
  bvh.build(world);

  GMANBBox const span(GMANPoint(-1.0, -1.0, -1.0), GMANPoint(7.0, 1.0, 1.0));
  std::mt19937 rng(kSeed);
  std::size_t hits = 0;
  std::size_t mismatches = 0;
  std::size_t nanBoxHits = 0;
  for (GMANRay const& ray : extremeRays(span, rng)) {
    Probe const viaTree = probe(bvh, ray);
    Probe scan;
    for (GMANRayPrimitive* primitive : valid) {
      GMANHit candidate;
      if (primitive->intersect(ray, candidate) && (!scan.found || candidate.t < scan.hit.t)) {
        scan.hit = candidate;
        scan.primitive = primitive;
        scan.found = true;
      }
    }
    hits += scan.found ? 1 : 0;
    mismatches += sameHit(viaTree, scan) ? 0 : 1;
    nanBoxHits += viaTree.primitive == nanBox ? 1 : 0;
  }
  check(hits > 0, "NaN box: the family hits the valid primitives");
  check(mismatches == 0, "NaN box: every ray returns the valid primitives' hit");
  check(nanBoxHits == 0, "NaN box: no ray returns the NaN-boxed primitive");
}

std::size_t countWarnings(std::string const& logPath, std::string const& needle) {
  std::ifstream log(logPath);
  std::string line;
  std::size_t count = 0;
  while (std::getline(log, line)) {
    count += line.find(needle) != std::string::npos ? 1 : 0;
  }
  return count;
}

// ---- a build that leaves a NaN box out says so ----
void testNaNBoxWarning() {
  std::string const logPath = "raykernelnan_warning.log";
  std::remove(logPath.c_str());
  setLogFile(logPath.c_str());
  setScreenOutput(false);

  GMANWorld withNaN;
  withNaN.add(gman::make<GMANRaySphere>(1.0, -1.0, 1.0, 360.0, GMANParameterList(), translated(0.0, 0.0, 0.0)));
  withNaN.add(gman::make<NaNBoxPrimitive>());
  withNaN.add(gman::make<GMANRaySphere>(1.0, -1.0, 1.0, 360.0, GMANParameterList(), translated(3.0, 0.0, 0.0)));
  withNaN.add(gman::make<NaNBoxPrimitive>());

  GMANRayBVH bvh;
  bvh.build(withNaN);
  std::string const anyCount = "primitive(s) with a NaN bounding box";
  std::size_t const afterFirst = countWarnings(logPath, anyCount);
  std::size_t const namedTwo = countWarnings(logPath, "2 " + anyCount);
  bvh.build(withNaN);
  std::size_t const afterSecond = countWarnings(logPath, anyCount);

  GMANWorld valid;
  valid.add(gman::make<GMANRaySphere>(1.0, -1.0, 1.0, 360.0, GMANParameterList(), translated(0.0, 0.0, 0.0)));
  valid.add(gman::make<GMANRaySphere>(1.0, -1.0, 1.0, 360.0, GMANParameterList(), translated(3.0, 0.0, 0.0)));
  bvh.build(valid);
  std::size_t const afterValid = countWarnings(logPath, anyCount);

  setLogFile("/dev/null");
  setScreenOutput(true);

  check(afterFirst == 1 && namedTwo == 1, "NaN box warning: one build logs one line naming two primitives");
  check(afterSecond == 2, "NaN box warning: a second build logs a second line");
  check(afterValid == afterSecond, "NaN box warning: a build over valid primitives logs none");
}

bool finiteHit(GMANHit const& hit) {
  RtFloat const fields[] = {hit.t,
                            hit.point.getX(),
                            hit.point.getY(),
                            hit.point.getZ(),
                            hit.normal.getX(),
                            hit.normal.getY(),
                            hit.normal.getZ(),
                            hit.u,
                            hit.v,
                            hit.dPdu.getX(),
                            hit.dPdu.getY(),
                            hit.dPdu.getZ(),
                            hit.dPdv.getX(),
                            hit.dPdv.getY(),
                            hit.dPdv.getZ()};
  for (RtFloat const field : fields) {
    if (!std::isfinite(field)) {
      return false;
    }
  }
  return true;
}

struct Placed {
  std::string name;
  GMANRayPrimitive* primitive;
};

std::vector<Placed> placeEveryIntersector(GMANWorld& world) {
  std::vector<Placed> placed;
  auto add = [&](std::string const& name, gman::Owned<GMANRayPrimitive> primitive) {
    placed.push_back({name, primitive.get()});
    world.add(std::move(primitive));
  };
  RtPoint spanningFrom = {1.0f, 0.0f, -1.0f};
  RtPoint spanningTo = {0.5f, 0.5f, 1.0f};
  RtPoint annulusFrom = {0.25f, 0.0f, 0.5f};
  RtPoint annulusTo = {1.0f, 0.25f, 0.5f};
  std::vector<GMANPoint> const quad = {GMANPoint(79.0, -1.0, 0.0), GMANPoint(81.0, -1.0, 0.0),
                                       GMANPoint(81.0, 1.0, 0.0), GMANPoint(79.0, 1.0, 0.0)};

  add("torus", gman::make<GMANRayTorus>(1.0, 0.25, 0.0, 360.0, 360.0, GMANParameterList(), GMANTransform()));
  add("torus band and wedge",
      gman::make<GMANRayTorus>(1.0, 0.25, -90.0, 90.0, 270.0, GMANParameterList(), translated(5.0, 0.0, 0.0)));
  add("sphere", gman::make<GMANRaySphere>(1.0, -1.0, 1.0, 360.0, GMANParameterList(), translated(10.0, 0.0, 0.0)));
  add("sphere zone and wedge",
      gman::make<GMANRaySphere>(1.0, -0.5, 0.8, 270.0, GMANParameterList(), translated(15.0, 0.0, 0.0)));
  add("cylinder", gman::make<GMANRayCylinder>(1.0, -1.0, 1.0, 360.0, GMANParameterList(), translated(20.0, 0.0, 0.0)));
  add("cylinder wedge",
      gman::make<GMANRayCylinder>(1.0, -1.0, 1.0, 180.0, GMANParameterList(), translated(25.0, 0.0, 0.0)));
  add("cone", gman::make<GMANRayCone>(2.0, 1.0, 360.0, GMANParameterList(), translated(30.0, 0.0, 0.0)));
  add("cone wedge", gman::make<GMANRayCone>(2.0, 1.0, 180.0, GMANParameterList(), translated(35.0, 0.0, 0.0)));
  add("disk", gman::make<GMANRayDisk>(0.5, 1.0, 360.0, GMANParameterList(), translated(40.0, 0.0, 0.0)));
  add("disk wedge", gman::make<GMANRayDisk>(0.5, 1.0, 180.0, GMANParameterList(), translated(45.0, 0.0, 0.0)));
  add("hyperboloid",
      gman::make<GMANRayHyperboloid>(spanningFrom, spanningTo, 360.0, GMANParameterList(), translated(50.0, 0.0, 0.0)));
  add("hyperboloid wedge",
      gman::make<GMANRayHyperboloid>(spanningFrom, spanningTo, 180.0, GMANParameterList(), translated(55.0, 0.0, 0.0)));
  add("hyperboloid annulus",
      gman::make<GMANRayHyperboloid>(annulusFrom, annulusTo, 360.0, GMANParameterList(), translated(60.0, 0.0, 0.0)));
  add("hyperboloid annulus wedge",
      gman::make<GMANRayHyperboloid>(annulusFrom, annulusTo, 180.0, GMANParameterList(), translated(65.0, 0.0, 0.0)));
  add("paraboloid",
      gman::make<GMANRayParaboloid>(1.0, 0.0, 2.0, 360.0, GMANParameterList(), translated(70.0, 0.0, 0.0)));
  add("paraboloid wedge",
      gman::make<GMANRayParaboloid>(1.0, 0.5, 2.0, 180.0, GMANParameterList(), translated(75.0, 0.0, 0.0)));
  add("polygon", gman::make<GMANRayPolygon>(quad, GMANParameterList()));
  return placed;
}

// ---- no hit carries a non-finite field ----
void testNoHitCarriesNonFiniteField() {
  GMANWorld world;
  std::vector<Placed> const placed = placeEveryIntersector(world);
  GMANRayBVH bvh;
  bvh.build(world);

  GMANRay const torusProbe(GMANPoint(-1.0, -1.5, -0.25), GMANVector(1e-25f, 1.0f, 0.0f));
  GMANHit probeHit;
  probeHit.t = kSentinelT;
  bool const probeFound = placed.front().primitive->intersect(torusProbe, probeHit);
  std::printf("torus probe ray: found=%d t=%g\n", (int)probeFound, probeHit.t);
  check(!probeFound || finiteHit(probeHit),
        "torus: the ray from (-1, -1.5, -0.25) along (1e-25, 1, 0) hits finitely or not at all");

  std::mt19937 rng(kSeed);
  for (Placed const& entry : placed) {
    std::vector<GMANRay> const rays = extremeRays(entry.primitive->getBBox(), rng);
    std::size_t direct = 0;
    std::size_t directNonFinite = 0;
    std::size_t viaTree = 0;
    std::size_t viaTreeNonFinite = 0;
    for (GMANRay const& ray : rays) {
      GMANHit hit;
      if (entry.primitive->intersect(ray, hit)) {
        ++direct;
        if (!finiteHit(hit)) {
          if (directNonFinite++ == 0) {
            std::printf("%s: non-finite hit t=%g from origin (%g, %g, %g) direction (%g, %g, %g)\n", entry.name.c_str(),
                        hit.t, ray.getOrigin().getX(), ray.getOrigin().getY(), ray.getOrigin().getZ(),
                        ray.getDirection().getX(), ray.getDirection().getY(), ray.getDirection().getZ());
          }
        }
      }
      Probe const tree = probe(bvh, ray);
      if (tree.found) {
        ++viaTree;
        viaTreeNonFinite += finiteHit(tree.hit) ? 0 : 1;
      }
    }
    std::printf("%s: %zu rays, %zu direct hits, %zu non-finite, %zu tree hits, %zu non-finite\n", entry.name.c_str(),
                rays.size(), direct, directNonFinite, viaTree, viaTreeNonFinite);
    check(direct > 0, entry.name + ": the family hits the primitive");
    check(directNonFinite == 0, entry.name + ": no intersect hit carries a non-finite field");
    check(viaTreeNonFinite == 0, entry.name + ": no nearestHit hit carries a non-finite field");
  }
}

} // namespace

int main() {
  testUntraceableRaysMiss();
  testDenormalComponentIsParallel();
  testNaNBoxStaysOut();
  testNaNBoxWarning();
  testNoHitCarriesNonFiniteField();
  return checkSummary("raykernelnan: ok");
}
