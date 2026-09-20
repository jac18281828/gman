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
// magnitude rather than fixed. A self-hit's surviving root is
// approximately the hit point's own floating-point error divided by N.L,
// so it grows both toward a grazing angle and with that magnitude.
// kSelfShadowBiasScale must clear that quantity at every angle and
// magnitude this renderer meets, and still stay below the smallest gap
// its own geometry ever puts between two surfaces, or a real blocker
// close to what it shadows stops registering. kSelfShadowBiasFloor keeps
// a hit point at or near the origin, where the scaled term vanishes, a
// positive tmin.
constexpr RtFloat kSelfShadowBiasScale = (RtFloat)1.0e-2;
constexpr RtFloat kSelfShadowBiasFloor = (RtFloat)1.0e-6;

// The composite loop's own stop conditions: a transmission below this in
// every channel is invisible in an 8-bit image, and a stack deeper than
// this many surfaces bounds a pathological scene rather than tracing it
// forever.
constexpr RtFloat kTransmissionCutoff = (RtFloat)(1.0 / 255.0);
constexpr int kMaxCompositeLayers = 16;

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

// Finds the nearest ray-primitive hit against ray, moving worldManager's
// shared getFirst/getNext cursor. Shared by nearestHit and
// GMANRayOccluder::transmission, each of which walks past its own nearest
// hit in turn to advance along the ray.
bool walkWorldManager(GMANWorldManager& worldManager, GMANRay const& ray, GMANHit& hit,
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
    }
    primitive = worldManager.getNext();
  }
  return found;
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

// The bias a ray continuing past hitPoint applies to its own tmin -- the
// shadow ray below and the composite loop's next ray share this, one rule
// for "past this hit" (see kSelfShadowBiasScale/kSelfShadowBiasFloor).
RtFloat selfShadowBias(GMANPoint const& hitPoint) {
  RtFloat const magnitude =
      GMANMax(GMANMax(std::fabs(hitPoint.getX()), std::fabs(hitPoint.getY())), std::fabs(hitPoint.getZ()));
  return GMANMax(kSelfShadowBiasScale * magnitude, kSelfShadowBiasFloor);
}

} // namespace

GMANColor GMANRayOccluder::transmission(GMANLight const& /*light*/, GMANPoint const& P, GMANVector const& towardLight,
                                        GMANVector const& /*Ng*/, RtFloat distance) const {
  GMANColor transmission(1.0f, 1.0f, 1.0f);
  GMANPoint origin = P;
  RtFloat remaining = distance;

  // A closed solid contributes one (1 - Os) factor per surface the shadow
  // ray crosses, not per blocker: walkWorldManager keeps one hit per call,
  // so this walks the interval itself, moving origin/remaining past each
  // surface found. This matches the composite loop below, which likewise
  // crosses both shells of a sphere the ray enters.
  while (true) {
    RtFloat const bias = selfShadowBias(origin);
    if (bias >= remaining) {
      break;
    }
    GMANRay const shadowRay(origin, towardLight, bias, remaining);
    GMANHit hit;
    GMANRayInterface const* hitPrimitive = nullptr;
    if (!walkWorldManager(worldManager, shadowRay, hit, hitPrimitive)) {
      break;
    }
    transmission = multiplyChannels(transmission, oneMinus(hitPrimitive->getAppearance().Os));
    if (transmissionNegligible(transmission)) {
      return GMANColor(0.0f, 0.0f, 0.0f);
    }
    origin = hit.point;
    remaining -= hit.t;
  }
  return transmission;
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
  if (walkWorldManager(worldManager, ray, result.hit, hitPrimitive)) {
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

  for (int layer = 0; layer < kMaxCompositeLayers && hit.appearance != nullptr; ++layer) {
    gman::Shading const shading = gman::shade(*hit.appearance, hitSurfacePoint(ray, hit.hit), cameraToWorld, &occluder);
    accumulated += multiplyChannels(transmission, shading.Ci);
    transmission = multiplyChannels(transmission, oneMinus(shading.Oi));
    if (transmissionNegligible(transmission)) {
      break;
    }

    ray = GMANRay(hit.hit.point, ray.getDirection(), selfShadowBias(hit.hit.point), RI_INFINITY);
    hit = nearestHit(ray);
  }

  accumulated += multiplyChannels(transmission, background);
  sampleBuffer->zTestAndSet(sampleX, sampleY, sampleDepth, accumulated);
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
