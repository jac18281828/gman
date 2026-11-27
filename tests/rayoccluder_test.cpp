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
 * GMANRayOccluder::transmission. A point light's own
 * distance bounds the shadow ray -- a blocker past it does not shadow,
 * the same blocker short of it does. Sampled densely across a lit sphere's
 * own surface, near-grazing points included, every self-test transmits
 * white: kSelfShadowOffsetScale keeps a lit convex surface from shadowing
 * itself, at three scales spanning 1e6:1 -- an offset scaled to coordinate
 * magnitude self-shadows once the scene is large enough, and swallows a
 * genuine blocker once it is small enough, so the self-shadow sweep runs
 * at x1, x1000 and x0.001, and a scaled blocker/receiver pair at x1 and
 * x0.001.
 *
 * An occluder built with a camera transform and a texture cache shades each
 * blocker: a ray through a sphere's centre crosses two surfaces at normal
 * incidence and answers the product of their factors, and a null-shader
 * opaque sphere still answers black. An occluder built on a bvh alone keeps
 * 1 - Os per crossing and skips the shader.
 */

#include <cmath>
#include <memory>
#include <numbers>
#include <string>

#include "check.h"
#include "gmanbsdf.h"
#include "gmancolor.h"
#include "gmanlightsourcemgr.h"
#include "gmanlinearworldmanager.h"
#include "gmanmath.h"
#include "gmanmatrix4.h"
#include "gmanparameterlist.h"
#include "gmanpoint.h"
#include "gmanray.h"
#include "gmanraybbox.h"
#include "gmanrayoccluder.h"
#include "gmanraysphere.h"
#include "gmanshading.h"
#include "gmansurfaceshader.h"
#include "gmantexture.h"
#include "gmantransform.h"
#include "gmanvector.h"
#include "maketransform.h"
#include "ri.h"

