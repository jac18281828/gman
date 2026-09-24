/* SPDX-License-Identifier: LGPL-2.1-or-later */

/* This is part of GMAN, a RenderMan-compatible renderer.
 *
 * Copyright (c) 2001, 2000, 1999  John Cairns
 *
 * Author: John Cairns <john@2ad.com>
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

#include <cmath>
#include <limits>

#include "gmanmath.h"
#include "gmanrayinterface.h"
#include "gmanraysphere.h"
#include "gmanraytracerenderer.h"
#include "gmanrenderer.h"
#include "gmanshading.h"
#include "gmanworldmanager.h"
#include "ri.h"

// One unit roundoff: half an RtFloat ulp at 1.0, the size of every term
// kSelfShadowOffsetScale's own derivation below counts in.
constexpr RtFloat kUnitRoundoff = std::numeric_limits<RtFloat>::epsilon() / (RtFloat)2.0;

// The distance a ray continuing past a hit displaces its own origin,
// along the surface's geometric normal, keyed on the larger of the hit
// point's own coordinate magnitude and the hit primitive's own
// camera-space extent (M) -- a primitive's own size, not the camera
// distance alone, since a large surface near the camera needs an offset
// proportional to itself, not to |P|. kSelfShadowOffsetFloor keeps a hit
// point at or near the origin, where the scaled term vanishes, a positive
// offset. kSelfShadowOffsetCeiling caps the result at today's own
// max|P|-scaled offset, so a large receiver near the camera never gets a
// larger offset than before, and its own contact shadows are no worse.
//
// c (16) is the larger of two bounds. The derivation: the chain from the
// exact surface point to the next intersect's view of the moved origin
// crosses four roundings -- the object-space root cast to RtFloat, the
// float objectToCamera transformPoint building hit.point, the float add
// in offsetOrigin, and the next intersect's float cameraToObject
// transformPoint (which also carries GMANMatrix4::invert's own error and
// a homogeneous divide off 1 by a ulp) -- each bounded by the sweep's own
// condition-number-4 affine transform, so each contributes at most
// 4 * kUnitRoundoff * max(max|P|, M); four such terms sum to 16. The
// measured bound: the self-shadow sweep's own largest per-cell minimum
// self-hit-free c, over the quadrics and the torus at x1 and x1000 in
// both the dev and nofma builds, was 3 (the nofma build's skewed
// paraboloid at x1000); 4x that margin is 12. 16 clears both.
constexpr RtFloat kSelfShadowOffsetScale = (RtFloat)16.0 * kUnitRoundoff;
constexpr RtFloat kSelfShadowOffsetCeiling = (RtFloat)3.0e-5;
constexpr RtFloat kSelfShadowOffsetFloor = (RtFloat)1.0e-9;

// The composite loop's own stop conditions: a transmission below this in
// every channel is invisible in an 8-bit image, and a stack deeper than
// this many surfaces bounds a pathological scene rather than tracing it
// forever.
constexpr RtFloat kTransmissionCutoff = (RtFloat)(1.0 / 255.0);
constexpr int kMaxCompositeLayers = 16;

// GMANRayTracer::trace()'s own recursion bound: independent of
// kMaxCompositeLayers above, which bounds semi-transparent layers along
// one ray's own path, not recursion branching. A primary ray shades at
// depth 0; each trace() call a hit's own shader makes recurses at
// depth + 1.
constexpr int kMaxTraceDepth = 4;

namespace {

// The largest absolute coordinate over box's own six corners, 0 for a box
// still at its default +/-RI_INFINITY (a primitive whose own bbox was
// never assigned) -- hitSurfacePoint's own M, gman::SurfacePoint's
// surfaceMagnitude field.
RtFloat primitiveMagnitude(GMANBBox const& box) {
  GMANPoint const boxMin = box.getMin();
  GMANPoint const boxMax = box.getMax();
  RtFloat const coords[6] = {boxMin.getX(), boxMin.getY(), boxMin.getZ(), boxMax.getX(), boxMax.getY(), boxMax.getZ()};
  RtFloat magnitude = 0.0;
  for (RtFloat const coord : coords) {
    if (std::fabs(coord) >= RI_INFINITY) {
      return 0.0;
    }
    magnitude = GMANMax(magnitude, (RtFloat)std::fabs(coord));
  }
  return magnitude;
}

// The gman::SurfacePoint a ray hit implies: P and N/Ng already camera
// space (GMANRayInterface::intersect's own contract), I the ray's own
// direction and E its own origin, rather than both assumed at the
// camera-space origin -- an orthographic ray, or a future secondary ray,
// does not look from there. s and t default to u and v, the RISpec's own
// default texture-coordinate mapping; texture-coordinate corners on a ray
// primitive are a later unit.
gman::SurfacePoint hitSurfacePoint(GMANRay const& ray, GMANHit const& hit) {
  gman::SurfacePoint point;
  point.P = hit.point;
  point.N = GMANNormal(hit.normal.getX(), hit.normal.getY(), hit.normal.getZ());
  point.Ng = point.N;
  point.I = ray.getDirection();
  point.E = ray.getOrigin();
  point.u = hit.u;
  point.v = hit.v;
  point.s = hit.u;
  point.t = hit.v;
  point.surfaceMagnitude = primitiveMagnitude(hit.primitive->getBBox());
  return point;
}

// Channel-wise product: how an attenuation composes with a colour or with
// another attenuation throughout this file.
GMANColor multiplyChannels(GMANColor const& a, GMANColor const& b) {
  return GMANColor(a.getRed() * b.getRed(), a.getGreen() * b.getGreen(), a.getBlue() * b.getBlue());
}

// 1 - c per channel: what a surface of opacity c leaves for whatever lies
// behind it.
GMANColor oneMinus(GMANColor const& c) { return GMANColor(1.0f - c.getRed(), 1.0f - c.getGreen(), 1.0f - c.getBlue()); }

// True once transmission is invisible in an 8-bit image on every channel.
bool transmissionNegligible(GMANColor const& transmission) {
  return transmission.getRed() < kTransmissionCutoff && transmission.getGreen() < kTransmissionCutoff &&
         transmission.getBlue() < kTransmissionCutoff;
}

// The self-shadow offset's own magnitude at hitPoint (see
// kSelfShadowOffsetScale/kSelfShadowOffsetFloor); offsetOrigin below
// scales Ng by this and orients the result, and transmission's own loop
// guard compares a ray's remaining interval against it directly.
// surfaceMagnitude is the hit primitive's own M (0 for a free point, no
// surface known), so max|P| alone applies there.
RtFloat selfShadowOffsetMagnitude(GMANPoint const& hitPoint, RtFloat surfaceMagnitude) {
  RtFloat const maxP =
      GMANMax(GMANMax(std::fabs(hitPoint.getX()), std::fabs(hitPoint.getY())), std::fabs(hitPoint.getZ()));
  RtFloat const keyMagnitude = GMANMax(maxP, surfaceMagnitude);
  RtFloat const scaled = GMANMin(kSelfShadowOffsetCeiling * maxP, kSelfShadowOffsetScale * keyMagnitude);
  return GMANMax(kSelfShadowOffsetFloor, scaled);
}

// hitPoint displaced along Ng by the self-shadow offset, oriented toward
// reference by the sign of Ng . reference -- Ng carries no orientation
// guarantee of its own, and an offset on the wrong side would place the
// new origin inside the surface. reference is towardLight for a shadow
// ray (the offset lands toward the light) and the incoming ray's own
// direction for the composite loop (the offset lands on the far side of
// the surface the ray is continuing through), both unit length by
// construction. GMANRayTracer::trace()'s own reference is a
// shader-supplied R instead -- unit length only insofar as the
// shader's own reflect()/refract() (or whatever it called) produced
// one; this file does not itself enforce it. surfaceMagnitude is
// hitPoint's own surface's M, forwarded to selfShadowOffsetMagnitude.
GMANPoint offsetOrigin(GMANPoint const& hitPoint, GMANVector const& Ng, GMANVector const& reference,
                       RtFloat surfaceMagnitude) {
  RtFloat const magnitude = selfShadowOffsetMagnitude(hitPoint, surfaceMagnitude);
  RtFloat const offset = (Ng.dot(reference) < (RtFloat)0.0) ? -magnitude : magnitude;
  return GMANPoint(hitPoint.getX() + Ng.getX() * offset, hitPoint.getY() + Ng.getY() * offset,
                   hitPoint.getZ() + Ng.getZ() * offset);
}

} // namespace

GMANColor GMANRayOccluder::transmission(GMANLight const& /*light*/, GMANPoint const& P, GMANVector const& towardLight,
                                        GMANVector const& Ng, RtFloat distance, RtFloat surfaceMagnitude) const {
  GMANColor transmission(1.0f, 1.0f, 1.0f);
  GMANPoint origin = offsetOrigin(P, Ng, towardLight, surfaceMagnitude);
  RtFloat remaining = distance;
  // The surface origin currently sits on: the caller's own P until the
  // walk crosses a blocker, then that blocker's own M.
  RtFloat currentMagnitude = surfaceMagnitude;

  // A closed solid contributes one (1 - Os) factor per surface the shadow
  // ray crosses, not per blocker: bvh.nearestHit keeps one hit per call,
  // so this walks the interval itself, moving origin/remaining past each
  // surface found. This matches the composite loop below, which likewise
  // crosses both shells of a sphere the ray enters. The cap bounds a
  // stack deeper than it, not an infinite walk: offsetOrigin always
  // advances toward reference regardless of which surface's own normal
  // produced it, so a zero-opacity surface stack never decays
  // transmission enough to break the loop on its own, and a pathological
  // stack deeper than the cap would otherwise be walked in full.
  for (int layer = 0; layer < kMaxCompositeLayers; ++layer) {
    // A light nearer than the offset itself still has to end the walk,
    // though at this offset's scale that is effectively never.
    if (selfShadowOffsetMagnitude(origin, currentMagnitude) >= remaining) {
      break;
    }
    GMANRay const shadowRay(origin, towardLight, RI_EPSILON, remaining);
    GMANHit hit;
    GMANRayInterface const* hitPrimitive = nullptr;
    if (!bvh.nearestHit(shadowRay, hit, hitPrimitive)) {
      break;
    }
    transmission = multiplyChannels(transmission, oneMinus(hitPrimitive->getAppearance().Os));
    if (transmissionNegligible(transmission)) {
      return GMANColor(0.0f, 0.0f, 0.0f);
    }
    // The surface this iteration is leaving, not the Ng the caller
    // passed: that one belongs to the first hit only, a different
    // surface once the walk has crossed it.
    currentMagnitude = primitiveMagnitude(hitPrimitive->getBBox());
    origin = offsetOrigin(hit.point, hit.normal, towardLight, currentMagnitude);
    remaining -= hit.t;
  }
  return transmission;
}

