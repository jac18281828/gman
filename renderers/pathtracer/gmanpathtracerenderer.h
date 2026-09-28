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
 * Unidirectional path tracing with next-event estimation over point,
 * spot and distant lights. Every path carries a throughput beta,
 * starting white, and a radiance L, starting black; at each hit it may
 * pass straight through (coverage), otherwise it evaluates next-event
 * estimation against one light chosen by its contribution, then samples
 * the surface's own BSDF (gman::bsdf) for its next direction, subject to
 * Russian roulette from its fourth vertex on. An escaped ray adds
 * beta times the background and ends the path. The light set is the
 * union of every primitive's own Appearance::lights, gathered once at
 * the top of render(); ambientlight contributes nothing, since bounce
 * light is what it faked. A non-finite path -- a NaN or infinite channel
 * anywhere in its throughput, radiance or coverage estimate -- is
 * dropped: it contributes zero to the slot's sums and is counted, never
 * filtered. render() is serial; each row is its own unit of work, reading
 * the BVH, the light set, the options and the background as const and
 * writing only its own slots of the sample buffer and the dropped-path
 * count it is handed, so a later caller can shard rows across
 * gman::parallelFor workers unchanged.
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

  // The union of every primitive's own Appearance::lights, point, spot
  // and distant alike, deduplicated by pointer in first-seen order.
  // Gathered once at the top of render(); every vertex samples the whole
  // set, whatever its own hit primitive's own list says.
  std::vector<GMANLight const*> lights;

  // The last render() call's count of paths dropped for a non-finite
  // channel, and of ambientlight lights its light-gathering walk
  // skipped.
  std::size_t droppedPaths = 0;
  std::size_t skippedAmbientLights = 0;

  // Walks worldManager once, filling lights and skippedAmbientLights.
  void gatherLights();

public:
  GMANPathtraceRenderer(); // default constructor

  ~GMANPathtraceRenderer(); // default destructor

  RtVoid illuminance(RtInt /*i*/, GMANPoint const& /*p*/, GMANVector const& /*axis*/, RtFloat /*angle*/) {}
  RtVoid illuminate(RtInt /*i*/, GMANPoint const& /*p*/, GMANVector const& /*axis*/, RtFloat /*angle*/) {}
  RtVoid solar(RtInt /*i*/, GMANVector const& /*axis*/, RtFloat /*angle*/) {}

  virtual void render(GMANFrameBuffer* frameBuffer, GMANViewingSystem* viewingSys, const GMANOptions& options,
                      const GMANAttributes& attributes);

  // Camera-space z of the nearest sample at (x, y), RI_INFINITY where no
  // path's camera ray ever hit anything.
  RtFloat getDepth(int x, int y) const { return sampleBuffer ? sampleBuffer->getResolvedDepth(x, y) : RI_INFINITY; }

  // The last render() call's count of dropped paths, as getDepth reports
  // its resolved depth.
  std::size_t droppedPathCount() const { return droppedPaths; }

  virtual GMANWorldManager* getWorldManager(void);

  virtual GMANObjectManager* getObjectManager(void);
};
