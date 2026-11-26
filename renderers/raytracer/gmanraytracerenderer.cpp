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
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <numbers>
#include <optional>
#include <variant>
#include <vector>

#include "gmanbbox.h"
#include "gmancolor.h"
#include "gmanemitter.h"
#include "gmanframebuffer.h"
#include "gmanindirectpass.h"
#include "gmanlightsourcemgr.h"
#include "gmanlog.h"
#include "gmanmath.h"
#include "gmanmatrix4.h"
#include "gmanobjectmanager.h"
#include "gmanocclude.h"
#include "gmanoptions.h"
#include "gmanparallel.h"
#include "gmanpoint.h"
#include "gmanprimitive.h"
#include "gmanray.h"
#include "gmanraybvh.h"
#include "gmanrayinterface.h"
#include "gmanrayoccluder.h"
#include "gmanraytracerenderer.h"
#include "gmanrenderer.h"
#include "gmansamplebuffer.h"
#include "gmansampling.h"
#include "gmanshading.h"
#include "gmantexture.h"
#include "gmanvector.h"
#include "gmanviewingsystem.h"
#include "gmanworldmanager.h"
#include "ri.h"

// GMANRayTracer::trace()'s own recursion bound: independent of
// gman::kMaxCompositeLayers (gmanrayoccluder.h), which bounds
// semi-transparent layers along one ray's own path, not recursion
// branching. A primary ray shades at depth 0; each trace() call a hit's
// own shader makes recurses at depth + 1.
constexpr int kMaxTraceDepth = 4;

namespace {

// The draws per emitter and shading point when no Option names a count.
constexpr std::uint32_t kDefaultAreaLightSamples = 4;

// Pins every area-light draw this renderer takes; changing it moves every
// image with an area emitter in it.
constexpr std::uint32_t kAreaLightSeed = 0x2f6b1d93u;

// pass's irradiance at hit from dir, and the pointer gman::shade takes
// for it: null when pass is null, so a call site needs one line instead
// of a value-or-default plus a pointer-or-null pair.
struct PassIrradiance {
  GMANColor value;
  GMANColor const* ptr = nullptr;

  PassIrradiance(gman::IndirectPass const* pass, GMANHit const& hit, GMANVector const& dir) {
    if (pass != nullptr) {
      value = pass->irradiance(hit, dir);
      ptr = &value;
    }
  }
};

GMANColor coverageOf(GMANColor const& opacity) {
  auto const channel = [](GMANColor::ColorSampleType v) {
    if (std::isfinite(v)) {
      return v;
    }
    return GMANMax<GMANColor::ColorSampleType>(GMANMin<GMANColor::ColorSampleType>(v, 1.0), 0.0);
  };
  return GMANColor(channel(opacity.getRed()), channel(opacity.getGreen()), channel(opacity.getBlue()));
}

bool colorBlack(GMANColor const& c) { return c.getRed() == 0.0f && c.getGreen() == 0.0f && c.getBlue() == 0.0f; }

bool colorFinite(GMANColor const& c) {
  return std::isfinite(c.getRed()) && std::isfinite(c.getGreen()) && std::isfinite(c.getBlue());
}

// The point light one draw es on an emitter is, seen from P among drawCount
// draws: at the draw's shadow target, coloured so that GMANLight::sample,
// which divides a point light's colour by the squared distance, answers the
// emitter's radiance over pi, its draw's solid-angle density and drawCount.
// Summed over an emitter's draws that is the irradiance over pi a Lambert
// lobe of reflectance 1 reads. Empty when the density is not positive and
// finite, the radiance is black or the colour is not finite.
std::optional<GMANLight> drawnLight(gman::EmitterSample const& es, GMANPoint const& P, std::uint32_t drawCount) {
  if (!(es.pdf > 0.0f && std::isfinite(es.pdf)) || colorBlack(es.Cl)) {
    return std::nullopt;
  }
  GMANVector const toTarget(P, es.shadowTarget);
  RtFloat const weight =
      toTarget.dot(toTarget) / (std::numbers::pi_v<RtFloat> * es.pdf * static_cast<RtFloat>(drawCount));
  GMANColor const colour(es.Cl.getRed() * weight, es.Cl.getGreen() * weight, es.Cl.getBlue() * weight);
  if (!colorFinite(colour)) {
    return std::nullopt;
  }
  return GMANLight(GMAN_LIGHT_POINT, colour, es.shadowTarget, GMANVector());
}

// The shading occluder of a point holding area draws. A light among draws
// is shadowed by the area walk, aimed at the light's own position from an
// origin offset off the point and re-aimed after every blocker, which a
// target only its own offset off an emitter's surface needs; every other
// light, a delta one, goes to deltaOccluder unchanged. draws outlives the
// occluder.
class AreaDrawOccluder : public gman::Occluder {
public:
  AreaDrawOccluder(GMANRayOccluder const& deltaOccluder, GMANRayBVH const& bvh, GMANMatrix4 const& cameraToWorld,
                   gman::TextureCache* textureCache, std::vector<GMANLight> const& draws)
      : deltaOccluder(deltaOccluder), bvh(bvh), cameraToWorld(cameraToWorld), textureCache(textureCache), draws(draws) {
  }