GMANColor GMANRayTracer::trace(GMANPoint const& P, GMANVector const& R, GMANVector const& Ng,
                               RtFloat surfaceMagnitude) const {
  if (depth >= kMaxTraceDepth) {
    // The cheapest possible bound: no ray cast, bvh untouched.
    return background;
  }

  GMANPoint const origin = offsetOrigin(P, Ng, R, surfaceMagnitude);
  GMANRay const ray(origin, R, RI_EPSILON, RI_INFINITY);
  GMANHit hit;
  GMANRayInterface const* hitPrimitive = nullptr;
  if (!bvh.nearestHit(ray, hit, hitPrimitive)) {
    return background;
  }

  GMANRayTracer const child(bvh, occluder, cameraToWorld, background, depth + 1);
  gman::Shading const shading =
      gman::shade(hitPrimitive->getAppearance(), hitSurfacePoint(ray, hit), cameraToWorld, &occluder, &child);
  return shading.Ci;
}

/*
 * RenderMan API GMANRaytraceRenderer
 *
 */

// default constructor
GMANRaytraceRenderer::GMANRaytraceRenderer() : GMANRenderer(), occluder(bvh) {};

// default destructor
GMANRaytraceRenderer::~GMANRaytraceRenderer() {};

GMANRaytraceRenderer::RayHit GMANRaytraceRenderer::nearestHit(GMANRay const& ray) {
  RayHit result;
  GMANRayInterface const* hitPrimitive = nullptr;
  if (bvh.nearestHit(ray, result.hit, hitPrimitive)) {
    result.appearance = &hitPrimitive->getAppearance();
  }
  return result;
}