namespace {

constexpr RtFloat kIndex = (RtFloat)1.5;
constexpr RtFloat kRelativeTolerance = (RtFloat)1e-5;
GMANColor const kShaderOpacity(1.0f, 0.5f, 0.0f);
GMANColor const kShaderWeight(1.0f, 0.5f, 0.25f);

// Opaque (Os = white): a bare GMANRayPrimitive's appearance defaults to
// black, and the occluder attenuates by Os rather than by the hit
// alone, so a blocker meant to fully occlude has to say so.
GMANRaySphere* sphereAt(RtFloat radius, RtFloat cx, RtFloat cy, RtFloat cz) {
  GMANMatrix4 place;
  place.trans(cx, cy, cz);
  GMANTransform const transform = makeTransform(place);
  const auto sphere = new GMANRaySphere(radius, -radius, radius, 360.0f, GMANParameterList(), transform);
  gman::Appearance opaque;
  opaque.Os = GMANColor(1.0f, 1.0f, 1.0f);
  sphere->setAppearance(opaque);
  return sphere;
}

class DielectricShader : public GMANSurfaceShader {
public:
  GMANColor computeCi(GMANSurfaceEnv const&) const override { return GMANColor(0.0f, 0.0f, 0.0f); }
  GMANColor computeOi(GMANSurfaceEnv const&) const override { return kShaderOpacity; }
  gman::BSDF bsdf(GMANSurfaceEnv const& se) const override {
    gman::BSDF closure(GMANVector(se.N.getX(), se.N.getY(), se.N.getZ()));
    closure.addDielectric(kShaderWeight, kIndex);
    return closure;
  }
};

GMANRaySphere* sphereWith(gman::Appearance const& appearance) {
  GMANMatrix4 place;
  const auto sphere = new GMANRaySphere(1.0f, -1.0f, 1.0f, 360.0f, GMANParameterList(), makeTransform(place));
  sphere->setAppearance(appearance);
  return sphere;
}

gman::Appearance shadedAppearance(DielectricShader const& shader) {
  gman::Appearance appearance;
  appearance.Os = GMANColor(1.0f, 1.0f, 1.0f);
  appearance.shader = std::shared_ptr<GMANSurfaceShader const>(&shader, [](GMANSurfaceShader const*) {});
  return appearance;
}

RtFloat crossingFactor(RtFloat opacity, RtFloat weight) {
  RtFloat const root = (kIndex - (RtFloat)1.0) / (kIndex + (RtFloat)1.0);
  return ((RtFloat)1.0 - opacity) + opacity * weight * ((RtFloat)1.0 - root * root);
}

bool near(RtFloat a, RtFloat b) { return std::fabs(a - b) <= kRelativeTolerance * std::fabs(b); }

// The shadow ray runs along +x from x = -5 through the sphere's centre at the
// origin to a point light at x = 5, crossing the sphere's two surfaces at
// normal incidence.
GMANColor walkThroughCentre(GMANRayOccluder const& occluder) {
  GMANPoint const P(-5.0f, 0.0f, 0.0f);
  GMANLight const light(GMAN_LIGHT_POINT, GMANColor(1.0f, 1.0f, 1.0f), GMANPoint(5.0f, 0.0f, 0.0f), GMANVector());
  return occluder.transmission(light, P, GMANVector(1.0f, 0.0f, 0.0f), GMANVector(), 10.0f, 0.0f);
}

// ---- a shading occluder composites each blocker's shader-evaluated factor ----
void testShadingOccluderMultipliesTwoCrossings() {
  DielectricShader const shader;
  GMANLinearWorldManager worldManager;
  worldManager.add(sphereWith(shadedAppearance(shader)));
  GMANRayBVH bvh;
  bvh.build(worldManager);
  gman::TextureCache textureCache;
  GMANRayOccluder const occluder(bvh, GMANMatrix4(), textureCache);

  GMANColor const result = walkThroughCentre(occluder);
  RtFloat const red = crossingFactor(kShaderOpacity.getRed(), kShaderWeight.getRed());
  RtFloat const green = crossingFactor(kShaderOpacity.getGreen(), kShaderWeight.getGreen());
  RtFloat const blue = crossingFactor(kShaderOpacity.getBlue(), kShaderWeight.getBlue());
  check(near(result.getRed(), red * red) && near(result.getGreen(), green * green) &&
            near(result.getBlue(), blue * blue),
        "f: a shading occluder through a sphere's centre answers the product of its two crossings, per channel");
}

// ---- a bvh-only occluder keeps 1 - Os per crossing ----
void testBvhOnlyOccluderIgnoresTheShader() {
  DielectricShader const shader;
  {
    GMANLinearWorldManager worldManager;
    worldManager.add(sphereWith(shadedAppearance(shader)));
    GMANRayBVH bvh;
    bvh.build(worldManager);
    GMANRayOccluder const occluder(bvh);

    GMANColor const result = walkThroughCentre(occluder);
    check(result.getRed() == 0.0f && result.getGreen() == 0.0f && result.getBlue() == 0.0f,
          "g: a bvh-only occluder over an Os-1 sphere answers black however its shader answers");
  }

  {
    gman::Appearance partial = shadedAppearance(shader);
    partial.Os = GMANColor(0.5f, 0.25f, 0.0f);
    GMANLinearWorldManager worldManager;
    worldManager.add(sphereWith(partial));
    GMANRayBVH bvh;
    bvh.build(worldManager);
    GMANRayOccluder const occluder(bvh);

    GMANColor const result = walkThroughCentre(occluder);
    check(near(result.getRed(), 0.25f) && near(result.getGreen(), 0.5625f) && near(result.getBlue(), 1.0f),
          "g: a bvh-only occluder answers 1 - Os per crossing, squared across a sphere's two surfaces");
  }
}

// ---- a shading occluder still blocks an opaque null-shader sphere ----
void testShadingOccluderBlocksOpaqueNullShader() {
  GMANLinearWorldManager worldManager;
  worldManager.add(sphereAt(1.0f, 0.0f, 0.0f, 0.0f));
  GMANRayBVH bvh;
  bvh.build(worldManager);
  gman::TextureCache textureCache;
  GMANRayOccluder const occluder(bvh, GMANMatrix4(), textureCache);

  GMANColor const result = walkThroughCentre(occluder);
  check(result.getRed() == 0.0f && result.getGreen() == 0.0f && result.getBlue() == 0.0f,
        "h: a shading occluder over a null-shader opaque sphere answers black");
}

// ---- a point light's own distance bounds the shadow ray ----
void testPointLightDistanceBoundsTheShadowRay() {
  GMANPoint const P(0.0f, 0.0f, 0.0f);
  GMANPoint const lightPos(0.0f, 0.0f, 10.0f);
  GMANLight const light(GMAN_LIGHT_POINT, GMANColor(1.0f, 1.0f, 1.0f), lightPos, GMANVector());
  GMANVector towardLight(P, lightPos);
  RtFloat const distance = towardLight.magnitude();
  towardLight.normalize();

  {
    // GMANLinearWorldManager owns what it holds and deletes it on
    // destruction (see its own destructor), so blocker is not deleted here.
    GMANLinearWorldManager worldManager;
    worldManager.add(sphereAt(1.0f, 0.0f, 0.0f, 15.0f)); // beyond the light
    GMANRayBVH bvh;
    bvh.build(worldManager);
    GMANRayOccluder const occluder(bvh);

    // P is a free-space point, not on any primitive: Ng is the zero
    // vector and surfaceMagnitude is 0.0, the header's own contract for
    // that case.
    GMANColor const result = occluder.transmission(light, P, towardLight, GMANVector(), distance, 0.0f);
    check(result.getRed() > 0.99f && result.getGreen() > 0.99f && result.getBlue() > 0.99f,
          "point light: a blocker beyond the light's own distance transmits white");
  }

  {
    GMANLinearWorldManager worldManager;
    worldManager.add(sphereAt(1.0f, 0.0f, 0.0f, 5.0f)); // between P and the light
    GMANRayBVH bvh;
    bvh.build(worldManager);
    GMANRayOccluder const occluder(bvh);

    GMANColor const result = occluder.transmission(light, P, towardLight, GMANVector(), distance, 0.0f);
    check(result.getRed() < 0.01f && result.getGreen() < 0.01f && result.getBlue() < 0.01f,
          "point light: the same blocker between P and the light transmits black");
  }
}

// ---- a lit convex surface casts no shadow on itself, at scale ----
// Every hit is generated by GMANRaySphere::intersect, the same call the
// render loop makes, so hit.point carries the same object-space round
// trip's floating-point error a primary ray's hit does, which a point
// placed directly by trigonometry would lack. scale
// multiplies the sphere's own radius and every ray origin, so the sampled
// hit points carry that scale's coordinate magnitude, the way a scaled
// RIB scene would.
void testSelfShadowAtScale(RtFloat scale) {
  GMANLinearWorldManager worldManager;
  // sphereAt owns what worldManager holds and deletes it on destruction
  // (GMANLinearWorldManager's own contract).
  GMANRaySphere* sphere = sphereAt(scale, 0.0f, 0.0f, 0.0f);
  worldManager.add(sphere);
  GMANRayBVH bvh;
  bvh.build(worldManager);
  GMANRayOccluder const occluder(bvh);

  GMANVector lightDir(1.0f, 0.6f, 0.3f); // off-axis, so many samples graze
  lightDir.normalize();
  GMANLight const light(GMAN_LIGHT_DISTANT, GMANColor(1.0f, 1.0f, 1.0f), GMANPoint(),
                        GMANVector(-lightDir.getX(), -lightDir.getY(), -lightDir.getZ()));

  constexpr int kThetaSteps = 72;
  constexpr int kPhiSteps = 36;
  int litSamples = 0;
  int selfShadowed = 0;
  for (int ti = 0; ti < kThetaSteps; ++ti) {
    RtFloat const theta = (RtFloat)ti / (RtFloat)kThetaSteps * 2.0f * std::numbers::pi_v<RtFloat>;
    for (int pi = 1; pi < kPhiSteps; ++pi) { // skip the poles: theta is undefined there
      RtFloat const phi = (RtFloat)pi / (RtFloat)kPhiSteps * std::numbers::pi_v<RtFloat>;
      GMANVector const dir((RtFloat)(std::sin(phi) * std::cos(theta)), (RtFloat)(std::sin(phi) * std::sin(theta)),
                           (RtFloat)std::cos(phi));
      GMANPoint const origin(dir.getX() * 5.0f * scale, dir.getY() * 5.0f * scale, dir.getZ() * 5.0f * scale);
      GMANVector const direction(origin, GMANPoint(0.0f, 0.0f, 0.0f));
      GMANRay const ray(origin, direction);
      GMANHit hit;
      if (!sphere->intersect(ray, hit)) {
        continue;
      }
      RtFloat const nDotL = hit.normal.dot(lightDir);
      if (nDotL <= 0.0f) {
        continue; // the sphere's own dark side: not this check's business
      }
      ++litSamples;
      RtFloat const magnitude = gman::primitiveMagnitude(sphere->getBBox());
      GMANColor const result = occluder.transmission(light, hit.point, lightDir, hit.normal, RI_INFINITY, magnitude);
      if (result.getRed() < 0.5f) {
        ++selfShadowed;
      }
    }
  }

  std::string const scaleLabel = "x" + std::to_string(scale);
  check(litSamples > 500,
        "self-shadow " + scaleLabel + ": enough lit-side samples were gathered to trust a zero count");
  check(selfShadowed == 0, "self-shadow " + scaleLabel + ": every lit-side sample transmits white toward the light (" +
                               std::to_string(selfShadowed) + "/" + std::to_string(litSamples) + " self-shadowed)");
}

// ---- a genuine blocker still casts a shadow once the scene shrinks ----
// A fixed bias exceeding the whole scaled scene would swallow this
// blocker the way it swallows self-hits' error -- the failure a scale-
// blind bias produces at the opposite end from self-shadowing. The
// blocker sits off the P-to-light axis by less than its own radius: an
// on-axis blocker is hit exactly at its pole, where GMANRaySphere's own
// zmin/zmax band check is precision-fragile at small radius -- a separate,
// pre-existing concern this test does not need to exercise.
void testBlockerRelightsAtScale(RtFloat scale) {
  GMANPoint const P(0.0f, 0.0f, 0.0f);
  GMANPoint const lightPos(0.0f, 0.0f, 10.0f * scale);
  GMANLight const light(GMAN_LIGHT_POINT, GMANColor(1.0f, 1.0f, 1.0f), lightPos, GMANVector());
  GMANVector towardLight(P, lightPos);
  RtFloat const distance = towardLight.magnitude();
  towardLight.normalize();

  GMANLinearWorldManager worldManager;
  worldManager.add(sphereAt(scale, 0.4f * scale, 0.0f, 5.0f * scale)); // between P and the light, off-axis
  GMANRayBVH bvh;
  bvh.build(worldManager);
  GMANRayOccluder const occluder(bvh);

  // P is a free-space point, not on any primitive -- Ng is the zero vector
  // the header's own contract names for that case. No offset scale would
  // change this test's own result, since a free point has no surface to
  // offset from; that is testSelfShadowAtScale's business, not this one's.
  GMANColor const result = occluder.transmission(light, P, towardLight, GMANVector(), distance, 0.0f);
  std::string const scaleLabel = "x" + std::to_string(scale);
  check(result.getRed() < 0.01f && result.getGreen() < 0.01f && result.getBlue() < 0.01f,
        "blocker relights " + scaleLabel + ": a blocker between P and the light still transmits black");
}

} // namespace

int main() {
  testPointLightDistanceBoundsTheShadowRay();

  testSelfShadowAtScale(1.0f);
  testSelfShadowAtScale(1000.0f);
  testSelfShadowAtScale(0.001f);

  testBlockerRelightsAtScale(1.0f);
  testBlockerRelightsAtScale(0.001f);

  testShadingOccluderMultipliesTwoCrossings();
  testBvhOnlyOccluderIgnoresTheShader();
  testShadingOccluderBlocksOpaqueNullShader();

  return checkSummary("GMANRayOccluder::transmission: a point light's own distance, no self-shadowing, at every scale");
}
