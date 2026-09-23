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
 * R8 proof, §8 C: GMANRayTracer. Depth 4 (this unit's own kMaxTraceDepth,
 * asserted here as the literal 4 since the constant is file-scope in
 * gmanraytracerenderer.cpp, invisible to this translation unit) is the
 * cheapest possible bound; a miss or the depth limit answers background,
 * never black; a single hit's shading matches gman::shade called
 * directly; the self-shadow offset applies to a traced ray exactly as it
 * does to a shadow ray (tests/rayoccluder_test.cpp's own model); and
 * recursion depth is counted correctly.
 */

#include <cmath>
#include <string>

#include "check.h"
#include "gmanlightsourcemgr.h"
#include "gmanlinearworldmanager.h"
#include "gmanmath.h"
#include "gmanparameterlist.h"
#include "gmanpoint.h"
#include "gmanray.h"
#include "gmanraysphere.h"
#include "gmanraytracerenderer.h"
#include "gmanshaderenvironment.h"
#include "gmanshading.h"
#include "gmansurfaceshader.h"
#include "gmantransform.h"
#include "gmanvector.h"
#include "ri.h"

namespace {

constexpr RtFloat kTol = (RtFloat)1.0e-5;

GMANTransform makeTransform(GMANMatrix4 matrix) {
  GMANOneMatrix storage(matrix);
  return GMANTransform(storage);
}

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

// Always the same fixed colour, regardless of se -- a sentinel for
// checks that must never actually reach shading (C.1) or that only care
// whether shading ran at all, not what it computed.
class FixedColorShader : public GMANSurfaceShader {
public:
  explicit FixedColorShader(GMANColor color) : color_(color) {}
  const GMANColor& computeCi(GMANSurfaceEnv& /*se*/) override { return color_; }
  const GMANColor& computeOi(GMANSurfaceEnv& se) override {
    oi_ = se.Os;
    return oi_;
  }

private:
  GMANColor color_;
  GMANColor oi_;
};

// Cs tinted by the illuminance loop's diffuse() -- Ci depends on P/N (via
// diffuse()'s own N.L term) and is neither black nor, for the fixtures
// below, equal to background.
class LambertianTestShader : public GMANSurfaceShader {
public:
  const GMANColor& computeCi(GMANSurfaceEnv& se) override {
    GMANVector const n(se.N.getX(), se.N.getY(), se.N.getZ());
    GMANColor const lit = se.diffuse(n);
    ci_ = GMANColor(se.Cs.getRed() * lit.getRed(), se.Cs.getGreen() * lit.getGreen(), se.Cs.getBlue() * lit.getBlue());
    return ci_;
  }
  const GMANColor& computeOi(GMANSurfaceEnv& se) override {
    oi_ = se.Os;
    return oi_;
  }

private:
  GMANColor ci_;
  GMANColor oi_;
};

// Ci is se.ambient()'s own contribution, scaled by Cs -- constant over
// the whole surface (ambient() has no direction to depend on), so a
// self-hit on this sphere (the only primitive in checkSelfShadowSweep's
// fixture) is unambiguously distinguishable from a genuine escape to a
// distinct background.
class AmbientTestShader : public GMANSurfaceShader {
public:
  const GMANColor& computeCi(GMANSurfaceEnv& se) override {
    GMANColor const lit = se.ambient();
    ci_ = GMANColor(se.Cs.getRed() * lit.getRed(), se.Cs.getGreen() * lit.getGreen(), se.Cs.getBlue() * lit.getBlue());
    return ci_;
  }
  const GMANColor& computeOi(GMANSurfaceEnv& se) override {
    oi_ = se.Os;
    return oi_;
  }

private:
  GMANColor ci_;
  GMANColor oi_;
};

// Reflects I about N and recurses unconditionally, counting its own
// computeCi invocations -- the minimum shader that lets check 5 observe
// GMANRayTracer's own recursion depth from outside.
class RecursionCountingMirror : public GMANSurfaceShader {
public:
  int computeCiCallCount = 0;

  const GMANColor& computeCi(GMANSurfaceEnv& se) override {
    ++computeCiCallCount;
    GMANVector const reflected = se.reflect(se.I, se.N);
    ci_ = se.trace(reflected);
    return ci_;
  }
  const GMANColor& computeOi(GMANSurfaceEnv& se) override {
    oi_ = se.Os;
    return oi_;
  }

private:
  GMANColor ci_;
  GMANColor oi_;
};

// Check 1: a GMANRayTracer built at depth == kMaxTraceDepth (4), trace()ing
// toward a primitive that would be hit, returns exactly background --
// without touching the sentinel shader at all: a mutated depth guard
// would reach it and return the sentinel colour instead.
void checkDepthLimitTermination() {
  GMANLinearWorldManager worldManager;
  GMANRaySphere* sphere = sphereAt(1.0f, 0.0f, 0.0f, 5.0f);
  FixedColorShader shader(GMANColor(0.9f, 0.1f, 0.1f));
  gman::Appearance appearance;
  appearance.shader = &shader;
  appearance.Os = GMANColor(1.0f, 1.0f, 1.0f);
  sphere->setAppearance(appearance);
  worldManager.add(sphere);

  GMANRayBVH bvh;
  bvh.build(worldManager);
  GMANRayOccluder const occluder(bvh);
  GMANMatrix4 const cameraToWorld;
  GMANColor const background(0.2f, 0.2f, 0.8f);
  GMANRayTracer const tracer(bvh, occluder, cameraToWorld, background, 4);

  GMANColor const result = tracer.trace(GMANPoint(0.0f, 0.0f, 0.0f), GMANVector(0.0f, 0.0f, 1.0f), GMANVector());
  check(colorExactly(result, background), "check 1: depth == kMaxTraceDepth returns exactly background");
}

// Check 2: a GMANRayTracer at depth == 0, trace()ing a direction that
// clears every primitive, returns background, not black.
void checkEscapeReturnsBackground() {
  GMANLinearWorldManager worldManager;
  GMANRaySphere* sphere = sphereAt(1.0f, 0.0f, 0.0f, 5.0f);
  FixedColorShader shader(GMANColor(0.9f, 0.1f, 0.1f));
  gman::Appearance appearance;
  appearance.shader = &shader;
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
  GMANColor const result = tracer.trace(GMANPoint(0.0f, 0.0f, 0.0f), GMANVector(0.0f, 0.0f, -1.0f), GMANVector());
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
  GMANLinearWorldManager worldManager;
  GMANRaySphere* sphere = sphereAt(1.0f, 0.0f, 0.0f, 5.0f);
  LambertianTestShader shader;
  gman::Appearance appearance;
  appearance.shader = &shader;
  appearance.Cs = GMANColor(0.8f, 0.3f, 0.2f);
  appearance.Os = GMANColor(1.0f, 1.0f, 1.0f);
  // Direction is light -> scene; (0,0,1) makes toward-light (0,0,-1), the
  // same side as the sphere's own outward normal at the tested hit point
  // (0,0,4), so N.dot(towardLight) > 0 and diffuse() actually lights it.
  GMANLight const distant(GMAN_LIGHT_DISTANT, GMANColor(1.0f, 1.0f, 1.0f), GMANPoint(), GMANVector(0.0f, 0.0f, 1.0f));
  appearance.lights = {&distant};
  sphere->setAppearance(appearance);
  worldManager.add(sphere);

  GMANRayBVH bvh;
  bvh.build(worldManager);
  GMANRayOccluder const occluder(bvh);
  GMANMatrix4 const cameraToWorld;
  GMANColor const background(0.1f, 0.1f, 0.1f);
  GMANRayTracer const tracer(bvh, occluder, cameraToWorld, background, 0);

  GMANPoint const p0(0.0f, 0.0f, 0.0f);
  GMANVector const r0(0.0f, 0.0f, 1.0f);
  GMANColor const traced = tracer.trace(p0, r0, GMANVector());

  GMANRay const ray(p0, r0, RI_EPSILON, RI_INFINITY);
  GMANHit hit;
  GMANRayInterface const* hitPrimitive = nullptr;
  bool const gotHit = bvh.nearestHit(ray, hit, hitPrimitive);
  check(gotHit, "check 3 setup: the hand-built ray hits the sphere");
  if (!gotHit) {
    return;
  }

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
  gman::Shading const direct = gman::shade(hitPrimitive->getAppearance(), point, cameraToWorld, &occluder, nullptr);

  check(colorNear(traced, direct.Ci, kTol),
        "check 3: trace()'s single-hit shading matches gman::shade called directly");
  check(!colorNear(direct.Ci, GMANColor(0.0f, 0.0f, 0.0f), kTol) && !colorNear(direct.Ci, background, kTol),
        "check 3 setup: the fixture's Ci is neither black nor background");
}

// Check 4: swept, per §1's settled decision and rayoccluder_test.cpp's
// own testSelfShadowAtScale model -- real hits from GMANRaySphere::
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
  appearance.shader = &shader;
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
    RtFloat const theta = (RtFloat)ti / (RtFloat)kThetaSteps * 2.0f * (RtFloat)PI;
    for (int pi = 1; pi < kPhiSteps; ++pi) { // skip the poles: theta is undefined there
      RtFloat const phi = (RtFloat)pi / (RtFloat)kPhiSteps * (RtFloat)PI;
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

      GMANColor const result = tracer.trace(hit.point, grazeR, ng);
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
  appearance.shader = &shader;
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
  GMANColor const result = tracer.trace(GMANPoint(0.0f, 0.0f, 0.0f), r0, GMANVector());

  check(shader.computeCiCallCount == 4,
        "check 5: computeCi runs exactly 4 times (depths 0-3), got " + std::to_string(shader.computeCiCallCount));
  check(colorExactly(result, background), "check 5: the fully-bottomed-out chain resolves to exactly background");
}

} // namespace

int main() {
  checkDepthLimitTermination();
  checkEscapeReturnsBackground();
  checkRealHitMatchesDirectShade();
  checkSelfShadowSweepAppliesOffset();
  checkDepthCountsCorrectly();

  return checkSummary("GMANRayTracer: depth-limit termination, background on miss, single-hit shading matches "
                      "gman::shade, self-shadow swept clean, and recursion depth counted correctly");
}