  GMANColor transmission(GMANLight const& light, GMANPoint const& P, GMANVector const& towardLight,
                         GMANVector const& Ng, RtFloat distance, RtFloat surfaceMagnitude) const override {
    if (!isDraw(light)) {
      return deltaOccluder.transmission(light, P, towardLight, Ng, distance, surfaceMagnitude);
    }
    return walkToDraw(light, P, towardLight, Ng, surfaceMagnitude, nullptr);
  }

  GMANColor transmissionFromTerminator(GMANLight const& light, GMANPoint const& P, GMANVector const& towardLight,
                                       GMANVector const& Ng, RtFloat distance, RtFloat surfaceMagnitude,
                                       GMANPrimitive const& origin) const override {
    if (!isDraw(light)) {
      return deltaOccluder.transmissionFromTerminator(light, P, towardLight, Ng, distance, surfaceMagnitude, origin);
    }
    return walkToDraw(light, P, towardLight, Ng, surfaceMagnitude, &origin);
  }

private:
  bool isDraw(GMANLight const& light) const {
    std::less<GMANLight const*> const before;
    GMANLight const* const first = draws.data();
    return !before(&light, first) && before(&light, first + draws.size());
  }

  GMANColor walkToDraw(GMANLight const& light, GMANPoint const& P, GMANVector const& towardLight, GMANVector const& Ng,
                       RtFloat surfaceMagnitude, GMANPrimitive const* terminatorOrigin) const {
    GMANPoint const origin = gman::offsetOrigin(P, Ng, towardLight, surfaceMagnitude);
    GMANPoint const& target = light.getPosition();
    GMANVector toTarget(origin, target);
    RtFloat const distance = toTarget.magnitude();
    toTarget.normalize();
    return gman::shadowWalk(bvh, origin, toTarget, distance, false, target, cameraToWorld, textureCache,
                            terminatorOrigin)
        .v;
  }

  GMANRayOccluder const& deltaOccluder;
  GMANRayBVH const& bvh;
  GMANMatrix4 const& cameraToWorld;
  gman::TextureCache* textureCache;
  std::vector<GMANLight> const& draws;
};

} // namespace

std::vector<GMANLight> GMANAreaLightSample::draw(GMANPoint const& P, GMANRayInterface const& hitPrimitive) {
  std::vector<gman::Emitter> const& emitters = lights.emitters;
  std::uint32_t const emitterCount = static_cast<std::uint32_t>(emitters.size());
  std::uint32_t const site = nextSite++;
  std::vector<GMANLight> draws;
  for (std::uint32_t e = 0; e < emitterCount; ++e) {
    if (emitters[e].shape == &hitPrimitive) {
      continue;
    }
    std::uint32_t const dimension = site * emitterCount + e;
    for (std::uint32_t i = 0; i < lights.drawsPerEmitter; ++i) {
      gman::Sample2D const uv = gman::sample2D(kAreaLightSeed, gridX, gridY, i, lights.drawsPerEmitter, dimension);
      std::optional<GMANLight> const light =
          drawnLight(gman::sample(emitters[e], P, uv.u1, uv.u2), P, lights.drawsPerEmitter);
      if (light) {
        draws.push_back(*light);
      }
    }
  }
  return draws;
}

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

  GMANRayTracer const child(bvh, occluder, cameraToWorld, background, depth + 1, indirectPass, textureCache,
                            areaLights);
  return child.shadeHit(*hitPrimitive, ray, hit).Ci;
}

gman::Shading GMANRayTracer::shadeLit(GMANRayInterface const& hitPrimitive, gman::SurfacePoint const& point,
                                      GMANColor const* indirect) const {
  gman::Appearance const& appearance = hitPrimitive.getAppearance();
  std::vector<GMANLight> draws;
  if (areaLights != nullptr) {
    draws = areaLights->draw(point.P, hitPrimitive);
  }
  if (draws.empty()) {
    return gman::shade(appearance, point, cameraToWorld, &occluder, this, textureCache, indirect);
  }

  gman::Appearance lit = appearance;
  for (GMANLight const& draw : draws) {
    lit.lights.push_back(&draw);
  }
  AreaDrawOccluder const drawOccluder(occluder, bvh, cameraToWorld, textureCache, draws);
  return gman::shade(lit, point, cameraToWorld, &drawOccluder, this, textureCache, indirect);
}

