/* SPDX-License-Identifier: LGPL-2.1-or-later */

/* This is part of GMAN, a RenderMan-compatible renderer.
 *
 * Copyright (c) 2001, 2000, 1999 John Cairns
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

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "gman/export.h"
#include "gmancolor.h"
#include "gmanemitter.h"
#include "gmanlightsourcemgr.h"
#include "gmanlinearworldmanager.h"
#include "gmanmatrix4.h"
#include "gmanoptions.h"
#include "gmanpoint.h"
#include "gmanprimitivefactory.h"
#include "gmanray.h"
#include "gmanraybvh.h"
#include "gmanrayoccluder.h"
#include "gmanrayprimitive.h"
#include "gmanrayprimitivefactory.h"
#include "gmanrenderer.h"
#include "gmansamplebuffer.h"
#include "gmanshading.h"
#include "gmantrace.h"
#include "gmanvector.h"
#include "gmanviewingsystem.h"
#include "gmanworldmanager.h"
#include "ri.h"

namespace gman {
class IndirectPass;
} // namespace gman

/*
 * The area emitters a render lights every shading point from, and how many
 * draws each takes there. A render reads the count from
 * Option "raytracer" "integer arealightsamples"; the default is 4, and a
 * value below 1 or one that is not an integer logs a warning once and uses
 * the default. emitters holds area emitters only.
 */
struct GMANAreaLights {
  std::vector<gman::Emitter> emitters;
  std::uint32_t drawsPerEmitter = 0;
};

/*
 * One camera sample's draws on a render's area emitters. Each shading point
 * the sample reaches, a composite layer or a hit a shader's trace() shades,
 * takes drawsPerEmitter points on every emitter but the one it lies on,
 * drawn as a stratified pattern of its own: the pattern is a pure function
 * of the camera sample's absolute sample-grid coordinate, the emitter and
 * the shading point's place in the order that sample's shading runs, so no
 * worker count or row order changes a draw. One thread owns a sample.
 */
class GMANAreaLightSample {
public:
  GMANAreaLightSample(GMANAreaLights const& lights, RtInt gridX, RtInt gridY)
      : lights(lights), gridX(gridX), gridY(gridY) {}

  // The point lights of this sample's next shading point, at P on
  // hitPrimitive: one per draw on each emitter but hitPrimitive itself,
  // except a draw whose density is not positive and finite, whose radiance is
  // black or whose colour is not finite. A light sits at its draw's shadow
  // target, coloured by the emitter's radiance divided by pi, the draw's
  // solid-angle density and the draw count, times the squared distance from
  // P, so GMANLight::sample's inverse-square falloff leaves a shader's
  // diffuse the emitter's irradiance over pi.
  std::vector<GMANLight> draw(GMANPoint const& P, GMANRayPrimitive const& hitPrimitive);

  std::vector<gman::Emitter> const& emitters() const { return lights.emitters; }

private:
  GMANAreaLights const& lights;
  RtInt gridX;
  RtInt gridY;
  std::uint32_t nextSite = 0;
};

/*
 * The ray tracer's own gman::Tracer: casts R from P (offset off Ng, the
 * same self-shadow discipline compositeLayers and
 * GMANRayOccluder::transmission already take), composites the layers along
 * the ray front to back, each shaded at depth + 1, and returns that colour
 * plus the transmission left over background -- the colour a miss returns, so
 * a ray crossing partly opaque surfaces and leaving the scene reads them over
 * the background a camera ray sees. Returns background alone on a miss or
 * once depth reaches kMaxTraceDepth (gmanraytracerenderer.cpp), without
 * casting a ray or touching bvh at all in that last case. A public,
 * standalone class so a unit test builds one directly, against a GMANRayBVH
 * and GMANRayOccluder it controls. Not GMAN_EXPORT: only its own tests use
 * it, and they compile the plugin's own sources directly rather than linking
 * the installed library.
 *
 * Bound to a GMANAreaLightSample, it lights every hit it shades from that
 * sample's area emitters through the hit's own shader: each draw is one more
 * point light in the shader's illuminance loop, shadowed through the path
 * tracer's own area walk (gman::shadowWalk), and a hit on an emitter adds
 * that emitter's radiance to the hit's colour when the hit faces the ray.
 * Bound to none, it shades through its hits' own lights alone.
 */
class GMANRayTracer : public gman::Tracer {
public:
  GMANRayTracer(GMANRayBVH const& bvh, GMANRayOccluder const& occluder, GMANMatrix4 const& cameraToWorld,
                GMANColor const& background, int depth, gman::IndirectPass const* indirectPass = nullptr,
                gman::TextureCache* textureCache = nullptr, GMANAreaLightSample* areaSample = nullptr)
      : bvh(bvh), occluder(occluder), cameraToWorld(cameraToWorld), background(background), depth(depth),
        indirectPass(indirectPass), textureCache(textureCache), areaSample(areaSample) {}

