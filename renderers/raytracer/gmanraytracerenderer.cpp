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

#include "gmanmath.h"
#include "gmanrayinterface.h"
#include "gmanraysphere.h"
#include "gmanraytracerenderer.h"
#include "gmanrenderer.h"
#include "gmanshading.h"
#include "gmanworldmanager.h"
#include "ri.h"

// The shadow ray's tmin, scaled to the hit point's own coordinate
// magnitude rather than fixed. kSelfShadowBiasScale must stay above the
// hit-point error recomputing a point through a primitive's own quadratic
// solve leaves behind -- an error that grows with that magnitude -- and
// below the smallest gap this renderer's own geometry ever puts between
// two surfaces, or a real blocker close to what it shadows stops
// registering. kSelfShadowBiasFloor keeps a hit point at or near the
// origin, where the scaled term vanishes, a positive tmin.
constexpr RtFloat kSelfShadowBiasScale = (RtFloat)1.0e-2;
constexpr RtFloat kSelfShadowBiasFloor = (RtFloat)1.0e-6;

namespace {

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
  return point;
}

// Walks worldManager for a ray-primitive hit against ray, shared by
// nearestHit (every primitive, keeping the nearest) and
// GMANRayOccluder::transmission (the first hit, since any one blocks).
// stopAtFirst selects which; moves worldManager's shared getFirst/getNext
// cursor either way.
bool walkWorldManager(GMANWorldManager& worldManager, GMANRay const& ray, bool stopAtFirst, GMANHit& hit,
                      GMANRayInterface const*& hitPrimitive) {
  bool found = false;
  GMANPrimitive* primitive = worldManager.getFirst();
  while (primitive) {
    GMANRayInterface const* rayPrimitive = dynamic_cast<GMANRayInterface const*>(primitive);
    GMANHit candidate;
    if (rayPrimitive && rayPrimitive->intersect(ray, candidate) && (!found || candidate.t < hit.t)) {
      hit = candidate;
      hitPrimitive = rayPrimitive;
      found = true;
      if (stopAtFirst) {
        return true;
      }
    }
    primitive = worldManager.getNext();
  }
  return found;
}

} // namespace

GMANColor GMANRayOccluder::transmission(GMANLight const& /*light*/, GMANPoint const& P, GMANVector const& towardLight,
                                        RtFloat distance) const {
  RtFloat const magnitude = GMANMax(GMANMax(std::fabs(P.getX()), std::fabs(P.getY())), std::fabs(P.getZ()));
  RtFloat const bias = GMANMax(kSelfShadowBiasScale * magnitude, kSelfShadowBiasFloor);
  GMANRay const shadowRay(P, towardLight, bias, distance);

  GMANHit hit;
  GMANRayInterface const* hitPrimitive = nullptr;
  bool const blocked = walkWorldManager(worldManager, shadowRay, /*stopAtFirst=*/true, hit, hitPrimitive);
  return blocked ? GMANColor(0.0f, 0.0f, 0.0f) : GMANColor(1.0f, 1.0f, 1.0f);
}

/*
 * RenderMan API GMANRaytraceRenderer
 *
 */

// default constructor
GMANRaytraceRenderer::GMANRaytraceRenderer() : GMANRenderer(), occluder(worldManager) {};

// default destructor
GMANRaytraceRenderer::~GMANRaytraceRenderer() {};

GMANRaytraceRenderer::RayHit GMANRaytraceRenderer::nearestHit(GMANRay const& ray) {
  RayHit result;
  GMANRayInterface const* hitPrimitive = nullptr;
  if (walkWorldManager(worldManager, ray, /*stopAtFirst=*/false, result.hit, hitPrimitive)) {
    result.appearance = &hitPrimitive->getAppearance();
  }
  return result;
}

void GMANRaytraceRenderer::shadeSample(GMANViewingSystem* viewingSys, GMANMatrix4 const& cameraToWorld, RtFloat rasterX,
                                       RtFloat rasterY, int sampleX, int sampleY) {
  GMANRay const ray = viewingSys->cameraRay(rasterX, rasterY);
  RayHit const nearest = nearestHit(ray);
  if (nearest.appearance == nullptr) {
    return;
  }

  GMANColor const color = gman::shade(*nearest.appearance, hitSurfacePoint(ray, nearest.hit), cameraToWorld, &occluder);

  // Camera-space z of the hit point (see getDepth's own comment on why
  // this differs from the z-buffer's post-projection depth).
  sampleBuffer->zTestAndSet(sampleX, sampleY, nearest.hit.point.getZ(), color);
}

void GMANRaytraceRenderer::render(GMANFrameBuffer* frameBuffer, GMANViewingSystem* viewingSys,
                                  const GMANOptions& options, const GMANAttributes& /*attributes*/) {
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
  // at construction.
  sampleBuffer.reset(new GMANSampleBuffer(width, height, xsamples, ysamples, frameBuffer->getPixel(0, 0)));

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
          shadeSample(viewingSys, cameraToWorld, rasterX, rasterY, sampleX, sampleY);
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
