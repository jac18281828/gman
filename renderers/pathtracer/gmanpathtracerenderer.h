/* SPDX-License-Identifier: LGPL-2.1-or-later */

/* This is part of GMAN, a RenderMan-compatible renderer.
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

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "gmanemitter.h"
#include "gmanlightsourcemgr.h"
#include "gmanlinearworldmanager.h"
#include "gmanray.h"
#include "gmanraybvh.h"
#include "gmanrayobjectmanager.h"
#include "gmanrenderer.h"
#include "gmansamplebuffer.h"
#include "gmanshading.h"
#include "gmantexture.h"
#include "gmanworldmanager.h"
#include "ri.h"

/*
 * RenderMan API GMANPathtraceRenderer
 *
 * Unidirectional path tracing with next-event estimation over every point,
 * spot, distant and area light in the scene, through the shared
 * gman::Emitter interface. Every path carries a throughput beta, starting
 * white, and a radiance L, starting black; at each hit, before anything
 * else, an eligible area-light primitive the ray may still credit adds its
 * own Le, then the path may pass straight through (coverage), otherwise it
 * evaluates next-event estimation against one emitter chosen by its
 * contribution, then samples the surface's own BSDF (gman::bsdf) for its
 * next direction, subject to Russian roulette from its fourth vertex on. An
 * escaped ray adds beta times the background and ends the path. The
 * emitter set is gman::emitters(worldManager), gathered once at the top of
 * render(); ambientlight contributes nothing, since bounce light is what
 * it faked. A non-finite path -- a NaN or infinite channel anywhere in its
 * throughput, radiance or coverage estimate -- is dropped: it contributes
 * zero to the slot's sums and is counted, never filtered. render() runs its
 * rows through gman::parallelFor; each row is its own unit of work, reading
 * the BVH, the emitter set, the viewing system, the options and the
 * background as const and writing only its own slots of the sample buffer,
 * its own dropped-path count and the gman::TextureCache of the worker that
 * runs it. Every random draw is a pure function of its slot, so the image,
 * its alpha, its depth and droppedPathCount() are the same at every worker
 * count.
 */
class GMAN_EXPORT GMANPathtraceRenderer : public GMANRenderer {
private:
  GMANRayObjectManager objectManager;

  GMANLinearWorldManager worldManager;

  // Rebuilt from worldManager at the top of every render() call, as
  // GMANRaytraceRenderer's own bvh is.
  GMANRayBVH bvh;

  // The real per-slot colour and alpha store; resolved into frameBuffer
  // at the end of render(). getDepth reads its resolved depth.
  std::unique_ptr<GMANSampleBuffer> sampleBuffer;

  // gman::emitters(worldManager): every delta light and eligible area
  // light in the scene. Gathered once at the top of render(); every vertex
  // samples the whole set, whatever its own hit primitive's own
  // appearance says.
  std::vector<gman::Emitter> emitters;

  // The last render() call's count of paths dropped for a non-finite
  // channel, and of ambientlight lights its light-gathering walk
  // skipped.
  std::size_t droppedPaths = 0;
  std::size_t skippedAmbientLights = 0;

  // Weights next-event estimation and an emitter hit by the power
  // heuristic where both can reach the same area light; false is
  // light-only sampling, next-event estimation alone at weight 1. Not a
  // RIB option: a correctness/variance proof's own switch, set only
  // through setMultipleImportanceSampling.
  bool multipleImportanceSamplingEnabled = true;

  // The worker count render() requests from gman::parallelFor; 0 lets it
  // choose the hardware's concurrency. Not a RIB option or an environment
  // variable: a determinism proof's own switch, set only through
  // setWorkers.
  RtInt requestedWorkers = 0;

  // The worker that rendered each row of the last render() call, indexed
  // by row. render() sizes it to the frame's height before its rows run
  // and each row writes only its own element, so no two threads write one
  // location.
  std::vector<RtInt> rowWorker;

  // Fills emitters from worldManager and skippedAmbientLights from its own
  // separate count -- gman::emitters excludes ambient lights silently,
  // with no count of its own to report.
  void gatherLights();

public:
  GMANPathtraceRenderer(); // default constructor

  ~GMANPathtraceRenderer(); // default destructor

  RtVoid illuminance(RtInt /*i*/, GMANPoint const& /*p*/, GMANVector const& /*axis*/, RtFloat /*angle*/) {}
  RtVoid illuminate(RtInt /*i*/, GMANPoint const& /*p*/, GMANVector const& /*axis*/, RtFloat /*angle*/) {}
  RtVoid solar(RtInt /*i*/, GMANVector const& /*axis*/, RtFloat /*angle*/) {}

  virtual void render(GMANFrameBuffer* frameBuffer, GMANViewingSystem* viewingSys, GMANOptions const& options,
                      GMANAttributes const& attributes);

  // Camera-space z of the nearest sample at (x, y), RI_INFINITY where no
  // path's camera ray ever hit anything.
  RtFloat getDepth(int x, int y) const { return sampleBuffer ? sampleBuffer->getResolvedDepth(x, y) : RI_INFINITY; }

  // The last render() call's count of dropped paths, as getDepth reports
  // its resolved depth.
  std::size_t droppedPathCount() const { return droppedPaths; }

  // Exported with GMANPathtraceRenderer itself, in every build: a
  // correctness/variance switch, for a test that constructs this renderer
  // by hand, never a RIB surface. This header is not installed, so no
  // out-of-tree consumer reaches it regardless.
  void setMultipleImportanceSampling(bool enabled);

  // Exported with GMANPathtraceRenderer itself, in every build: the worker
  // count for the next render() call, 0 for the hardware's concurrency. For
  // a test that constructs this renderer by hand, never a RIB surface. This
  // header is not installed, so no out-of-tree consumer reaches it
  // regardless.
  void setWorkers(RtInt workers);

  // The last render() call's worker for each row, one entry per row of the
  // frame; every entry is 0 when one worker ran them all. Exported as
  // setWorkers is, and for the same tests.
  std::vector<RtInt> const& rowWorkers() const { return rowWorker; }

  virtual GMANWorldManager* getWorldManager(void);

  virtual GMANObjectManager* getObjectManager(void);
};