std::optional<GMANColor> GMANRayTracer::emitterRadiance(GMANRayInterface const& hitPrimitive, GMANRay const& ray,
                                                        GMANHit const& hit) const {
  if (areaLights == nullptr) {
    return std::nullopt;
  }
  std::vector<gman::Emitter> const& emitters = areaLights->emitters();
  std::optional<std::size_t> const emitter = gman::emitterIndex(emitters, &hitPrimitive);
  if (!emitter || !(hit.normal.dot(-ray.getDirection()) > 0.0f)) {
    return std::nullopt;
  }
  GMANColor const& radiance = emitters[*emitter].light->getCl();
  if (!colorFinite(radiance)) {
    return std::nullopt;
  }
  return radiance;
}

gman::Shading GMANRayTracer::shadeHit(GMANRayInterface const& hitPrimitive, GMANRay const& ray,
                                      GMANHit const& hit) const {
  PassIrradiance const indirect(indirectPass, hit, ray.getDirection());
  gman::Shading shading = shadeLit(hitPrimitive, gman::hitSurfacePoint(ray, hit), indirect.ptr);
  std::optional<GMANColor> const radiance = emitterRadiance(hitPrimitive, ray, hit);
  if (radiance) {
    shading.Ci += *radiance;
  }
  return shading;
}

/*
 * RenderMan API GMANRaytraceRenderer
 *
 */

// default constructor
GMANRaytraceRenderer::GMANRaytraceRenderer() : GMANRenderer(), occluder(bvh) {};

// default destructor
GMANRaytraceRenderer::~GMANRaytraceRenderer() {};

GMANRaytraceRenderer::RayHit GMANRaytraceRenderer::nearestHit(GMANRay const& ray) const {
  RayHit result;
  GMANRayInterface const* hitPrimitive = nullptr;
  if (bvh.nearestHit(ray, result.hit, hitPrimitive)) {
    result.primitive = hitPrimitive;
  }
  return result;
}

void GMANRaytraceRenderer::shadeSample(GMANViewingSystem* viewingSys, GMANMatrix4 const& cameraToWorld,
                                       GMANColor const& background, RtFloat rasterX, RtFloat rasterY, int sampleX,
                                       int sampleY, gman::IndirectPass const* indirectPass,
                                       GMANRayOccluder const& shadingOccluder, gman::TextureCache* textureCache,
                                       GMANAreaLightSample& areaSample) const {
  GMANRay ray = viewingSys->cameraRay(rasterX, rasterY);
  RayHit hit = nearestHit(ray);
  if (hit.primitive == nullptr) {
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
  // into a child GMANRayTracer at depth 1 (see GMANRayTracer::trace()),
  // carrying indirectPass, textureCache and areaSample forward so a
  // reflected or refracted hit gets indirect light too, samples through this
  // worker's cache and draws on the same sample's area emitters.
  GMANRayTracer const tracer(bvh, shadingOccluder, cameraToWorld, background, 0, indirectPass, textureCache,
                             &areaSample);

  for (int layer = 0; layer < gman::kMaxCompositeLayers && hit.primitive != nullptr; ++layer) {
    gman::Shading const shading = tracer.shadeHit(*hit.primitive, ray, hit.hit);
    accumulated += gman::multiplyChannels(transmission, shading.Ci);
    transmission = gman::multiplyChannels(transmission, gman::oneMinus(coverageOf(shading.Oi)));
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

void GMANRaytraceRenderer::shadeRow(GMANViewingSystem* viewingSys, GMANMatrix4 const& cameraToWorld,
                                    GMANColor const& background, GMANOptions::RasterInfo const& raster, int width,
                                    int xsamples, int ysamples, int py, gman::IndirectPass const* indirectPass,
                                    GMANRayOccluder const& shadingOccluder, gman::TextureCache* textureCache,
                                    GMANAreaLights const& areaLights) const {
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
        GMANAreaLightSample areaSample(areaLights, raster.rxmin * xsamples + sampleX,
                                       raster.rymin * ysamples + sampleY);
        shadeSample(viewingSys, cameraToWorld, background, rasterX, rasterY, sampleX, sampleY, indirectPass,
                    shadingOccluder, textureCache, areaSample);
      }
    }
  }
}

