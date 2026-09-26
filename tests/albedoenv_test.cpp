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
 * gman::hitSurfacePoint's own field mapping off a real BVH hit, and
 * gman::albedo's parity with gman::shade's own Ci at the same point, for a
 * hit on a sphere and on a polygon carrying "st". A null shader answers
 * its own Cs clamped to [0, 1].
 */

#include <array>
#include <cmath>
#include <cstddef>
#include <memory>
#include <vector>

#include "check.h"
#include "gmancolor.h"
#include "gmanlinearworldmanager.h"
#include "gmanmatrix4.h"
#include "gmanparameterlist.h"
#include "gmanpoint.h"
#include "gmanpolygon.h"
#include "gmanray.h"
#include "gmanraybbox.h"
#include "gmanraybvh.h"
#include "gmanraypolygon.h"
#include "gmanraysphere.h"
#include "gmanshaderenvironment.h"
#include "gmanshading.h"
#include "gmansurfaceshader.h"
#include "gmantransform.h"
#include "gmanvector.h"
#include "ri.h"

namespace {

// The parity check's own tolerance: both calls fill a GMANSurfaceEnv
// through the identical helper (decision 6), so the two probes agree
// exactly but for the float rounding one extra copy could introduce.
constexpr RtFloat kParityTol = (RtFloat)1e-6;

bool near(RtFloat a, RtFloat b, RtFloat tol) { return std::fabs(a - b) <= tol; }

bool vectorExactly(GMANVector const& a, GMANVector const& b) {
  return a.getX() == b.getX() && a.getY() == b.getY() && a.getZ() == b.getZ();
}

bool pointExactly(GMANPoint const& a, GMANPoint const& b) {
  return a.getX() == b.getX() && a.getY() == b.getY() && a.getZ() == b.getZ();
}

bool colorNear(GMANColor const& a, GMANColor const& b, RtFloat tol) {
  return near(a.getRed(), b.getRed(), tol) && near(a.getGreen(), b.getGreen(), tol) &&
         near(a.getBlue(), b.getBlue(), tol);
}

GMANTransform identityTransform() {
  GMANMatrix4 matrix;
  GMANOneMatrix storage(matrix);
  return GMANTransform(storage);
}

// Wraps a stack- or member-owned shader for Appearance::shader, which owns
// whatever it holds: this alias shares the caller's own lifetime instead,
// owning nothing and freeing nothing.
std::shared_ptr<GMANSurfaceShader const> asAppearanceShader(GMANSurfaceShader const& shader) {
  return std::shared_ptr<GMANSurfaceShader const>(&shader, [](GMANSurfaceShader const*) {});
}

// Every field gman::shade's own fill sets from a point and an appearance
// (not the lights vector, cameraToWorld or textureCache, which shade holds
// fixed regardless of the point): Cs, Os, P, N, Ng, I, E, u, v, s, t and
// surfaceMagnitude, 26 scalars in all.
std::array<double, 26> envFeatures(GMANSurfaceEnv const& se) {
  return {se.Cs.getRed(),       se.Cs.getGreen(),
          se.Cs.getBlue(),      se.Os.getRed(),
          se.Os.getGreen(),     se.Os.getBlue(),
          (double)se.P.getX(),  (double)se.P.getY(),
          (double)se.P.getZ(),  (double)se.N.getX(),
          (double)se.N.getY(),  (double)se.N.getZ(),
          (double)se.Ng.getX(), (double)se.Ng.getY(),
          (double)se.Ng.getZ(), (double)se.I.getX(),
          (double)se.I.getY(),  (double)se.I.getZ(),
          (double)se.E.getX(),  (double)se.E.getY(),
          (double)se.E.getZ(),  (double)se.u,
          (double)se.v,         (double)se.s,
          (double)se.t,         (double)se.surfaceMagnitude};
}

// Three independent linear combinations of every feature above, so a
// mismatch in any single field -- s and t included -- moves at least one
// channel.
GMANColor probe(GMANSurfaceEnv const& se) {
  std::array<double, 26> const f = envFeatures(se);
  double red = 0, green = 0, blue = 0;
  for (std::size_t i = 0; i < f.size(); ++i) {
    red += f[i] * (double)(i + 1);
    green += f[i] * (double)(f.size() - i);
    blue += f[i] * (double)((i % 5) + 1);
  }
  return GMANColor((RtFloat)red, (RtFloat)green, (RtFloat)blue);
}

// Answers probe(se) from both computeCi and albedo, so a hit's shaded Ci
// and its own albedo are one function of every env field.
class EnvProbeShader : public GMANSurfaceShader {
public:
  GMANColor computeCi(GMANSurfaceEnv const& se) const override { return probe(se); }
  GMANColor computeOi(GMANSurfaceEnv const& se) const override { return se.Os; }
  GMANColor albedo(GMANSurfaceEnv const& se) const override { return probe(se); }
};

// ---- check 1: hitSurfacePoint's own field mapping off a real BVH hit ----
void testHitSurfacePointFieldMapping() {
  GMANLinearWorldManager world;
  world.add(new GMANRaySphere(1.0f, -1.0f, 1.0f, 360.0f, GMANParameterList(), identityTransform()));
  GMANRayBVH bvh;
  bvh.build(world);

  GMANRay const ray(GMANPoint(0.2f, 0.3f, -5.0f), GMANVector(0.0f, -0.05f, 1.0f));
  GMANHit hit;
  GMANRayInterface const* hitPrimitive = nullptr;
  check(bvh.nearestHit(ray, hit, hitPrimitive), "field mapping: the ray hits the sphere");
  if (hitPrimitive == nullptr) {
    return;
  }

  gman::SurfacePoint const point = gman::hitSurfacePoint(ray, hit);
  check(pointExactly(point.P, hit.point), "field mapping: P is the hit's own point");
  check(vectorExactly(GMANVector(point.N.getX(), point.N.getY(), point.N.getZ()), hit.normal),
        "field mapping: N is the hit's own normal");
  check(point.N.getX() == point.Ng.getX() && point.N.getY() == point.Ng.getY() && point.N.getZ() == point.Ng.getZ(),
        "field mapping: Ng equals N");
  check(point.u == hit.u && point.v == hit.v, "field mapping: u and v are the hit's own");
  check(point.s == hit.u && point.t == hit.v, "field mapping: s and t default to u and v");
  check(vectorExactly(point.I, ray.getDirection()), "field mapping: I is the ray's own direction");
  check(pointExactly(point.E, ray.getOrigin()), "field mapping: E is the ray's own origin");
  check(point.surfaceMagnitude == gman::primitiveMagnitude(hitPrimitive->getBBox()),
        "field mapping: surfaceMagnitude is the hit primitive's own");
}

// The two GMANColor answers gman::albedo and gman::shade give at the same
// hit: albedo(appearance, point, cameraToWorld) and shade's own Ci called
// with no occluder, tracer or indirect.
struct ParityResult {
  GMANColor albedoAnswer;
  GMANColor shadeCi;
};

ParityResult parityAt(gman::Appearance const& appearance, gman::SurfacePoint const& point,
                      GMANMatrix4 const& cameraToWorld) {
  ParityResult result;
  result.albedoAnswer = gman::albedo(appearance, point, cameraToWorld);
  result.shadeCi = gman::shade(appearance, point, cameraToWorld).Ci;
  return result;
}

// ---- check 2: albedo/shade parity on a sphere hit ----
void testParityOnSphere() {
  EnvProbeShader const shader;
  gman::Appearance appearance;
  appearance.shader = asAppearanceShader(shader);
  appearance.Cs = GMANColor(0.6f, 0.3f, 0.9f);
  appearance.Os = GMANColor(1.0f, 1.0f, 1.0f);

  GMANLinearWorldManager world;
  world.add(new GMANRaySphere(1.0f, -1.0f, 1.0f, 360.0f, GMANParameterList(), identityTransform()));
  GMANRayBVH bvh;
  bvh.build(world);

  GMANRay const ray(GMANPoint(0.15f, -0.2f, -5.0f), GMANVector(0.03f, 0.02f, 1.0f));
  GMANHit hit;
  GMANRayInterface const* hitPrimitive = nullptr;
  check(bvh.nearestHit(ray, hit, hitPrimitive), "sphere parity: the ray hits the sphere");
  if (hitPrimitive == nullptr) {
    return;
  }

  gman::SurfacePoint const point = gman::hitSurfacePoint(ray, hit);
  GMANMatrix4 const cameraToWorld;
  ParityResult const result = parityAt(appearance, point, cameraToWorld);
  check(colorNear(result.albedoAnswer, result.shadeCi, kParityTol),
        "sphere parity: gman::albedo equals gman::shade's own Ci within 1e-6 per channel");
}

// A unit square (object space == camera space here, an implicit identity
// CTM) carrying "st", built the way RiPolygonV itself builds one
// (gmanrendermanimpl.cpp): a real "P" and a real "st", not
// GMANRayPolygon's own default-constructed, "P"-less parameter list.
GMANRayPolygon* squareWithSt() {
  std::vector<GMANPoint> verts = {GMANPoint(-1.0f, -1.0f, 0.0f), GMANPoint(1.0f, -1.0f, 0.0f),
                                  GMANPoint(1.0f, 1.0f, 0.0f), GMANPoint(-1.0f, 1.0f, 0.0f)};
  RtFloat p[] = {-1, -1, 0, 1, -1, 0, 1, 1, 0, -1, 1, 0};
  // Each vertex's own default (x, y) transposed to (y, x), so a hit's (s,
  // t) differs from its own (u, v) -- proof the parity check reads a real
  // textured (s, t), not one that happens to equal (u, v).
  RtFloat st[] = {-1, -1, -1, 1, 1, 1, 1, -1};
  RtToken tokens[] = {RI_P, RI_ST};
  RtPointer parms[] = {(RtPointer)p, (RtPointer)st};
  GMANParameterList pl(gman::standardDictionary(), 2, tokens, parms, 4, 4, 1, 1);
  return new GMANRayPolygon(verts, pl);
}

// ---- check 3: albedo/shade parity on a polygon hit carrying "st" ----
void testParityOnTexturedPolygon() {
  EnvProbeShader const shader;
  gman::Appearance appearance;
  appearance.shader = asAppearanceShader(shader);
  appearance.Cs = GMANColor(0.2f, 0.8f, 0.4f);
  appearance.Os = GMANColor(1.0f, 1.0f, 1.0f);

  GMANLinearWorldManager world;
  world.add(squareWithSt());
  GMANRayBVH bvh;
  bvh.build(world);

  GMANRay const ray(GMANPoint(0.5f, 0.25f, -5.0f), GMANVector(0.0f, 0.0f, 1.0f));
  GMANHit hit;
  GMANRayInterface const* hitPrimitive = nullptr;
  check(bvh.nearestHit(ray, hit, hitPrimitive), "polygon parity: the ray hits the textured polygon");
  if (hitPrimitive == nullptr) {
    return;
  }
  check(hit.u != hit.v, "polygon parity fixture: the hit's own (u, v) is not symmetric, so a transposed (s, t) "
                        "cannot coincidentally match it");

  gman::SurfacePoint const point = gman::hitSurfacePoint(ray, hit);
  GMANMatrix4 const cameraToWorld;
  ParityResult const result = parityAt(appearance, point, cameraToWorld);
  check(colorNear(result.albedoAnswer, result.shadeCi, kParityTol),
        "polygon parity: gman::albedo equals gman::shade's own Ci within 1e-6 per channel");
}

// ---- check 4: a null shader answers Cs clamped to [0, 1] exactly ----
void testNullShaderClampsCs() {
  gman::Appearance appearance;
  appearance.Cs = GMANColor(1.4f, 0.2f, -0.3f);

  gman::SurfacePoint point;
  GMANMatrix4 const cameraToWorld;
  GMANColor const answer = gman::albedo(appearance, point, cameraToWorld);
  check(answer.getRed() == 1.0f && answer.getGreen() == 0.2f && answer.getBlue() == 0.0f,
        "null shader: albedo answers Cs clamped to [0, 1] exactly");
}

} // namespace

int main() {
  testHitSurfacePointFieldMapping();
  testParityOnSphere();
  testParityOnTexturedPolygon();
  testNullShaderClampsCs();

  return checkSummary("gman::hitSurfacePoint maps a BVH hit's own fields, gman::albedo agrees with gman::shade's "
                      "own Ci at the same point for a sphere and a textured polygon, and a null shader answers Cs "
                      "clamped to [0, 1]");
}