  GMANColor trace(GMANPoint const& P, GMANVector const& R, GMANVector const& Ng,
                  RtFloat surfaceMagnitude) const override;

  // Shades hit, ray's nearest hit on hitPrimitive, with this tracer answering
  // the shader's own trace() calls: the shader's Ci, plus the emitter's
  // radiance where hitPrimitive is an area emitter whose face looks at ray's
  // origin, and its Oi unchanged.
  gman::Shading shadeHit(GMANRayPrimitive const& hitPrimitive, GMANRay const& ray, GMANHit const& hit) const;

  // What compositeLayers accumulates: the colour the layers add, each through
  // the transmission left before it, and the transmission left after the last.
  struct Composited {
    GMANColor colour;
    GMANColor transmission;
  };

  // The front-to-back composite, the RISpec's own "over", of the layers along
  // ray, whose nearest hit is hit on hitPrimitive: each layer adds its Ci
  // through the transmission left and shrinks that transmission by its own
  // opacity. Every shipped shader's Ci already carries its own Os factor, so no
  // second Oi multiplies the running colour. Stops once the transmission is
  // negligible, so an opaque layer casts no further ray, or after
  // gman::kMaxCompositeLayers layers. Every layer shades through this tracer,
  // so a layer adds no depth. The background is in neither result: the caller
  // adds it through the transmission left.
  Composited compositeLayers(GMANRay const& ray, GMANHit const& hit, GMANRayPrimitive const& hitPrimitive) const;

private:
  gman::Shading shadeLit(GMANRayPrimitive const& hitPrimitive, gman::SurfacePoint const& point,
                         GMANColor const* indirect) const;
  std::optional<GMANColor> emitterRadiance(GMANRayPrimitive const& hitPrimitive, GMANRay const& ray,
                                           GMANHit const& hit) const;

  GMANRayBVH const& bvh;
  GMANRayOccluder const& occluder;
  GMANMatrix4 const& cameraToWorld;
  GMANColor background;
  int depth;

  // Null when no Option named a pass, in which case every hit this tracer
  // shades passes null indirect light to gman::shade. Carried into every
  // child GMANRayTracer trace() builds, so a reflected or refracted hit
  // gets indirect light too.
  gman::IndirectPass const* indirectPass;

  // The cache this tracer's own hit samples textures through; null means
  // gman::textureCache()'s process cache. Carried into every child
  // GMANRayTracer trace() builds, so a reflected or refracted hit samples
  // through the cache of the tracer that cast it, which no other thread
  // owns. Non-owning; the caller keeps it alive.
  gman::TextureCache* textureCache;

  // The camera sample whose area emitters light this tracer's hits; null
  // means none do. Carried into every child GMANRayTracer trace() builds,
  // so a reflected or refracted hit takes its own draws from the same
  // sample. Non-owning; the caller keeps it alive.
  GMANAreaLightSample* areaSample;
};

/*
 * RenderMan API GMANRaytraceRenderer
 *
 * A renderer implementing a ray tracing model.
 *
 */

class GMAN_EXPORT GMANRaytraceRenderer : public GMANRenderer {
private:
  GMANRayPrimitiveFactory primitiveFactory;

  GMANLinearWorldManager worldManager;

  // Rebuilt from worldManager at the top of every render() call (see
  // render()'s own comment); declared after worldManager and before
  // occluder so occluder's reference binds to an already-constructed
  // object.
  GMANRayBVH bvh;

  // The bvh-only occluder: loadAndPrepareIndirectPass hands it to the
  // indirect pass, which shoots through it from threads with no texture
  // cache of their own. render() shades every row through one occluder per
  // worker instead, each built on that worker's cache, so the shadow rays of
  // a shaded hit evaluate its blockers' shaders. Declared after bvh so its
  // reference binds to an already-constructed object.
  GMANRayOccluder occluder;

  // The real per-sample visibility test and colour store; resolved into
  // frameBuffer at the end of render(). getDepth reads its resolved depth.
  std::unique_ptr<GMANSampleBuffer> sampleBuffer;

  // The nearest ray-primitive hit and the ray primitive it lies on; both
  // null (hit.primitive null, primitive null) when nothing is hit. Carrying
  // the ray primitive here, rather than re-deriving it from hit.primitive
  // afterward, keeps the downcast to GMANRayPrimitive to GMANRayBVH::build's
  // own loop.
  struct RayHit {
    GMANHit hit;
    GMANRayPrimitive const* primitive = nullptr;
  };