namespace {

// Loads the pass options names and prepares it over world, or returns an
// empty owning pointer when no Option named one. The caller's BVH must
// already be built into occluder: prepare() may query transmission
// through it.
gman::IndirectPassPtr loadAndPrepareIndirectPass(GMANOptions const& options, GMANWorldManager& world,
                                                 GMANRayOccluder const& occluder) {
  if (options.getIndirectPass().empty()) {
    return {nullptr, [](gman::IndirectPass*) {}};
  }
  gman::IndirectPassPtr pass = gman::loadIndirectPass(options.getIndirectPass());
  if (pass != nullptr) {
    pass->prepare(world, occluder, options);
  }
  return pass;
}

// The draws per emitter and shading point options names: Option "raytracer"
// "integer arealightsamples", at least 1. Anything else, including a value of
// another type, logs a warning and answers kDefaultAreaLightSamples.
std::uint32_t areaLightSamples(GMANOptions const& options) {
  GMANOptions::NamedOptionValue const* const named = options.getNamedOption("raytracer", "arealightsamples");
  if (named == nullptr) {
    return kDefaultAreaLightSamples;
  }
  auto const* const integers = std::get_if<std::vector<RtInt>>(named);
  if (integers == nullptr || integers->empty() || integers->front() < 1) {
    warning("gmanraytracer: Option \"raytracer\" \"arealightsamples\" must be an integer of at least 1; using {}.",
            kDefaultAreaLightSamples);
    return kDefaultAreaLightSamples;
  }
  return static_cast<std::uint32_t>(integers->front());
}

// world's area emitters, with the draw count options names: every emitter
// gman::emitters enumerates that has a shape. Warns once, through
// gman::emitters, of each area-light primitive it skips.
GMANAreaLights areaLightsOf(GMANWorldManager& world, GMANOptions const& options) {
  GMANAreaLights areaLights;
  areaLights.emitters = gman::emitters(world);
  std::erase_if(areaLights.emitters, [](gman::Emitter const& emitter) { return emitter.shape == nullptr; });
  areaLights.drawsPerEmitter = areaLightSamples(options);
  return areaLights;
}

// One shading occluder per worker, each built on that worker's own cache and
// in the same order as textureCaches, so a blocker's shader samples its
// textures through the cache of the worker whose shadow ray crossed it.
std::vector<GMANRayOccluder> shadingOccludersFor(GMANRayBVH const& bvh, GMANMatrix4 const& cameraToWorld,
                                                 std::vector<gman::TextureCache>& textureCaches) {
  std::vector<GMANRayOccluder> shadingOccluders;
  shadingOccluders.reserve(textureCaches.size());
  for (gman::TextureCache& textureCache : textureCaches) {
    shadingOccluders.emplace_back(bvh, cameraToWorld, textureCache);
  }
  return shadingOccluders;
}

} // namespace

void GMANRaytraceRenderer::render(GMANFrameBuffer* frameBuffer, GMANViewingSystem* viewingSys,
                                  GMANOptions const& options, GMANAttributes const& /*attributes*/) {
  // Rebuilt every call, discarding any tree a prior call built: simpler
  // than tracking whether a stale tree needs invalidating, and a render
  // pass over the whole frame already costs far more than one extra tree
  // build.
  bvh.build(worldManager);

  // Gathered once per render() call from the same world bvh was built from,
  // so the emitters name primitives the tree holds.
  GMANAreaLights const areaLights = areaLightsOf(worldManager, options);

  // Loaded, prepared and released here, once per render() call: a pass
  // kept across calls would answer for a scene that no longer exists,
  // since bvh above is rebuilt every time too.
  gman::IndirectPassPtr const ownedIndirectPass = loadAndPrepareIndirectPass(options, worldManager, occluder);
  gman::IndirectPass* const indirectPass = ownedIndirectPass.get();

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

  // One cache per worker, cold at the start of every render: a worker's
  // rows are the only code that touches its cache. Each worker shades
  // through an occluder built on its own cache.
  std::vector<gman::TextureCache> textureCaches(
      static_cast<std::size_t>(gman::parallelWorkers(height, requestedWorkers)));
  std::vector<GMANRayOccluder> const shadingOccluders = shadingOccludersFor(bvh, cameraToWorld, textureCaches);
  rowWorker.assign(static_cast<std::size_t>(height), 0);

  gman::parallelFor(
      height,
      [&](RtInt row, RtInt worker) {
        rowWorker[static_cast<std::size_t>(row)] = worker;
        shadeRow(viewingSys, cameraToWorld, background, raster, width, xsamples, ysamples, row, indirectPass,
                 shadingOccluders[static_cast<std::size_t>(worker)], &textureCaches[static_cast<std::size_t>(worker)],
                 areaLights);
      },
      requestedWorkers);

  GMANOptions::PixelFilterStruct const& pf = options.getPixelFilter();
  sampleBuffer->resolve(frameBuffer, pf.filterfunc, pf.xwidth, pf.ywidth);
}

void GMANRaytraceRenderer::setWorkers(RtInt workers) { requestedWorkers = workers; }

GMANWorldManager* GMANRaytraceRenderer::getWorldManager(void) { return &worldManager; }

// return its object manager
GMANObjectManager* GMANRaytraceRenderer::getObjectManager(void) { return &objectManager; }
