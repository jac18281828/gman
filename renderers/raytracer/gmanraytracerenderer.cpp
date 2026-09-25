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

#include "gmanmath.h"
#include "gmanraybbox.h"
#include "gmanrayinterface.h"
#include "gmanraytracerenderer.h"
#include "gmanrenderer.h"
#include "gmanshading.h"
#include "gmanworldmanager.h"
#include "ri.h"

// GMANRayTracer::trace()'s own recursion bound: independent of
// gman::kMaxCompositeLayers (gmanrayoccluder.h), which bounds
// semi-transparent layers along one ray's own path, not recursion
// branching. A primary ray shades at depth 0; each trace() call a hit's
// own shader makes recurses at depth + 1.
constexpr int kMaxTraceDepth = 4;

namespace {

// The gman::SurfacePoint a ray hit implies: P and N/Ng already camera
// space (GMANRayInterface::intersect's own contract), I the ray's own
// direction and E its own origin, rather than both assumed at the
// camera-space origin -- an orthographic ray, or a future secondary ray,
// does not look from there. s and t default to u and v, the RISpec's own
// default texture-coordinate mapping; texture-coordinate corners on a ray
// primitive are not implemented.
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
  point.surfaceMagnitude = gman::primitiveMagnitude(hit.primitive->getBBox());
  return point;
}

} // namespace

GMANColor GMANRayTracer::trace(GMANPoint const& P, GMANVector const& R, GMANVector const& Ng,
                               RtFloat surfaceMagnitude) const {
  if (depth >= kMaxTraceDepth) {
    // The cheapest possible bound: no ray cast, bvh untouched.
    return background;
  }

  GMANPoint const origin = gman::offsetOrigin(P, Ng, R, surfaceMagnitude);
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

  for (int layer = 0; layer < gman::kMaxCompositeLayers && hit.appearance != nullptr; ++layer) {
    gman::Shading const shading =
        gman::shade(*hit.appearance, hitSurfacePoint(ray, hit.hit), cameraToWorld, &occluder, &tracer);
    accumulated += gman::multiplyChannels(transmission, shading.Ci);
    transmission = gman::multiplyChannels(transmission, gman::oneMinus(shading.Oi));
    if (gman::transmissionNegligible(transmission)) {
      break;
    }

    // Offset along Ng rather than the ray direction: offsetting along the
    // direction gives a perpendicular clearance of offset * |Ng . D|,
    // which vanishes at a grazing hit and lets a silhouette-grazing
    // surface composite itself repeatedly. The hit primitive's own M, not
    // the previous layer's: each layer composited here can be a different
    // surface.
    RtFloat const magnitude = gman::primitiveMagnitude(hit.hit.primitive->getBBox());
    GMANPoint const origin = gman::offsetOrigin(hit.hit.point, hit.hit.normal, ray.getDirection(), magnitude);
    ray = GMANRay(origin, ray.getDirection(), RI_EPSILON, RI_INFINITY);
    hit = nearestHit(ray);
  }

  GMANColor const coverage = gman::oneMinus(transmission);
  GMANAlpha const alpha(coverage.getRed(), coverage.getGreen(), coverage.getBlue());
  accumulated += gman::multiplyChannels(transmission, background);
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
