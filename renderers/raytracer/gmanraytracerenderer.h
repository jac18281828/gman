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
#include "gmanray.h"
#include "gmanrayobjectmanager.h"
#include "gmanrenderer.h"
#include "gmansamplebuffer.h"
#include "gmanshading.h"
#include "gmanworldmanager.h"
#include "ri.h"

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

  // The real per-sample visibility test and colour store; resolved into
  // frameBuffer at the end of render(). getDepth reads its resolved depth.
  std::unique_ptr<GMANSampleBuffer> sampleBuffer;

  // The nearest ray-primitive hit and the appearance it was declared
  // under; both null (hit.primitive null, appearance null) when nothing
  // is hit. Carrying the appearance here, rather than re-deriving it from
  // hit.primitive afterward, keeps the downcast to GMANRayInterface to
  // nearestHit's own loop.
  struct RayHit {
    GMANHit hit;
    gman::Appearance const* appearance = nullptr;
  };

  // Walks worldManager for the ray primitive nearest ray's origin. The
  // BVH is future work; this walk is linear in the primitive count.
  RayHit nearestHit(GMANRay const& ray);

  // Traces, shades and stores one sample -- render()'s per-sample body.
  void shadeSample(GMANViewingSystem* viewingSys, GMANMatrix4 const& cameraToWorld, RtFloat rasterX, RtFloat rasterY,
                   int sampleX, int sampleY);

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
