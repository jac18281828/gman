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

#include "gman/export.h"
#include "gmandenoise.h"
#include "gmanemitter.h"
#include "gmanoptions.h"
#include "gmanpoint.h"
#include "gmanprimitivefactory.h"
#include "gmanraybvh.h"
#include "gmanrayprimitivefactory.h"
#include "gmanrenderer.h"
#include "gmansamplebuffer.h"
#include "gmanshading.h"
#include "gmanvector.h"
#include "gmanviewingsystem.h"
#include "gmanworld.h"
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
 * emitter set is gman::emitters(world), gathered once at the top of
 * render(); ambientlight contributes nothing, since bounce light is what
 * it faked. A non-finite path -- a NaN or infinite channel anywhere in its
 * throughput, radiance or coverage estimate -- is dropped: it contributes
 * zero to the slot's sums and is counted rather than filtered. render() runs its
 * rows through gman::parallelFor; each row is its own unit of work, reading
 * the BVH, the emitter set, the viewing system, the options and the
 * background as const and writing only its own slots of the sample buffer,
 * its own dropped-path count and the gman::TextureCache of the worker that
 * runs it. Every random draw is a pure function of its slot, so the image,
 * its alpha, its depth and droppedPathCount() are the same at every worker
 * count.
 *
 * Option "pathtracer" "integer samples" is each slot's pattern size and its
 * maximum path count. With PixelVariance 0 every slot traces that many
 * paths. Under a positive PixelVariance a slot samples in two stages: a
 * pilot of m paths estimates the largest per-channel variance of the
 * radiance and the coverage, which sizes a fresh batch of n paths with a
 * standard error, per channel and absolute, strictly below PixelVariance,
 * from m to the maximum less m; the fresh batch is the slot's whole
 * estimate and the pilot contributes nothing to its colour, alpha or depth.
 * The estimate is unbiased because n is fixed before the fresh paths are
 * drawn. m is Option "pathtracer" "integer minsamples" when that is an
 * integer of at least 2, otherwise the smallest integer whose square
 * reaches the maximum, but not below 2. The samples option bounds the paths
 * a slot traces, the pilot included, and the pilot is skipped when 2m
 * reaches it: the slot then draws what a PixelVariance of 0 draws. The
 * image and each slot's path count stay the same at every worker count.
 *
 * Option "pathtracer" "string denoise" ["atrous"] filters the resolved image
 * through gman::atrousDenoise, the edge-avoiding a-trous wavelet filter, as
 * the frame's last step before exposure, the imager and quantization. It is
 * off by default: absent or "none" computes no guide and writes the image an
 * unfiltered render writes, and any other value warns once and renders
 * without the filter. The filter is guided by the first hit of every camera
 * ray: its albedo, from gman::albedo, and its normal, camera space and facing
 * the camera. Both resolve through the pixel filter beside the colour, and a
 * pilot's paths add nothing to either. Option "pathtracer" "integer
 * denoiseiterations" is the level count, 5 by default and at least 1; "float
 * denoisecolor", "float denoisenormal" and "float denoisealbedo" are the
 * three weights' sigmas, each finite and positive, 0.5 and 0.2 by default for
 * the normal and the albedo and, for the colour, the lower median of the
 * positive colour norms in the image. An invalid value warns once and takes
 * its default. One info line reports the parameters the filter used, the
 * colour sigma included. Alpha and depth stay as sampling resolved them.
 */
class GMAN_EXPORT GMANPathtraceRenderer : public GMANRenderer {
private:
  GMANRayPrimitiveFactory primitiveFactory;

  // Rebuilt from the world render() borrows, at the top of every render()
  // call, as GMANRaytraceRenderer's own bvh is.
  GMANRayBVH bvh;

  // The real per-slot colour and alpha store; resolved into frameBuffer
  // at the end of render(). getDepth reads its resolved depth.
  std::unique_ptr<GMANSampleBuffer> sampleBuffer;

  // gman::emitters(world): every delta light and eligible area
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

  // The paths each slot traced in the last render() call, indexed by
  // sampleY * (width * xsamples) + sampleX. render() sizes it before its
  // rows run and each row writes only its own slots.
  std::vector<std::uint32_t> slotPaths;

  // The last render() call's resolved colour, albedo and normal when it ran
  // the denoise stage; empty otherwise.
  gman::GuidedImage guided;

  // Fills emitters from world and skippedAmbientLights from its own
  // separate count -- gman::emitters excludes ambient lights silently,
  // with no count of its own to report.
  void gatherLights(GMANWorld const& world);

  // Filters guided.color through gman::atrousDenoise, writes the result into
  // frameBuffer's pixels, leaving its alpha, and logs the parameters used.
  void denoiseFrame(GMANFrameBuffer* frameBuffer, gman::AtrousParameters const& parameters);

public:
  GMANPathtraceRenderer(); // default constructor

  ~GMANPathtraceRenderer(); // default destructor

  RtVoid illuminance(RtInt /*i*/, GMANPoint const& /*p*/, GMANVector const& /*axis*/, RtFloat /*angle*/) {}
  RtVoid illuminate(RtInt /*i*/, GMANPoint const& /*p*/, GMANVector const& /*axis*/, RtFloat /*angle*/) {}
  RtVoid solar(RtInt /*i*/, GMANVector const& /*axis*/, RtFloat /*angle*/) {}

  virtual void render(GMANWorld const& world, GMANFrameBuffer* frameBuffer, GMANViewingSystem* viewingSys,
                      GMANOptions const& options, GMANAttributes const& attributes);

  // Camera-space z of the nearest sample at (x, y), RI_INFINITY where no
  // path's camera ray ever hit anything.
  RtFloat getDepth(int x, int y) const { return sampleBuffer ? sampleBuffer->getResolvedDepth(x, y) : RI_INFINITY; }

  // The last render() call's count of dropped paths, as getDepth reports
  // its resolved depth.
  std::size_t droppedPathCount() const { return droppedPaths; }

  // Exported with GMANPathtraceRenderer itself, in every build: a
  // correctness/variance switch, for a test that constructs this renderer
  // by hand; RIB cannot set it. This header is not installed, so no
  // out-of-tree consumer reaches it regardless.
  void setMultipleImportanceSampling(bool enabled);

  // Exported with GMANPathtraceRenderer itself, in every build: the worker
  // count for the next render() call, 0 for the hardware's concurrency. For
  // a test that constructs this renderer by hand; RIB cannot set it. This
  // header is not installed, so no out-of-tree consumer reaches it
  // regardless.
  void setWorkers(RtInt workers);

  // The last render() call's worker for each row, one entry per row of the
  // frame; every entry is 0 when one worker ran them all. Exported as
  // setWorkers is, and for the same tests.
  std::vector<RtInt> const& rowWorkers() const { return rowWorker; }

  // The last render() call's path count for each slot of the sample grid,
  // indexed by sampleY * (width * xsamples) + sampleX, the coordinates a
  // slot is stored under. A count includes the slot's pilot. Exported as
  // setWorkers is, and for the same tests.
  std::vector<std::uint32_t> const& slotPathCounts() const { return slotPaths; }

  // The last render() call's resolved colour, as it stood before the filter,
  // with the albedo and normal guides beside it; empty after a render that
  // ran no denoise stage. Exported as setWorkers is, and for the same tests.
  gman::GuidedImage const& guidedImage() const { return guided; }

  virtual GMANPrimitiveFactory* getPrimitiveFactory(void);
};