  // The worker count render() requests from gman::parallelFor; 0 lets it
  // choose the hardware's concurrency. Set only through setWorkers.
  RtInt requestedWorkers = 0;

  // The worker that rendered each pixel row of the last render(), one entry
  // per row. Each row's body writes its own element, so no two workers
  // touch the same one.
  std::vector<RtInt> rowWorker;

  // Finds the ray primitive nearest ray's origin, through bvh. Reads only
  // state no render() call changes after it builds bvh.
  RayHit nearestHit(GMANRay const& ray) const;

  // Traces, shades and stores one sample -- render()'s per-sample body.
  // background is the sample buffer's seed colour (frameBuffer's corner
  // pixel), what remains after every layer's transmission composites
  // over. indirectPass is null when no Option named one. textureCache is
  // the calling worker's own cache and shadingOccluder the occluder built on
  // it, which every shadow ray of the sample goes through; the sample writes
  // only its own slot of sampleBuffer, so rows shaded on different workers
  // share no state. areaSample is this sample's own draws on the render's
  // area emitters, which every shading point the sample reaches takes from.
  void shadeSample(GMANViewingSystem* viewingSys, GMANMatrix4 const& cameraToWorld, GMANColor const& background,
                   RtFloat rasterX, RtFloat rasterY, int sampleX, int sampleY, gman::IndirectPass const* indirectPass,
                   GMANRayOccluder const& shadingOccluder, gman::TextureCache* textureCache,
                   GMANAreaLightSample& areaSample) const;

  // Shades every sample of pixel row py, in raster order, through
  // shadingOccluder and textureCache, the calling worker's own -- one
  // gman::parallelFor body. Writes only the sample rows py owns. Each sample
  // draws on areaLights, keyed by its absolute sample-grid coordinate.
  void shadeRow(GMANViewingSystem* viewingSys, GMANMatrix4 const& cameraToWorld, GMANColor const& background,
                GMANOptions::RasterInfo const& raster, int width, int xsamples, int ysamples, int py,
                gman::IndirectPass const* indirectPass, GMANRayOccluder const& shadingOccluder,
                gman::TextureCache* textureCache, GMANAreaLights const& areaLights) const;

public:
  GMANRaytraceRenderer(); // default constructor

  ~GMANRaytraceRenderer(); // default destructor

  RtVoid illuminance(RtInt /*i*/, GMANPoint const& /*p*/, GMANVector const& /*axis*/, RtFloat /*angle*/) {}
  RtVoid illuminate(RtInt /*i*/, GMANPoint const& /*p*/, GMANVector const& /*axis*/, RtFloat /*angle*/) {}
  RtVoid solar(RtInt /*i*/, GMANVector const& /*axis*/, RtFloat /*angle*/) {}

  /*
   * Apply a ray-tracing environment to the objects in object manager
   * to generate a frameBuffer output. Pixel rows render through
   * gman::parallelFor, one gman::TextureCache and one shading
   * GMANRayOccluder per worker; every sample's
   * value depends on the scene alone, so the image is byte-identical at
   * any worker count.
   *
   * An area light lights every shading point through its shader's own
   * illuminance loop: each emitter gman::emitters enumerates takes
   * Option "raytracer" "integer arealightsamples" draws at a point (default
   * 4), each a point light on the emitter's surface whose shadow passes
   * glass and cut-outs as the path tracer's does, and a ray that meets an
   * emitter sees its radiance. A scene with no area emitter shades through
   * its delta lights alone. An emitter gman::emitters skips as ineligible or
   * unsupported is warned about once.
   */
  virtual void render(GMANFrameBuffer* frameBuffer, GMANViewingSystem* viewingSys, const GMANOptions& options,
                      const GMANAttributes& attributes);

  // The worker count render() requests from gman::parallelFor; 0, the
  // default, lets it choose the hardware's concurrency. Takes effect on the
  // next render(); the image is the same at any worker count.
  void setWorkers(RtInt workers);

  // The worker that rendered each pixel row of the last render(), one entry
  // per row; empty before the first. Lets a test see how rows spread over
  // workers.
  std::vector<RtInt> const& rowWorkers() const { return rowWorker; }

  // Camera-space z of the nearest sample at (x, y), RI_INFINITY where none
  // hit -- the sample buffer's own uncovered value. Unlike the z-buffer's
  // getDepth, this is not post-projection z.
  RtFloat getDepth(int x, int y) const { return sampleBuffer ? sampleBuffer->getResolvedDepth(x, y) : RI_INFINITY; }

  // return its world manager
  virtual GMANWorldManager* getWorldManager(void);

  // return its object manager
  virtual GMANPrimitiveFactory* getPrimitiveFactory(void);
};
