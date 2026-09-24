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

#include <list>
#include <map>
#include <memory>
#include <stack>
#include <string>

#include "gmanlinearworldmanager.h"
#include "gmanlog.h"
#include "gmanobjectmanager.h"
#include "gmanocclude.h"
#include "gmanray.h"
#include "gmanraybvh.h"
#include "gmanrayobjectmanager.h"
#include "gmanrenderer.h"
#include "gmansamplebuffer.h"
#include "gmanshading.h"
#include "gmantrace.h"
#include "gmanworldmanager.h"
#include "ri.h"

/*
 * The ray tracer's own occlusion test: casts a shadow ray from P toward
 * the light and walks bvh's nearestHit repeatedly, each call's hit
 * shrinking the ray's remaining interval and compositing that blocker's
 * own opacity into the running transmission, until the interval is spent
 * or transmission is negligible -- the nearest hit at each step is what
 * lets the walk advance past exactly one blocker per call. A public,
 * standalone class (not nested in GMANRaytraceRenderer) so a unit test
 * can probe transmission() directly against a GMANRayBVH it controls.
 * Not GMAN_EXPORT: only its own tests use it, and they compile the
 * plugin's sources directly rather than linking the installed library.
 */
class GMANRayOccluder : public gman::Occluder {
public:
  explicit GMANRayOccluder(GMANRayBVH const& bvh) : bvh(bvh) {}

  // const on this class's own state; nearestHit on a built bvh is itself
  // const and re-entrant, so concurrent calls here never interleave
  // state.
  GMANColor transmission(GMANLight const& light, GMANPoint const& P, GMANVector const& towardLight,
                         GMANVector const& Ng, RtFloat distance, RtFloat /*surfaceMagnitude*/) const override;

private:
  GMANRayBVH const& bvh;
};

/*
 * The ray tracer's own gman::Tracer: casts R from P (offset off Ng, the
 * same self-shadow discipline shadeSample's composite loop and
 * GMANRayOccluder::transmission already take), shades the nearest hit at
 * depth + 1, and returns background on a miss or once depth reaches
 * kMaxTraceDepth (gmanraytracerenderer.cpp) -- without casting a ray or
 * touching bvh at all in that last case. A public, standalone class for
 * the same reason as GMANRayOccluder above: a unit test builds one
 * directly, against a GMANRayBVH and GMANRayOccluder it controls. Not
 * GMAN_EXPORT, for the same reason as GMANRayOccluder too.
 */
class GMANRayTracer : public gman::Tracer {
public:
  GMANRayTracer(GMANRayBVH const& bvh, GMANRayOccluder const& occluder, GMANMatrix4 const& cameraToWorld,
                GMANColor const& background, int depth)
      : bvh(bvh), occluder(occluder), cameraToWorld(cameraToWorld), background(background), depth(depth) {}

  GMANColor trace(GMANPoint const& P, GMANVector const& R, GMANVector const& Ng,
                  RtFloat /*surfaceMagnitude*/) const override;

private:
  GMANRayBVH const& bvh;
  GMANRayOccluder const& occluder;
  GMANMatrix4 const& cameraToWorld;
  GMANColor background;
  int depth;
};

/*
 * RenderMan API GMANRaytraceRenderer
 *
 * A renderer implementing a ray tracing model.
 *
 */

class GMAN_EXPORT GMANRaytraceRenderer : public GMANRenderer {
private:
  GMANRayObjectManager objectManager;

  GMANLinearWorldManager worldManager;

  // Rebuilt from worldManager at the top of every render() call (see
  // render()'s own comment); declared after worldManager and before
  // occluder so occluder's reference binds to an already-constructed
  // object.
  GMANRayBVH bvh;

  // Shadow rays route through bvh above; declared after it so occluder's
  // reference binds to an already-constructed object.
  GMANRayOccluder occluder;

  // The real per-sample visibility test and colour store; resolved into
  // frameBuffer at the end of render(). getDepth reads its resolved depth.
  std::unique_ptr<GMANSampleBuffer> sampleBuffer;

  // The nearest ray-primitive hit and the appearance it was declared
  // under; both null (hit.primitive null, appearance null) when nothing
  // is hit. Carrying the appearance here, rather than re-deriving it from
  // hit.primitive afterward, keeps the downcast to GMANRayInterface to
  // GMANRayBVH::build's own loop.
  struct RayHit {
    GMANHit hit;
    gman::Appearance const* appearance = nullptr;
  };

  // Finds the ray primitive nearest ray's origin, through bvh.
  RayHit nearestHit(GMANRay const& ray);

  // Traces, shades and stores one sample -- render()'s per-sample body.
  // background is the sample buffer's own seed colour (frameBuffer's
  // corner pixel), what remains after every layer's own transmission
  // composites over.
  void shadeSample(GMANViewingSystem* viewingSys, GMANMatrix4 const& cameraToWorld, GMANColor const& background,
                   RtFloat rasterX, RtFloat rasterY, int sampleX, int sampleY);

public:
  GMANRaytraceRenderer(); // default constructor

  ~GMANRaytraceRenderer(); // default destructor

  RtVoid illuminance(RtInt /*i*/, GMANPoint const& /*p*/, GMANVector const& /*axis*/, RtFloat /*angle*/) {}
  RtVoid illuminate(RtInt /*i*/, GMANPoint const& /*p*/, GMANVector const& /*axis*/, RtFloat /*angle*/) {}
  RtVoid solar(RtInt /*i*/, GMANVector const& /*axis*/, RtFloat /*angle*/) {}

  /*
   * Apply a ray-tracing environment to the objects in object manager
   * to generate a frameBuffer output.
   */
  virtual void render(GMANFrameBuffer* frameBuffer, GMANViewingSystem* viewingSys, const GMANOptions& options,
                      const GMANAttributes& attributes);

  // Camera-space z of the nearest sample at (x, y), RI_INFINITY where none
  // hit -- the sample buffer's own uncovered value. Unlike the z-buffer's
  // getDepth, this is not post-projection z.
  RtFloat getDepth(int x, int y) const { return sampleBuffer ? sampleBuffer->getResolvedDepth(x, y) : RI_INFINITY; }

  // return its world manager
  virtual GMANWorldManager* getWorldManager(void);

  // return its object manager
  virtual GMANObjectManager* getObjectManager(void);
};