void GMANRaytraceRenderer::shadeSample(GMANViewingSystem* viewingSys, GMANMatrix4 const& cameraToWorld,
                                       GMANColor const& background, RtFloat rasterX, RtFloat rasterY, int sampleX,
                                       int sampleY) {
  GMANRay ray = viewingSys->cameraRay(rasterX, rasterY);
  RayHit hit = nearestHit(ray);
  if (hit.appearance == nullptr) {
    return;
  }

  // Camera-space z of the hit point (see getDepth's own comment on why
  // this differs from the z-buffer's post-projection depth). The first
  // hit owns the sample's depth even when it is only partly opaque:
  // getDepth answers "what is nearest", not "what is opaque".
  RtFloat const sampleDepth = hit.hit.point.getZ();

  // Front-to-back composite, the RISpec's own "over": colour accumulates
  // what each layer contributes through everything already crossed,
  // transmission shrinks by that layer's own opacity. Every shipped
  // shader's Ci already carries its own Os factor, so no second Oi
  // multiplies the running colour here.
  GMANColor accumulated(0.0f, 0.0f, 0.0f);
  GMANColor transmission(1.0f, 1.0f, 1.0f);

  // Depth 0: a primary ray's own hit. A shader's trace() call recurses
  // into a child GMANRayTracer at depth 1 (see GMANRayTracer::trace()).
  GMANRayTracer const tracer(bvh, occluder, cameraToWorld, background, 0);

  for (int layer = 0; layer < kMaxCompositeLayers && hit.appearance != nullptr; ++layer) {
    gman::Shading const shading =
        gman::shade(*hit.appearance, hitSurfacePoint(ray, hit.hit), cameraToWorld, &occluder, &tracer);
    accumulated += multiplyChannels(transmission, shading.Ci);
    transmission = multiplyChannels(transmission, oneMinus(shading.Oi));
    if (transmissionNegligible(transmission)) {
      break;
    }

    // Offset along Ng rather than the ray direction: offsetting along the
    // direction gives a perpendicular clearance of offset * |Ng . D|,
    // which vanishes at a grazing hit and lets a silhouette-grazing
    // surface composite itself repeatedly. The hit primitive's own M, not
    // the previous layer's: each layer composited here can be a different
    // surface.
    RtFloat const magnitude = primitiveMagnitude(hit.hit.primitive->getBBox());
    GMANPoint const origin = offsetOrigin(hit.hit.point, hit.hit.normal, ray.getDirection(), magnitude);
    ray = GMANRay(origin, ray.getDirection(), RI_EPSILON, RI_INFINITY);
    hit = nearestHit(ray);
  }

  GMANColor const coverage = oneMinus(transmission);
  GMANAlpha const alpha(coverage.getRed(), coverage.getGreen(), coverage.getBlue());
  accumulated += multiplyChannels(transmission, background);
  sampleBuffer->zTestAndSet(sampleX, sampleY, sampleDepth, accumulated, alpha);
}

