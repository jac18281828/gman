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
 * GMANRayTracer. Depth 4 (kMaxTraceDepth,
 * asserted here as the literal 4 since the constant is file-scope in
 * gmanraytracerenderer.cpp, invisible to this translation unit) is the
 * cheapest possible bound; a miss or the depth limit answers background,
 * never black; a single hit's shading matches gman::shade called
 * directly; the self-shadow offset applies to a traced ray exactly as it
 * does to a shadow ray (tests/rayoccluder_test.cpp's own model); and
 * recursion depth is counted correctly. Checks 9 to 13 pin the composite of
 * partly opaque layers along a traced ray: each layer adds its colour through
 * the transmission left, the background shows through what remains, a
 * per-channel opacity passes each channel by its own, the composite stops at
 * gman::kMaxCompositeLayers, and an opaque layer casts no ray behind it.
 */

#include <cmath>
#include <memory>
#include <numbers>
#include <string>

#include "check.h"
#include "gmanindirectpass.h"
#include "gmanlightsourcemgr.h"
#include "gmanlinearworldmanager.h"
#include "gmanmath.h"
#include "gmanparameterlist.h"
#include "gmanpoint.h"
#include "gmanray.h"
#include "gmanraybbox.h"
#include "gmanraydisk.h"
#include "gmanrayoccluder.h"
#include "gmanraysphere.h"
#include "gmanraytracerenderer.h"
#include "gmanshaderenvironment.h"
#include "gmanshading.h"
#include "gmansurfaceshader.h"
#include "gmantransform.h"
#include "gmanvector.h"
#include "maketransform.h"
#include "ri.h"

namespace {

constexpr RtFloat kTol = (RtFloat)1.0e-5;

// Checks 7 and 8: the pass's constant enters through one addition along a
// path with no accumulated render error, so 1e-6 -- tighter than kTol's
// 1e-5, sized for the longer chains the other checks trace -- still holds.
constexpr RtFloat kIndirectPassTol = (RtFloat)1.0e-6;

// Checks 9 to 13: every layer is one flat disk wide enough to cover the ray
// at every z the stack reaches, spaced well past the composite's gap.
constexpr RtFloat kLayerRadius = 100.0f;
constexpr RtFloat kLayerSpacing = 1.0f;
constexpr RtFloat kFirstLayerZ = 2.0f;
constexpr RtFloat kThinLayerOpacity = 0.05f;
constexpr int kShallowStackDepth = 8;
constexpr int kDeepStackDepth = 20;
constexpr RtFloat kStackSeparation = 0.01f;

bool colorNear(GMANColor const& a, GMANColor const& b, RtFloat tol) {
  return std::fabs(a.getRed() - b.getRed()) <= tol && std::fabs(a.getGreen() - b.getGreen()) <= tol &&
         std::fabs(a.getBlue() - b.getBlue()) <= tol;
}

bool colorExactly(GMANColor const& a, GMANColor const& b) {
  return a.getRed() == b.getRed() && a.getGreen() == b.getGreen() && a.getBlue() == b.getBlue();
}

// Opaque (Os = white), matching rayoccluder_test.cpp's own sphereAt: a
// bare GMANRayInterface's appearance defaults to black/transparent.
GMANRaySphere* sphereAt(RtFloat radius, RtFloat cx, RtFloat cy, RtFloat cz) {
  GMANMatrix4 place;
  place.trans(cx, cy, cz);
  GMANTransform const transform = makeTransform(place);
  return new GMANRaySphere(radius, -radius, radius, 360.0f, GMANParameterList(), transform);
}

// An arbitrary unit vector perpendicular to n.
GMANVector anyPerpendicular(GMANVector const& n) {
  GMANVector const arbitrary =
      (std::fabs(n.getZ()) < 0.9f) ? GMANVector(0.0f, 0.0f, 1.0f) : GMANVector(1.0f, 0.0f, 0.0f);
  GMANVector t = n.cross(arbitrary);
  t.normalize();
  return t;
}

// Wraps a stack- or member-owned shader for Appearance::shader, which owns
// whatever it holds: this alias shares the caller's own lifetime instead,
// owning nothing and freeing nothing.
std::shared_ptr<GMANSurfaceShader const> asAppearanceShader(GMANSurfaceShader const& shader) {
  return std::shared_ptr<GMANSurfaceShader const>(&shader, [](GMANSurfaceShader const*) {});
}

// Always the same fixed colour, regardless of se -- a sentinel for
// checks that must never actually reach shading (C.1) or that only care
// whether shading ran at all, not what it computed.
class FixedColorShader : public GMANSurfaceShader {
public:
  explicit FixedColorShader(GMANColor color) : color_(color) {}
  GMANColor computeCi(GMANSurfaceEnv const& /*se*/) const override { return color_; }
  GMANColor computeOi(GMANSurfaceEnv const& se) const override { return se.Os; }

private:
  GMANColor color_;
};

// Cs tinted by the illuminance loop's diffuse() -- Ci depends on P/N (via
// diffuse()'s own N.L term) and is neither black nor, for the fixtures
// below, equal to background.
class LambertianTestShader : public GMANSurfaceShader {
public:
  GMANColor computeCi(GMANSurfaceEnv const& se) const override {
    GMANVector const n(se.N.getX(), se.N.getY(), se.N.getZ());
    GMANColor const lit = se.diffuse(n);
    return GMANColor(se.Cs.getRed() * lit.getRed(), se.Cs.getGreen() * lit.getGreen(), se.Cs.getBlue() * lit.getBlue());
  }
  GMANColor computeOi(GMANSurfaceEnv const& se) const override { return se.Os; }
};

// Ci is se.ambient()'s own contribution, scaled by Cs -- constant over
// the whole surface (ambient() has no direction to depend on), so a
// self-hit on this sphere (the only primitive in checkSelfShadowSweep's
// fixture) is unambiguously distinguishable from a genuine escape to a
// distinct background.
class AmbientTestShader : public GMANSurfaceShader {
public:
  GMANColor computeCi(GMANSurfaceEnv const& se) const override {
    GMANColor const lit = se.ambient();
    return GMANColor(se.Cs.getRed() * lit.getRed(), se.Cs.getGreen() * lit.getGreen(), se.Cs.getBlue() * lit.getBlue());
  }
  GMANColor computeOi(GMANSurfaceEnv const& se) const override { return se.Os; }
};

// Reflects I about N and recurses unconditionally, counting its own
// computeCi invocations -- the minimum shader that lets check 5 observe
// GMANRayTracer's own recursion depth from outside.
class RecursionCountingMirror : public GMANSurfaceShader {
public:
  mutable int computeCiCallCount = 0;

  GMANColor computeCi(GMANSurfaceEnv const& se) const override {
    ++computeCiCallCount;
    GMANVector const reflected = se.reflect(se.I, se.N);
    return se.trace(reflected);
  }
  GMANColor computeOi(GMANSurfaceEnv const& se) const override { return se.Os; }
};

// Kr = 1, perfect reflection: Ci is the traced colour along the real
// reflection of I about N, the production formula GMANSurfaceEnv::reflect
// applies.
class PerfectMirrorShader : public GMANSurfaceShader {
public:
  GMANColor computeCi(GMANSurfaceEnv const& se) const override {
    GMANVector const reflected = se.reflect(se.I, se.N);
    return se.trace(reflected);
  }
  GMANColor computeOi(GMANSurfaceEnv const& se) const override { return se.Os; }
};

// Fixed colour, counting its own computeCi invocations: lets check 13 see
// whether a layer behind another was ever shaded.
class CountingColorShader : public GMANSurfaceShader {
public:
  explicit CountingColorShader(GMANColor color) : color_(color) {}
  mutable int computeCiCallCount = 0;

  GMANColor computeCi(GMANSurfaceEnv const& /*se*/) const override {
    ++computeCiCallCount;
    return color_;
  }
  GMANColor computeOi(GMANSurfaceEnv const& se) const override { return se.Os; }

private:
  GMANColor color_;
};

// Answers c unconditionally: proves the indirect-light wiring itself,
// independent of any host's own prepare()/irradiance() contract.
class ConstantIndirectPassFixture : public gman::IndirectPass {
public:
  explicit ConstantIndirectPassFixture(GMANColor const& c) : c_(c) {}
  void prepare(GMANWorldManager& /*world*/, GMANRayOccluder const& /*occluder*/,
               GMANOptions const& /*options*/) override {}
  GMANColor irradiance(GMANHit const& /*hit*/, GMANVector const& /*I*/) const override { return c_; }

private:
  GMANColor c_;
};

// A sphere at (0,0,5) radius 1, Lambertian-shaded, lit by one distant
// light from lightDirection -- shared by checks 3 and 6. bvh and
// occluder are built in place: GMANRayOccluder holds bvh by reference,
// so this fixture is never moved or returned by value, only constructed
// directly where it is used.
class LambertianHitFixture {
public:
  explicit LambertianHitFixture(GMANVector const& lightDirection)
      : occluder(bvh), light_(GMAN_LIGHT_DISTANT, GMANColor(1.0f, 1.0f, 1.0f), GMANPoint(), lightDirection) {
    GMANRaySphere* sphere = sphereAt(1.0f, 0.0f, 0.0f, 5.0f);
    gman::Appearance appearance;
    appearance.shader = asAppearanceShader(shader);
    appearance.Cs = GMANColor(0.8f, 0.3f, 0.2f);
    appearance.Os = GMANColor(1.0f, 1.0f, 1.0f);
    appearance.lights = {&light_};
    sphere->setAppearance(appearance);
    worldManager_.add(sphere);
  }

  // Adds an extra primitive before build(); ownership passes to the
  // fixture's own world manager, matching sphereAt's own convention.
  void addPrimitive(GMANLinearWorldManager::ObjectPtr primitive) { worldManager_.add(primitive); }

  void build() { bvh.build(worldManager_); }

  LambertianTestShader shader;
  GMANRayBVH bvh;
  GMANRayOccluder occluder;

private:
  GMANLinearWorldManager worldManager_;
  GMANLight light_;
};

// The SurfacePoint a bare GMANHit doesn't carry, built the same way
// checks 3 and 6 each need it to call gman::shade directly against a
// hand-built ray's own hit and compare it with what trace() found.
gman::SurfacePoint surfacePointFromHit(GMANHit const& hit, GMANVector const& r0, GMANPoint const& p0) {
  gman::SurfacePoint point;
  point.P = hit.point;
  point.N = GMANNormal(hit.normal.getX(), hit.normal.getY(), hit.normal.getZ());
  point.Ng = point.N;
  point.I = r0;
  point.E = p0;
  point.u = hit.u;
  point.v = hit.v;
  point.s = hit.u;
  point.t = hit.v;
  return point;
}

// Single-hit layers along +z from the origin, each a disk of Os os shaded
// by shader, which the caller keeps alive: what a trace() down +z crosses.
class LayerStack {
public:
  void addLayer(RtFloat z, GMANSurfaceShader const& shader, GMANColor const& os) {
    GMANMatrix4 place;
    place.trans(0.0f, 0.0f, z);
    const auto transform = makeTransform(place);
    const auto disk = new GMANRayDisk(0.0f, kLayerRadius, 360.0f, GMANParameterList(), transform);
    gman::Appearance appearance;
    appearance.shader = asAppearanceShader(shader);
    appearance.Os = os;
    disk->setAppearance(appearance);
    worldManager_.add(disk);
  }

  GMANColor trace(GMANColor const& background) {
    GMANRayBVH bvh;
    bvh.build(worldManager_);
    const auto occluder = GMANRayOccluder(bvh);
    const GMANMatrix4 cameraToWorld;
    const auto tracer = GMANRayTracer(bvh, occluder, cameraToWorld, background, 0);
    return tracer.trace(GMANPoint(0.0f, 0.0f, 0.0f), GMANVector(0.0f, 0.0f, 1.0f), GMANVector(), 0.0f);
  }

private:
  GMANLinearWorldManager worldManager_;
};

GMANColor channelScaled(GMANColor const& a, GMANColor const& b) {
  return GMANColor(a.getRed() * b.getRed(), a.getGreen() * b.getGreen(), a.getBlue() * b.getBlue());
}

GMANColor sum(GMANColor const& a, GMANColor const& b) {
  return GMANColor(a.getRed() + b.getRed(), a.getGreen() + b.getGreen(), a.getBlue() + b.getBlue());
}

// Check 1: a GMANRayTracer built at depth == kMaxTraceDepth (4), trace()ing
// toward a primitive that would be hit, returns exactly background --
// without touching the sentinel shader at all: a mutated depth guard
// would reach it and return the sentinel colour instead.
void checkDepthLimitTermination() {
  GMANLinearWorldManager worldManager;
  GMANRaySphere* sphere = sphereAt(1.0f, 0.0f, 0.0f, 5.0f);
  FixedColorShader shader(GMANColor(0.9f, 0.1f, 0.1f));
  gman::Appearance appearance;
  appearance.shader = asAppearanceShader(shader);
  appearance.Os = GMANColor(1.0f, 1.0f, 1.0f);
  sphere->setAppearance(appearance);
  worldManager.add(sphere);

  GMANRayBVH bvh;
  bvh.build(worldManager);
  GMANRayOccluder const occluder(bvh);
  GMANMatrix4 const cameraToWorld;
  GMANColor const background(0.2f, 0.2f, 0.8f);
  GMANRayTracer const tracer(bvh, occluder, cameraToWorld, background, 4);

  GMANColor const result = tracer.trace(GMANPoint(0.0f, 0.0f, 0.0f), GMANVector(0.0f, 0.0f, 1.0f), GMANVector(), 0.0f);
  check(colorExactly(result, background), "check 1: depth == kMaxTraceDepth returns exactly background");
}

// Check 2: a GMANRayTracer at depth == 0, trace()ing a direction that
// clears every primitive, returns background, not black.
void checkEscapeReturnsBackground() {
  GMANLinearWorldManager worldManager;
  GMANRaySphere* sphere = sphereAt(1.0f, 0.0f, 0.0f, 5.0f);
  FixedColorShader shader(GMANColor(0.9f, 0.1f, 0.1f));
  gman::Appearance appearance;
  appearance.shader = asAppearanceShader(shader);
  appearance.Os = GMANColor(1.0f, 1.0f, 1.0f);
  sphere->setAppearance(appearance);
  worldManager.add(sphere);

  GMANRayBVH bvh;
  bvh.build(worldManager);
  GMANRayOccluder const occluder(bvh);
  GMANMatrix4 const cameraToWorld;
  GMANColor const background(0.2f, 0.2f, 0.8f);
  GMANRayTracer const tracer(bvh, occluder, cameraToWorld, background, 0);

  // Away from the sphere entirely (it sits at +z).
  GMANColor const result = tracer.trace(GMANPoint(0.0f, 0.0f, 0.0f), GMANVector(0.0f, 0.0f, -1.0f), GMANVector(), 0.0f);
  check(colorExactly(result, background), "check 2: a direction clearing every primitive returns exactly background");
  check(!(result.getRed() == 0.0f && result.getGreen() == 0.0f && result.getBlue() == 0.0f),
        "check 2: a miss does not return black");
}

// Check 3: trace() toward a real hit returns the same colour gman::shade
// produces for that hit directly. Ng == the zero vector degrades
// trace()'s own self-shadow offset to none (GMANSurfaceEnv's own
// contract), so the ray trace() casts internally is bit-identical to the
// one built here by hand, and the two shading paths should agree tightly.
void checkRealHitMatchesDirectShade() {
  // Direction is light -> scene; (0,0,1) makes toward-light (0,0,-1), the
  // same side as the sphere's own outward normal at the tested hit point
  // (0,0,4), so N.dot(towardLight) > 0 and diffuse() actually lights it.
  LambertianHitFixture fixture(GMANVector(0.0f, 0.0f, 1.0f));
  fixture.build();

  GMANMatrix4 const cameraToWorld;
  GMANColor const background(0.1f, 0.1f, 0.1f);
  GMANRayTracer const tracer(fixture.bvh, fixture.occluder, cameraToWorld, background, 0);

  GMANPoint const p0(0.0f, 0.0f, 0.0f);
  GMANVector const r0(0.0f, 0.0f, 1.0f);
  const auto traced = tracer.trace(p0, r0, GMANVector(), 0.0f);

  GMANRay const ray(p0, r0, RI_EPSILON, RI_INFINITY);
  GMANHit hit;
  GMANRayInterface const* hitPrimitive = nullptr;
  bool const gotHit = fixture.bvh.nearestHit(ray, hit, hitPrimitive);
  check(gotHit, "check 3 setup: the hand-built ray hits the sphere");
  if (!gotHit) {
    return;
  }

  gman::SurfacePoint const point = surfacePointFromHit(hit, r0, p0);
  gman::Shading const direct =
      gman::shade(hitPrimitive->getAppearance(), point, cameraToWorld, &fixture.occluder, nullptr);

  check(colorNear(traced, direct.Ci, kTol),
        "check 3: trace()'s single-hit shading matches gman::shade called directly");
  check(!colorNear(direct.Ci, GMANColor(0.0f, 0.0f, 0.0f), kTol) && !colorNear(direct.Ci, background, kTol),
        "check 3 setup: the fixture's Ci is neither black nor background");
}

// Check 4: swept, following rayoccluder_test.cpp's own
// testSelfShadowAtScale model -- real hits from GMANRaySphere::
// intersect only, since a hand-placed point never carries the float
// error a real hit's own arithmetic leaves. At each hit, trace() a
// near-grazing, outward direction: the case most likely to clip back
// into the surface without the offset. The sphere is this fixture's only
// primitive and lit ambient (constant Ci), so a self-hit is
// distinguishable from a genuine escape to a distinct background.
void checkSelfShadowSweepAppliesOffset() {
  RtFloat const radius = 3.0f;
  GMANLinearWorldManager worldManager;
  GMANRaySphere* sphere = sphereAt(radius, 0.0f, 0.0f, 0.0f);
  AmbientTestShader shader;
  gman::Appearance appearance;
  appearance.shader = asAppearanceShader(shader);
  appearance.Cs = GMANColor(0.4f, 0.6f, 0.2f);
  appearance.Os = GMANColor(1.0f, 1.0f, 1.0f);
  GMANLight const ambient(GMAN_LIGHT_AMBIENT, GMANColor(1.0f, 1.0f, 1.0f), GMANPoint(), GMANVector());
  appearance.lights = {&ambient};
  sphere->setAppearance(appearance);
  worldManager.add(sphere);

  GMANRayBVH bvh;
  bvh.build(worldManager);
  GMANRayOccluder const occluder(bvh);
  GMANMatrix4 const cameraToWorld;
  GMANColor const background(1.0f, 1.0f, 1.0f); // white, distinct from the sphere's dim ambient Ci
  GMANRayTracer const tracer(bvh, occluder, cameraToWorld, background, 0);

  constexpr int kThetaSteps = 72;
  constexpr int kPhiSteps = 36;
  RtFloat const grazingAngle = (RtFloat)1.0e-3; // radians: Ng.dot(R) = sin(grazingAngle), small and positive
  int sampleCount = 0;
  int selfHitCount = 0;
  int notOutwardCount = 0;

  for (int ti = 0; ti < kThetaSteps; ++ti) {
    RtFloat const theta = (RtFloat)ti / (RtFloat)kThetaSteps * 2.0f * std::numbers::pi_v<RtFloat>;
    for (int pi = 1; pi < kPhiSteps; ++pi) { // skip the poles: theta is undefined there
      RtFloat const phi = (RtFloat)pi / (RtFloat)kPhiSteps * std::numbers::pi_v<RtFloat>;
      GMANVector const dir((RtFloat)(std::sin(phi) * std::cos(theta)), (RtFloat)(std::sin(phi) * std::sin(theta)),
                           (RtFloat)std::cos(phi));
      GMANPoint const origin(dir.getX() * 5.0f * radius, dir.getY() * 5.0f * radius, dir.getZ() * 5.0f * radius);
      GMANVector const direction(origin, GMANPoint(0.0f, 0.0f, 0.0f));
      GMANRay const ray(origin, direction);
      GMANHit hit;
      if (!sphere->intersect(ray, hit)) {
        continue;
      }
      ++sampleCount;

      GMANVector const ng(hit.normal.getX(), hit.normal.getY(), hit.normal.getZ());
      GMANVector const tangent = anyPerpendicular(ng);
      GMANVector grazeR = tangent * std::cos(grazingAngle) + ng * std::sin(grazingAngle);
      grazeR.normalize();
      if (ng.dot(grazeR) <= 0.0f) {
        ++notOutwardCount;
        continue;
      }

      RtFloat const magnitude = gman::primitiveMagnitude(sphere->getBBox());
      GMANColor const result = tracer.trace(hit.point, grazeR, ng, magnitude);
      if (!colorNear(result, background, kTol)) {
        ++selfHitCount;
      }
    }
  }

  check(sampleCount > 500, "check 4: enough samples were gathered to trust a zero count");
  check(notOutwardCount == 0, "check 4 setup: every swept direction is outward (Ng.dot(R) > 0)");
  check(selfHitCount == 0, "check 4: every near-grazing outward trace() escapes to background, no self-hits (" +
                               std::to_string(selfHitCount) + "/" + std::to_string(sampleCount) + " self-hit)");
}

// Check 5: a depth-0 GMANRayTracer.trace() call toward a mirror-shaded
// sphere, starting at its own centre -- every direction from dead centre
// hits the sphere at exactly normal incidence, so reflect() sends the
// ray straight back along the diameter it came in on, bouncing between
// two antipodal points forever (a closed scene: every trace() call is
// guaranteed to hit something). computeCi must run exactly 4 times
// (depths 0-3) before the depth-4 trace() call short-circuits.
void checkDepthCountsCorrectly() {
  RtFloat const radius = 5.0f;
  GMANLinearWorldManager worldManager;
  GMANRaySphere* sphere = sphereAt(radius, 0.0f, 0.0f, 0.0f);
  RecursionCountingMirror shader;
  gman::Appearance appearance;
  appearance.shader = asAppearanceShader(shader);
  appearance.Os = GMANColor(1.0f, 1.0f, 1.0f);
  sphere->setAppearance(appearance);
  worldManager.add(sphere);

  GMANRayBVH bvh;
  bvh.build(worldManager);
  GMANRayOccluder const occluder(bvh);
  GMANMatrix4 const cameraToWorld;
  GMANColor const background(1.0f, 1.0f, 0.0f);
  GMANRayTracer const tracer(bvh, occluder, cameraToWorld, background, 0);

  GMANVector r0(0.6f, 0.3f, 0.75f);
  r0.normalize();
  GMANColor const result = tracer.trace(GMANPoint(0.0f, 0.0f, 0.0f), r0, GMANVector(), 0.0f);

  check(shader.computeCiCallCount == 4,
        "check 5: computeCi runs exactly 4 times (depths 0-3), got " + std::to_string(shader.computeCiCallCount));
  check(colorExactly(result, background), "check 5: the fully-bottomed-out chain resolves to exactly background");
}

// Check 6: trace()'s own nested shade() call threads the occluder
// through, not just returns a hit's unshadowed colour. A blocker sphere
// sits on the shadow ray from the traced hit toward its one light, off
// the primary ray's own axis so it cannot occlude that ray too, only
// the shadow ray to the light. LambertianTestShader has no ambient
// term, so a fully blocked light leaves Ci exactly black; a mutation
// that dropped the occluder from trace()'s own nested shade() call
// would light the hit unshadowed instead, matching this fixture's own
// without-occluder reference below.
void checkNestedOccluderShadowsBlocker() {
  // lightDirection (0.8, 0, 0.6): towardLight = (-0.8, 0, -0.6), off the
  // primary ray's z-axis, so the shadow ray from the hit point (0,0,4)
  // and the blocker placed on it never coincide with the primary ray
  // from the origin to that hit.
  LambertianHitFixture fixture(GMANVector(0.8f, 0.0f, 0.6f));

  // On the shadow ray from (0,0,4) toward towardLight, at parameter 5:
  // (0,0,4) + 5*(-0.8,0,-0.6) = (-4,0,1). Distance from the primary
  // ray's own line (the z-axis) is 4, well clear of this blocker's own
  // radius 1.
  GMANRaySphere* blocker = sphereAt(1.0f, -4.0f, 0.0f, 1.0f);
  FixedColorShader blockerShader(GMANColor(0.0f, 0.0f, 0.0f));
  gman::Appearance blockerAppearance;
  blockerAppearance.shader = asAppearanceShader(blockerShader);
  blockerAppearance.Os = GMANColor(1.0f, 1.0f, 1.0f); // fully opaque
  blocker->setAppearance(blockerAppearance);
  fixture.addPrimitive(blocker);
  fixture.build();

  GMANMatrix4 const cameraToWorld;
  GMANColor const background(0.1f, 0.1f, 0.1f);
  GMANRayTracer const tracer(fixture.bvh, fixture.occluder, cameraToWorld, background, 0);

  GMANPoint const p0(0.0f, 0.0f, 0.0f);
  GMANVector const r0(0.0f, 0.0f, 1.0f);
  const auto traced = tracer.trace(p0, r0, GMANVector(), 0.0f);

  GMANRay const ray(p0, r0, RI_EPSILON, RI_INFINITY);
  GMANHit hit;
  GMANRayInterface const* hitPrimitive = nullptr;
  bool const gotHit = fixture.bvh.nearestHit(ray, hit, hitPrimitive);
  check(gotHit, "check 6 setup: the hand-built ray hits the sphere, not the blocker");
  if (!gotHit) {
    return;
  }

  gman::SurfacePoint const point = surfacePointFromHit(hit, r0, p0);

  gman::Shading const withOccluder =
      gman::shade(hitPrimitive->getAppearance(), point, cameraToWorld, &fixture.occluder, nullptr);
  gman::Shading const withoutOccluder =
      gman::shade(hitPrimitive->getAppearance(), point, cameraToWorld, nullptr, nullptr);

  check(colorExactly(withOccluder.Ci, GMANColor(0.0f, 0.0f, 0.0f)),
        "check 6 setup: the blocker leaves the occluded reference exactly black (no ambient term)");
  check(!colorNear(withoutOccluder.Ci, GMANColor(0.0f, 0.0f, 0.0f), kTol),
        "check 6 setup: the same hit is clearly lit once the occluder is dropped");

  check(colorNear(traced, withOccluder.Ci, kTol),
        "check 6: trace()'s nested shade() call applies the occluder, matching the shadowed reference");
  check(!colorNear(traced, withoutOccluder.Ci, kTol),
        "check 6: trace()'s result does not match the unshadowed reference");
}

// The renderer-path check: a mutation leaving hitSurfacePoint's own
// surfaceMagnitude at 0 must fail this. A radius-1000 sphere placed so its
// hits sit at max|P| ~ 5 (M ~ 2005, the large-primitive-row fixture's own
// placement), lit from a steep graze so N.L stays small and positive --
// the case most sensitive to a wrong (too small) magnitude. Every lit hit,
// traced through a real GMANRayOccluder via GMANRayTracer::trace, must
// return the same colour gman::shade gives the same hit with no occluder
// at all: the sphere is the scene's only primitive, so a correct offset
// never lets the occluder find anything to shadow.
void checkLargeSphereRendererPathAppliesMagnitude() {
  RtFloat const radius = 1000.0f;
  RtFloat const centreZ = radius + 5.0f;
  GMANLinearWorldManager worldManager;
  GMANRaySphere* sphere = sphereAt(radius, 0.0f, 0.0f, centreZ);
  LambertianTestShader shader;
  gman::Appearance appearance;
  appearance.shader = asAppearanceShader(shader);
  appearance.Cs = GMANColor(0.8f, 0.3f, 0.2f);
  appearance.Os = GMANColor(1.0f, 1.0f, 1.0f);

  // 87 degrees off the near pole's own normal -- a strongly grazing
  // shadow ray, the case selfshadowsweep_test.cpp's own large-primitive
  // row uses too.
  RtFloat const grazeAngle = GMANRadians(87.0f);
  GMANVector const towardLight((RtFloat)std::sin(grazeAngle), 0.0f, (RtFloat)-std::cos(grazeAngle));
  GMANLight const light(GMAN_LIGHT_DISTANT, GMANColor(1.0f, 1.0f, 1.0f), GMANPoint(),
                        GMANVector(-towardLight.getX(), -towardLight.getY(), -towardLight.getZ()));
  appearance.lights = {&light};
  sphere->setAppearance(appearance);
  worldManager.add(sphere);

  GMANRayBVH bvh;
  bvh.build(worldManager);
  GMANRayOccluder const occluder(bvh);
  GMANMatrix4 const cameraToWorld;
  GMANColor const background(1.0f, 1.0f, 0.0f); // never reached: every sampled ray hits the sphere
  GMANRayTracer const tracer(bvh, occluder, cameraToWorld, background, 0);

  GMANPoint const p0(0.0f, 0.0f, 0.0f);
  constexpr int kThetaSteps = 36;
  constexpr int kConeSteps = 12;
  // Keeps every sampled hit's own max|P| close to 5, the large-primitive
  // row's own reasoning.
  constexpr RtFloat kMaxConeAngle = (RtFloat)0.05;

  int checked = 0;
  int mismatched = 0;
  for (int ti = 0; ti < kThetaSteps; ++ti) {
    RtFloat const theta = (RtFloat)ti / (RtFloat)kThetaSteps * 2.0f * std::numbers::pi_v<RtFloat>;
    for (int ci = 1; ci <= kConeSteps; ++ci) {
      RtFloat const phi = (RtFloat)ci / (RtFloat)kConeSteps * kMaxConeAngle;
      GMANVector const r0((RtFloat)(std::sin(phi) * std::cos(theta)), (RtFloat)(std::sin(phi) * std::sin(theta)),
                          (RtFloat)std::cos(phi));
      GMANHit hit;
      if (!sphere->intersect(GMANRay(p0, r0), hit)) {
        continue;
      }
      ++checked;

      // Ng == zero: p0 is a free camera-space point, not on any surface
      // (GMANRayTracer::trace's own contract), so this offsets nothing.
      const auto traced = tracer.trace(p0, r0, GMANVector(), 0.0f);

      gman::SurfacePoint const point = surfacePointFromHit(hit, r0, p0);
      gman::Shading const withoutOccluder =
          gman::shade(sphere->getAppearance(), point, cameraToWorld, nullptr, nullptr);
      if (!colorNear(traced, withoutOccluder.Ci, kTol)) {
        ++mismatched;
      }
    }
  }

  check(checked >= 40, "renderer-path check: enough hits were gathered to trust a zero count");
  check(mismatched == 0,
        "renderer-path check: every traced hit through a real occluder matches the same shading with none, "
        "the only primitive being the sphere itself (" +
            std::to_string(mismatched) + "/" + std::to_string(checked) + " mismatched)");
}

// Check 7: a GMANRayTracer bound to an indirect-light pass answering c
// traces into a matte sphere (Ka = 1, Cs = Os = 1, ambient()-only since no
// light is bound) and returns exactly c above the same trace with no pass
// bound -- the primary-hit wiring GMANRayTracer::trace's own
// gman::shade call carries.
void checkIndirectPassReachesPrimaryHit() {
  GMANLinearWorldManager worldManager;
  GMANRaySphere* sphere = sphereAt(1.0f, 0.0f, 0.0f, 5.0f);
  AmbientTestShader shader;
  gman::Appearance appearance;
  appearance.shader = asAppearanceShader(shader);
  appearance.Cs = GMANColor(1.0f, 1.0f, 1.0f);
  appearance.Os = GMANColor(1.0f, 1.0f, 1.0f);
  sphere->setAppearance(appearance);
  worldManager.add(sphere);

  GMANRayBVH bvh;
  bvh.build(worldManager);
  GMANRayOccluder const occluder(bvh);
  GMANMatrix4 const cameraToWorld;
  GMANColor const background(0.1f, 0.1f, 0.1f);
  GMANColor const c(0.05f, 0.07f, 0.09f);
  ConstantIndirectPassFixture const pass(c);

  GMANRayTracer const unbound(bvh, occluder, cameraToWorld, background, 0);
  GMANRayTracer const bound(bvh, occluder, cameraToWorld, background, 0, &pass);

  GMANPoint const p0(0.0f, 0.0f, 0.0f);
  GMANVector const r0(0.0f, 0.0f, 1.0f);
  GMANColor const unboundResult = unbound.trace(p0, r0, GMANVector(), 0.0f);
  GMANColor const boundResult = bound.trace(p0, r0, GMANVector(), 0.0f);

  GMANColor delta = boundResult;
  delta -= unboundResult;
  check(colorNear(delta, c, kIndirectPassTol),
        "check 7: a primary hit through a bound indirect-light pass differs from the unbound trace by exactly c");
}

// Check 8: a mirror hit's own reflected ray, cast by GMANRayTracer::trace's
// child tracer, reaches a second sphere -- proof the pass reaches through
// the child tracer, not only the depth-0 one. The reflection direction is
// computed the same way GMANReflect does (gmanslapi.cpp), and the second
// sphere is placed exactly along it, so the geometry needs no
// hand-solved placement.
void checkIndirectPassReachesChildTracer() {
  // A throwaway probe, never added to a world manager, purely to find
  // where the primary ray will hit the mirror sphere before the real
  // fixture below is built around that hit.
  std::unique_ptr<GMANRaySphere> const mirrorProbe(sphereAt(1.0f, 0.3f, 0.0f, 5.0f));
  GMANPoint const p0(0.0f, 0.0f, 0.0f);
  GMANVector const r0(0.0f, 0.0f, 1.0f);
  GMANRay const primaryRay(p0, r0);
  GMANHit mirrorHit;
  check(mirrorProbe->intersect(primaryRay, mirrorHit), "check 8 setup: the primary ray hits the mirror sphere");

  GMANVector const n(mirrorHit.normal.getX(), mirrorHit.normal.getY(), mirrorHit.normal.getZ());
  GMANVector reflected = r0 - n * (r0.dot(n) * 2.0f);
  reflected.normalize();

  RtFloat const farDistance = 8.0f;
  RtFloat const farRadius = 2.0f;
  GMANPoint const farCentre(mirrorHit.point.getX() + reflected.getX() * farDistance,
                            mirrorHit.point.getY() + reflected.getY() * farDistance,
                            mirrorHit.point.getZ() + reflected.getZ() * farDistance);

  GMANLinearWorldManager worldManager;
  GMANRaySphere* mirrorSphere = sphereAt(1.0f, 0.3f, 0.0f, 5.0f);
  PerfectMirrorShader mirrorShader;
  gman::Appearance mirrorAppearance;
  mirrorAppearance.shader = asAppearanceShader(mirrorShader);
  mirrorAppearance.Os = GMANColor(1.0f, 1.0f, 1.0f);
  mirrorSphere->setAppearance(mirrorAppearance);
  worldManager.add(mirrorSphere);

  GMANRaySphere* farSphere = sphereAt(farRadius, farCentre.getX(), farCentre.getY(), farCentre.getZ());
  AmbientTestShader farShader;
  gman::Appearance farAppearance;
  farAppearance.shader = asAppearanceShader(farShader);
  farAppearance.Cs = GMANColor(1.0f, 1.0f, 1.0f);
  farAppearance.Os = GMANColor(1.0f, 1.0f, 1.0f);
  farSphere->setAppearance(farAppearance);
  worldManager.add(farSphere);

  GMANRayBVH bvh;
  bvh.build(worldManager);
  GMANRayOccluder const occluder(bvh);
  GMANMatrix4 const cameraToWorld;
  GMANColor const background(0.1f, 0.1f, 0.1f);
  GMANColor const c(0.05f, 0.07f, 0.09f);
  ConstantIndirectPassFixture const pass(c);

  GMANRayTracer const unbound(bvh, occluder, cameraToWorld, background, 0);
  GMANRayTracer const bound(bvh, occluder, cameraToWorld, background, 0, &pass);

  GMANColor const unboundResult = unbound.trace(p0, r0, GMANVector(), 0.0f);
  GMANColor const boundResult = bound.trace(p0, r0, GMANVector(), 0.0f);

  GMANColor delta = boundResult;
  delta -= unboundResult;
  check(colorNear(delta, c, kIndirectPassTol),
        "check 8: a mirror's reflected hit, shaded by the child tracer, differs by exactly c -- the pass reaches "
        "through the child tracer");
}

// Check 9: a layer of Os 0.5 and colour C before an opaque wall of colour W
// returns C + 0.5 W: the layer passes half of what lies behind it.
void checkPartlyOpaqueLayerShowsWallBehind() {
  const auto layerColor = GMANColor(0.3f, 0.2f, 0.1f);
  const auto wallColor = GMANColor(0.2f, 0.4f, 0.6f);
  FixedColorShader layerShader(layerColor);
  FixedColorShader wallShader(wallColor);
  LayerStack stack;
  stack.addLayer(kFirstLayerZ, layerShader, GMANColor(0.5f, 0.5f, 0.5f));
  stack.addLayer(kFirstLayerZ + kLayerSpacing, wallShader, GMANColor(1.0f, 1.0f, 1.0f));

  const auto traced = stack.trace(GMANColor(0.1f, 0.1f, 0.1f));
  const auto expected = sum(layerColor, channelScaled(GMANColor(0.5f, 0.5f, 0.5f), wallColor));
  check(colorNear(traced, expected, kTol), "check 9: a layer of Os 0.5 before an opaque wall returns C + 0.5 W");
}

// Check 10: the same layer before nothing returns C + 0.5 background: the
// ray that leaves the scene reads the layer over the background a miss
// returns.
void checkPartlyOpaqueLayerOverBackground() {
  const auto layerColor = GMANColor(0.3f, 0.2f, 0.1f);
  const auto background = GMANColor(0.1f, 0.5f, 0.9f);
  FixedColorShader layerShader(layerColor);
  LayerStack stack;
  stack.addLayer(kFirstLayerZ, layerShader, GMANColor(0.5f, 0.5f, 0.5f));

  const auto traced = stack.trace(background);
  const auto expected = sum(layerColor, channelScaled(GMANColor(0.5f, 0.5f, 0.5f), background));
  check(colorNear(traced, expected, kTol), "check 10: a layer of Os 0.5 before nothing returns C + 0.5 background");
}

// Check 11: a layer of per-channel Os (0.5, 0.25, 0.75) before the wall
// returns C + (0.5, 0.75, 0.25) W channel by channel.
void checkPerChannelOpacityComposites() {
  const auto layerColor = GMANColor(0.3f, 0.2f, 0.1f);
  const auto wallColor = GMANColor(0.2f, 0.4f, 0.6f);
  const auto passed = GMANColor(0.5f, 0.75f, 0.25f);
  FixedColorShader layerShader(layerColor);
  FixedColorShader wallShader(wallColor);
  LayerStack stack;
  stack.addLayer(kFirstLayerZ, layerShader, GMANColor(0.5f, 0.25f, 0.75f));
  stack.addLayer(kFirstLayerZ + kLayerSpacing, wallShader, GMANColor(1.0f, 1.0f, 1.0f));

  const auto traced = stack.trace(GMANColor(0.1f, 0.1f, 0.1f));
  const auto expected = sum(layerColor, channelScaled(passed, wallColor));
  check(colorNear(traced, expected, kTol),
        "check 11: a layer of per-channel Os (0.5, 0.25, 0.75) returns C + (0.5, 0.75, 0.25) W channel by channel");
}

GMANColor traceThinStack(int layerCount) {
  FixedColorShader shader(GMANColor(0.1f, 0.05f, 0.02f));
  LayerStack stack;
  for (int layer = 0; layer < layerCount; ++layer) {
    stack.addLayer(kFirstLayerZ + static_cast<RtFloat>(layer) * kLayerSpacing, shader,
                   GMANColor(kThinLayerOpacity, kThinLayerOpacity, kThinLayerOpacity));
  }
  return stack.trace(GMANColor(0.1f, 0.2f, 0.3f));
}

// Check 12: a stack of Os 0.05 layers 20 deep returns what a stack 16 deep
// returns and differs from a stack 8 deep, so trace() composites no more
// than gman::kMaxCompositeLayers layers and no fewer.
void checkTraceObeysLayerCap() {
  const auto shallow = traceThinStack(kShallowStackDepth);
  const auto atCap = traceThinStack(gman::kMaxCompositeLayers);
  const auto deep = traceThinStack(kDeepStackDepth);
  check(colorNear(deep, atCap, kTol), "check 12: a 20-layer stack returns what a 16-layer stack returns");
  check(!colorNear(shallow, atCap, kStackSeparation), "check 12: an 8-layer stack differs from a 16-layer one");
}

// Check 13: a counting shader behind an opaque layer is never shaded, and
// behind a layer of Os 0.5 is shaded once.
void checkOpaqueLayerStopsTheComposite() {
  const auto white = GMANColor(1.0f, 1.0f, 1.0f);
  FixedColorShader frontShader(GMANColor(0.3f, 0.2f, 0.1f));

  CountingColorShader behindOpaque(GMANColor(0.2f, 0.4f, 0.6f));
  LayerStack opaqueStack;
  opaqueStack.addLayer(kFirstLayerZ, frontShader, white);
  opaqueStack.addLayer(kFirstLayerZ + kLayerSpacing, behindOpaque, white);
  opaqueStack.trace(GMANColor(0.1f, 0.1f, 0.1f));
  check(behindOpaque.computeCiCallCount == 0, "check 13: a shader behind an opaque layer is never shaded, got " +
                                                  std::to_string(behindOpaque.computeCiCallCount));

  CountingColorShader behindPartlyOpaque(GMANColor(0.2f, 0.4f, 0.6f));
  LayerStack partlyOpaqueStack;
  partlyOpaqueStack.addLayer(kFirstLayerZ, frontShader, GMANColor(0.5f, 0.5f, 0.5f));
  partlyOpaqueStack.addLayer(kFirstLayerZ + kLayerSpacing, behindPartlyOpaque, white);
  partlyOpaqueStack.trace(GMANColor(0.1f, 0.1f, 0.1f));
  check(behindPartlyOpaque.computeCiCallCount == 1, "check 13: a shader behind a layer of Os 0.5 is shaded once, got " +
                                                        std::to_string(behindPartlyOpaque.computeCiCallCount));
}

} // namespace

int main() {
  checkDepthLimitTermination();
  checkEscapeReturnsBackground();
  checkRealHitMatchesDirectShade();
  checkSelfShadowSweepAppliesOffset();
  checkDepthCountsCorrectly();
  checkNestedOccluderShadowsBlocker();
  checkLargeSphereRendererPathAppliesMagnitude();
  checkIndirectPassReachesPrimaryHit();
  checkIndirectPassReachesChildTracer();
  checkPartlyOpaqueLayerShowsWallBehind();
  checkPartlyOpaqueLayerOverBackground();
  checkPerChannelOpacityComposites();
  checkTraceObeysLayerCap();
  checkOpaqueLayerStopsTheComposite();

  return checkSummary("GMANRayTracer: depth-limit termination, background on miss, single-hit shading matches "
                      "gman::shade, self-shadow swept clean, recursion depth counted correctly, the nested "
                      "shade() call applies the occluder, a bound indirect-light pass reaches both a primary "
                      "hit and a child tracer's own reflected hit, and partly opaque layers composite front to "
                      "back over the background up to the layer cap");
}
