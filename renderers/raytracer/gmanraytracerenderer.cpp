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
#include "gmanrayinterface.h"
#include "gmanraysphere.h"
#include "gmanraytracerenderer.h"
#include "gmanrenderer.h"
#include "gmanshading.h"
#include "gmanworldmanager.h"
#include "ri.h"

namespace {

// The gman::SurfacePoint a ray hit implies: P and N/Ng already camera
// space (GMANRayInterface::intersect's own contract), I the ray's own
// direction rather than assumed from the camera-space origin -- an
// orthographic ray, or a future secondary ray, does not look from there.
// s and t default to u and v, the RISpec's own default texture-coordinate
// mapping (texture-coordinate corners on a ray primitive are R5).
gman::SurfacePoint hitSurfacePoint(GMANRay const& ray, GMANHit const& hit) {
  gman::SurfacePoint point;
  point.P = hit.point;
  point.N = GMANNormal(hit.normal.getX(), hit.normal.getY(), hit.normal.getZ());
  point.Ng = point.N;
  point.I = ray.getDirection();
  point.u = hit.u;
  point.v = hit.v;
  point.s = hit.u;
  point.t = hit.v;
  return point;
}

} // namespace

/*
 * RenderMan API GMANRaytraceRenderer
 *
 */

// default constructor
GMANRaytraceRenderer::GMANRaytraceRenderer() : GMANRenderer() {};

// default destructor
GMANRaytraceRenderer::~GMANRaytraceRenderer() {};

GMANHit GMANRaytraceRenderer::nearestHit(GMANRay const& ray) {
  GMANHit best;
  GMANPrimitive* primitive = worldManager.getFirst();
  while (primitive) {
    GMANRayInterface const* rayPrimitive = dynamic_cast<GMANRayInterface const*>(primitive);
    GMANHit candidate;
    if (rayPrimitive && rayPrimitive->intersect(ray, candidate) &&
        (best.primitive == nullptr || candidate.t < best.t)) {
      best = candidate;
    }
    primitive = worldManager.getNext();
  }
  return best;
}

void GMANRaytraceRenderer::render(GMANFrameBuffer* frameBuffer, GMANViewingSystem* viewingSys,
                                  const GMANOptions& options, const GMANAttributes& /*attributes*/) {
  RtInt const width = frameBuffer->getWidth();
  RtInt const height = frameBuffer->getHeight();

  GMANOptions::RasterInfo const ri = options.getRasterInfo();

  // GMANRenderManImpl::RiPixelSamples already rounds to an integer count
  // and clamps to [1,16]; GMANMax here is just the floor this renderer
  // itself relies on for a default-constructed GMANOptions.
  GMANOptions::PixelSamplesStruct const& ps = options.getPixelSamples();
  int const xsamples = GMANMax(1, (int)GMANRound(ps.xsamples));
  int const ysamples = GMANMax(1, (int)GMANRound(ps.ysamples));

  // Every sample starts at the frame's background colour and infinite
  // depth (the settled decision on uncovered samples); frameBuffer is
  // already erased to background at construction.
  sampleBuffer.reset(new GMANSampleBuffer(width, height, xsamples, ysamples, frameBuffer->getPixel(0, 0)));

  GMANMatrix4 const& cameraToWorld = options.getCameraToWorld();

  for (int py = 0; py < height; py++) {
    for (int px = 0; px < width; px++) {
      for (int subY = 0; subY < ysamples; subY++) {
        for (int subX = 0; subX < xsamples; subX++) {
          // The raster point at this sample's centre, in the full
          // (uncropped) raster grid GMANViewingSystem::cameraRay expects --
          // ri.rxmin/rymin place this (possibly cropped) buffer's local
          // (px, py) there, the same origin the z-buffer's render reads.
          RtFloat const rasterX = (RtFloat)ri.rxmin + (RtFloat)px + ((RtFloat)subX + (RtFloat)0.5) / (RtFloat)xsamples;
          RtFloat const rasterY = (RtFloat)ri.rymin + (RtFloat)py + ((RtFloat)subY + (RtFloat)0.5) / (RtFloat)ysamples;
          GMANRay const ray = viewingSys->cameraRay(rasterX, rasterY);

          GMANHit const hit = nearestHit(ray);
          if (hit.primitive == nullptr) {
            continue;
          }

          GMANRayInterface const* rayPrimitive = dynamic_cast<GMANRayInterface const*>(hit.primitive);
          GMANColor const color = gman::shade(rayPrimitive->getAppearance(), hitSurfacePoint(ray, hit), cameraToWorld);

          int const sampleX = px * xsamples + subX;
          int const sampleY = py * ysamples + subY;
          // Camera-space z of the hit (GMANRay::pointAt on the camera-space
          // ray cameraRay returned), not post-projection z -- the settled
          // decision that leaves this renderer's depth and the z-buffer's
          // unaligned; nothing reads getDepth today.
          sampleBuffer->zTestAndSet(sampleX, sampleY, hit.point.getZ(), color);
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