void GMANRaytraceRenderer::render(GMANFrameBuffer* frameBuffer, GMANViewingSystem* viewingSys,
                                  const GMANOptions& options, const GMANAttributes& /*attributes*/) {
  // Rebuilt every call, discarding any tree a prior call built: simpler
  // than tracking whether a stale tree needs invalidating, and a render
  // pass over the whole frame already costs far more than one extra tree
  // build.
  bvh.build(worldManager);

  RtInt const width = frameBuffer->getWidth();
  RtInt const height = frameBuffer->getHeight();

  GMANOptions::RasterInfo const raster = options.getRasterInfo();

  // GMANRenderManImpl::RiPixelSamples already rounds to an integer count
  // and clamps to [1,16]; GMANMax here is just the floor this renderer
  // itself relies on for a default-constructed GMANOptions.
  GMANOptions::PixelSamplesStruct const& ps = options.getPixelSamples();
  int const xsamples = GMANMax(1, (int)GMANRound(ps.xsamples));
  int const ysamples = GMANMax(1, (int)GMANRound(ps.ysamples));

  // Every sample starts at the frame's background colour and infinite
  // depth, so an uncovered sample resolves to background rather than to
  // indeterminate or black; frameBuffer is already erased to background
  // at construction. shadeSample composites the same colour under
  // whatever transmission a sample's own layers leave.
  GMANColor const background = frameBuffer->getPixel(0, 0);
  sampleBuffer.reset(new GMANSampleBuffer(width, height, xsamples, ysamples, background));

  GMANMatrix4 const& cameraToWorld = options.getCameraToWorld();

  for (int py = 0; py < height; py++) {
    for (int px = 0; px < width; px++) {
      for (int subY = 0; subY < ysamples; subY++) {
        for (int subX = 0; subX < xsamples; subX++) {
          // The raster point at this sample's centre, in the full
          // (uncropped) raster grid GMANViewingSystem::cameraRay expects --
          // raster.rxmin/rymin place this (possibly cropped) buffer's
          // local (px, py) there, the same origin the z-buffer's render
          // reads.
          int const sampleX = px * xsamples + subX;
          int const sampleY = py * ysamples + subY;
          RtFloat const rasterX = gman::sampleCentre(raster.rxmin, sampleX, xsamples);
          RtFloat const rasterY = gman::sampleCentre(raster.rymin, sampleY, ysamples);
          shadeSample(viewingSys, cameraToWorld, background, rasterX, rasterY, sampleX, sampleY);
        }
      }
    }
  }

  GMANOptions::PixelFilterStruct const& pf = options.getPixelFilter();
  sampleBuffer->resolve(frameBuffer, pf.filterfunc, pf.xwidth, pf.ywidth);
}

GMANWorldManager* GMANRaytraceRenderer::getWorldManager(void) { return &worldManager; }

// return its object manager
GMANObjectManager* GMANRaytraceRenderer::getObjectManager(void) { return &objectManager; }
