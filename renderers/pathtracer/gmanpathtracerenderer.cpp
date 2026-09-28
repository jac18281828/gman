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

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "gmanattributes.h"
#include "gmanbsdf.h"
#include "gmanemitter.h"
#include "gmanlog.h"
#include "gmanmath.h"
#include "gmanpathtracerenderer.h"
#include "gmanraybbox.h"
#include "gmanrayinterface.h"
#include "gmanrayoccluder.h"
#include "gmansampling.h"
#include "ri.h"

namespace {

// Pins every image this plugin renders; changing it moves all of them.
constexpr std::uint32_t kSeed = 0x9e3779b9u;

// Russian roulette's own survival cap: every path ends with probability 1.
constexpr RtFloat kRouletteCap = (RtFloat)0.95;

GMANColor const kWhite(1.0f, 1.0f, 1.0f);
GMANColor const kBlack(0.0f, 0.0f, 0.0f);

RtFloat meanChannel(GMANColor const& c) { return (c.getRed() + c.getGreen() + c.getBlue()) / (RtFloat)3.0; }

RtFloat maxChannel(GMANColor const& c) { return GMANMax(GMANMax(c.getRed(), c.getGreen()), c.getBlue()); }

bool colorFinite(GMANColor const& c) {
  return std::isfinite(c.getRed()) && std::isfinite(c.getGreen()) && std::isfinite(c.getBlue());
}

bool colorBlack(GMANColor const& c) { return c.getRed() == 0.0f && c.getGreen() == 0.0f && c.getBlue() == 0.0f; }

GMANColor scaleColor(GMANColor const& c, RtFloat s) {
  return GMANColor(c.getRed() * s, c.getGreen() * s, c.getBlue() * s);
}

GMANColor divideColor(GMANColor const& c, RtFloat s) {
  return GMANColor(c.getRed() / s, c.getGreen() / s, c.getBlue() / s);
}

// Os's own channel, clamped to [0, 1] with a NaN mapped to 1: order
// matters here. GMANMin(v, 1) answers 1 for a NaN v, since a NaN
// comparison is always false and the ternary then takes its second
// operand; GMANMax then floors a finite value at 0 as an ordinary clamp
// would.
RtFloat clampCoverageChannel(RtFloat v) { return GMANMax(GMANMin(v, (RtFloat)1.0), (RtFloat)0.0); }

GMANColor clampCoverage(GMANColor const& os) {
  return GMANColor(clampCoverageChannel(os.getRed()), clampCoverageChannel(os.getGreen()),
                   clampCoverageChannel(os.getBlue()));
}

// The plugin's own shadow walk, in gmanrayoccluder.h's transmission's own
// shape: advances toward the light over [origin, origin + maxDistance)
// along wi, compositing each blocker's (1 - Os) + Os * shadowTransmittance
// per channel with no early exit, and answers black past
// gman::kMaxCompositeLayers blockers.
GMANColor shadowWalk(GMANRayBVH const& bvh, GMANPoint const& p, GMANVector const& ng, GMANVector const& wi,
                     RtFloat surfaceMagnitude, RtFloat maxDistance, GMANMatrix4 const& cameraToWorld,
                     gman::TextureCache* textureCache) {
  GMANColor v = kWhite;
  GMANPoint origin = gman::offsetOrigin(p, ng, wi, surfaceMagnitude);
  RtFloat remaining = maxDistance;

  for (int layer = 0; layer < gman::kMaxCompositeLayers; ++layer) {
    GMANRay const shadowRay(origin, wi, RI_EPSILON, remaining);
    GMANHit hit;
    GMANRayInterface const* hitPrimitive = nullptr;
    if (!bvh.nearestHit(shadowRay, hit, hitPrimitive)) {
      return v;
    }

    gman::Appearance const& appearance = hitPrimitive->getAppearance();
    GMANColor const os = clampCoverage(appearance.Os);
    gman::SurfacePoint const point = gman::hitSurfacePoint(shadowRay, hit);
    gman::BSDF const closure = gman::bsdf(appearance, point, cameraToWorld, textureCache);
    GMANColor const t = closure.shadowTransmittance(wi);

    GMANColor factor = gman::oneMinus(os);
    factor += gman::multiplyChannels(os, t);
    v = gman::multiplyChannels(v, factor);
    if (colorBlack(v)) {
      return kBlack;
    }

    RtFloat const magnitude = gman::primitiveMagnitude(hitPrimitive->getBBox());
    origin = gman::offsetOrigin(hit.point, hit.normal, wi, magnitude);
    remaining -= hit.t;
  }

  // A (kMaxCompositeLayers + 1)th blocker reads as opaque, never as a leak
  // of light through a wall deeper than the cap.
  GMANRay const capRay(origin, wi, RI_EPSILON, remaining);
  GMANHit capHit;
  GMANRayInterface const* capPrimitive = nullptr;
  if (bvh.nearestHit(capRay, capHit, capPrimitive)) {
    return kBlack;
  }
  return v;
}

// A single path's own contribution to a slot: its radiance and coverage
// estimate, whether its camera ray ever hit anything and, if so, that
// first hit's own camera-space z, and whether every channel it touched
// stayed finite.
struct PathResult {
  GMANColor L = kBlack;
  GMANColor alphaHat = kBlack;
  bool finite = true;
  bool hasFirstHit = false;
  RtFloat firstHitZ = 0.0f;
};

// Coverage's pass-through branch: the path continues straight along its own
// direction, weighted by (1 - Os)/qPass. Answers false once a straight run
// of more than kMaxCompositeLayers such vertices has ended the path.
bool passThrough(GMANHit const& hit, gman::SurfacePoint const& point, GMANColor const& os, RtFloat qPass,
                 RtFloat surfaceMagnitude, GMANColor& beta, int& passThroughRun, GMANRay& ray) {
  beta = gman::multiplyChannels(beta, divideColor(gman::oneMinus(os), qPass));
  ++passThroughRun;
  if (passThroughRun > gman::kMaxCompositeLayers) {
    return false;
  }
  GMANPoint const origin = gman::offsetOrigin(hit.point, point.Ng, ray.getDirection(), surfaceMagnitude);
  ray = GMANRay(origin, ray.getDirection());
  return true;
}

// Picks one emitter at p by its own contribution there: emitter j's weight
// is the mean of gman::sample(emitters[j], p, 0.5f, 0.5f).Cl, a fixed
// preview draw rather than the real next-event-estimation draw, taken as 0
// when it is not finite or not positive; j is drawn with probability its
// weight's share of the total. For a delta emitter this reproduces its own
// GMANLight::sample exactly, since a delta ignores (u1, u2). lightWeight is
// the caller's own per-path buffer, sized to emitters.size() and
// overwritten here, never reallocated per vertex. Answers false, chosen and
// pj untouched, when no emitter has positive weight there.
bool chooseLight(std::vector<gman::Emitter> const& emitters, GMANPoint const& p, RtInt sx, RtInt sy, std::uint32_t i,
                 std::uint32_t N, std::uint32_t k, std::vector<RtFloat>& lightWeight, std::size_t& chosen,
                 RtFloat& pj) {
  RtFloat totalWeight = 0.0f;
  for (std::size_t j = 0; j < emitters.size(); ++j) {
    gman::EmitterSample const preview = gman::sample(emitters[j], p, 0.5f, 0.5f);
    RtFloat const mean = meanChannel(preview.Cl);
    RtFloat const w = (std::isfinite(mean) && mean > 0.0f) ? mean : 0.0f;
    lightWeight[j] = w;
    totalWeight += w;
  }

  if (!(totalWeight > 0.0f) || !std::isfinite(totalWeight)) {
    return false;
  }

  RtFloat const lightU = gman::sample1D(kSeed, sx, sy, i, N, 2u + 5u * k);
  RtFloat const target = lightU * totalWeight;
  RtFloat cumulative = 0.0f;
  std::size_t lastPositive = 0;
  bool found = false;
  chosen = 0;
  for (std::size_t j = 0; j < emitters.size(); ++j) {
    if (lightWeight[j] > 0.0f) {
      lastPositive = j;
    }
    cumulative += lightWeight[j];
    if (!found && cumulative > target) {
      chosen = j;
      found = true;
    }
  }
  if (!found) {
    chosen = lastPositive;
  }

  pj = lightWeight[chosen] / totalWeight;
  return true;
}

// Next-event estimation at a scattering vertex: chooses one emitter through
// chooseLight, draws its own real (u1, u2) at dimension 3 + 5k -- reserved
// for exactly this, distinct from chooseLight's own fixed preview draw --
// and, where its solid-angle pdf is positive and the closure's response
// toward it is non-black, returns its shadowed contribution to L. Answers
// black where no emitter has positive weight there, the draw's pdf is 0 or
// the closure is black toward it. lightWeight is the caller's own per-path
// buffer, sized to emitters.size() and overwritten here, never reallocated
// per vertex.
GMANColor nextEventEstimation(GMANRayBVH const& bvh, std::vector<gman::Emitter> const& emitters, GMANHit const& hit,
                              gman::SurfacePoint const& point, gman::BSDF const& closure, GMANVector const& wo,
                              GMANColor const& beta, RtFloat surfaceMagnitude, GMANMatrix4 const& cameraToWorld,
                              gman::TextureCache* textureCache, RtInt sx, RtInt sy, std::uint32_t i, std::uint32_t N,
                              std::uint32_t k, std::vector<RtFloat>& lightWeight) {
  if (emitters.empty()) {
    return kBlack;
  }

  std::size_t chosen = 0;
  RtFloat pj = 0.0f;
  if (!chooseLight(emitters, hit.point, sx, sy, i, N, k, lightWeight, chosen, pj)) {
    return kBlack;
  }

  gman::Sample2D const uv = gman::sample2D(kSeed, sx, sy, i, N, 3u + 5u * k);
  gman::EmitterSample const es = gman::sample(emitters[chosen], hit.point, uv.u1, uv.u2);
  if (!(es.pdf > 0.0f)) {
    return kBlack;
  }

  GMANColor const f = closure.eval(wo, es.wi);
  if (colorBlack(f)) {
    return kBlack;
  }

  RtFloat const cosTerm = std::fabs(point.N.dot(es.wi));
  GMANColor const v =
      shadowWalk(bvh, hit.point, point.Ng, es.wi, surfaceMagnitude, es.distance, cameraToWorld, textureCache);

  GMANColor term = gman::multiplyChannels(beta, f);
  term = scaleColor(term, cosTerm / (pj * es.pdf));
  term = gman::multiplyChannels(term, es.Cl);
  term = gman::multiplyChannels(term, v);
  return term;
}

// Whether the BSDF step and the roulette it feeds let the path continue to
// vertex k + 1, along wi, or end it here.
enum class BSDFStepOutcome { Continue, End };

// The BSDF step: draws wi and updates beta by f*|wi.N|/pdf, tracking a
// dielectric transmission's own etaScale; then, from the fourth vertex on,
// Russian roulette. A pdf of 0 or a lost roulette draw ends the path; a beta
// gone non-finite ends it and clears finite.
BSDFStepOutcome bsdfStepAndRoulette(gman::BSDF const& closure, GMANVector const& wo, gman::SurfacePoint const& point,
                                    RtInt sx, RtInt sy, std::uint32_t i, std::uint32_t N, std::uint32_t k,
                                    GMANColor& beta, RtFloat& etaScale, bool& finite, GMANVector& wi, bool& isDelta) {
  gman::Sample2D const uvBsdf = gman::sample2D(kSeed, sx, sy, i, N, 4u + 5u * k);
  gman::BSDFSample const sample = closure.sample(wo, uvBsdf.u1, uvBsdf.u2);
  if (!(sample.pdf > 0.0f)) {
    return BSDFStepOutcome::End;
  }
  isDelta = sample.isDelta;

  RtFloat const cosI = std::fabs(point.N.dot(sample.wi));
  beta = gman::multiplyChannels(beta, scaleColor(sample.f, cosI / sample.pdf));
  if (!colorFinite(beta)) {
    finite = false;
    return BSDFStepOutcome::End;
  }

  if (sample.isDelta && closure.lobe(sample.lobeIndex).kind == gman::LobeKind::dielectric) {
    RtFloat const woDotN = wo.dot(point.N);
    RtFloat const wiDotN = sample.wi.dot(point.N);
    bool const transmitted = (woDotN > 0.0f) != (wiDotN > 0.0f);
    if (transmitted) {
      RtFloat const eta = closure.lobe(sample.lobeIndex).eta;
      if (woDotN > 0.0f) {
        etaScale *= eta * eta;
      } else {
        etaScale /= eta * eta;
      }
    }
  }

  if (k >= 3) {
    RtFloat const q = GMANMin(kRouletteCap, maxChannel(scaleColor(beta, etaScale)));
    RtFloat const rouletteU = gman::sample1D(kSeed, sx, sy, i, N, 5u + 5u * k);
    if (!(rouletteU < q)) {
      return BSDFStepOutcome::End;
    }
    beta = divideColor(beta, q);
  }

  wi = sample.wi;
  return BSDFStepOutcome::Continue;
}

// The emitter-hit term at a hit, added once per vertex before anything else
// there: black unless the hit primitive's own area light is eligible and
// supported (present in emitters, which already filtered both), its placed
// normal faces the incoming ray, and rayEligible says no non-delta BSDF
// draw produced the ray since its own last vertex -- otherwise next-event
// estimation at that earlier vertex already estimated this same
// contribution, and adding it again here would double it.
GMANColor emitterHitLe(std::vector<gman::Emitter> const& emitters, GMANRayInterface const* hitPrimitive,
                       gman::Appearance const& appearance, gman::SurfacePoint const& point, bool rayEligible) {
  if (!rayEligible || appearance.areaLight == nullptr) {
    return kBlack;
  }
  for (gman::Emitter const& emitter : emitters) {
    if (emitter.shape == hitPrimitive) {
      RtFloat const cosTheta = point.N.dot(-point.I);
      return (cosTheta > 0.0f) ? emitter.light->getCl() : kBlack;
    }
  }
  return kBlack;
}

// Traces one path from cameraRay: an eligible emitter hit's own Le,
// coverage, next-event estimation over emitters, a BSDF-sampled bounce and
// Russian roulette at each scattering vertex, until the path escapes, is
// rouletted out, fails a BSDF draw or runs past the composite-layer cap on
// a straight run of pass-throughs. (sx, sy) is the slot's absolute
// sample-grid coordinate and (i, N) the path's own index among the slot's
// N; every random draw is a pure function of these four plus a per-vertex
// dimension, so no state passes between paths or slots.
PathResult tracePath(GMANRayBVH const& bvh, std::vector<gman::Emitter> const& emitters,
                     GMANMatrix4 const& cameraToWorld, GMANColor const& background, gman::TextureCache* textureCache,
                     GMANRay cameraRay, RtInt sx, RtInt sy, std::uint32_t i, std::uint32_t N) {
  PathResult result;
  GMANColor beta = kWhite;
  RtFloat etaScale = 1.0f;
  bool hasScattered = false, escaped = false;
  // The camera ray itself is eligible: it carries no earlier BSDF draw, so
  // it has no earlier next-event estimate to double.
  bool rayEligibleForEmitterHit = true;
  int passThroughRun = 0;
  GMANRay ray = cameraRay;
  std::vector<RtFloat> lightWeight(emitters.size());
  for (std::uint32_t k = 0;; ++k) {
    GMANHit hit;
    GMANRayInterface const* hitPrimitive = nullptr;
    bool const hitFound = bvh.nearestHit(ray, hit, hitPrimitive);
    if (k == 0 && hitFound) {
      result.hasFirstHit = true;
      result.firstHitZ = hit.point.getZ();
    }
    if (!hitFound) {
      result.L += gman::multiplyChannels(beta, background);
      escaped = true;
      break;
    }
    gman::SurfacePoint const point = gman::hitSurfacePoint(ray, hit);
    gman::Appearance const& appearance = hitPrimitive->getAppearance();
    RtFloat const surfaceMagnitude = point.surfaceMagnitude;

    result.L +=
        gman::multiplyChannels(beta, emitterHitLe(emitters, hitPrimitive, appearance, point, rayEligibleForEmitterHit));

    GMANColor const os = clampCoverage(appearance.Os);
    RtFloat const qPass = meanChannel(gman::oneMinus(os));
    RtFloat const coverageU = gman::sample1D(kSeed, sx, sy, i, N, 1u + 5u * k);
    if (coverageU < qPass) {
      if (!passThrough(hit, point, os, qPass, surfaceMagnitude, beta, passThroughRun, ray)) {
        break;
      }
      continue;
    }
    // Coverage: scatter.
    passThroughRun = 0;
    hasScattered = true;
    beta = gman::multiplyChannels(beta, divideColor(os, (RtFloat)1.0 - qPass));
    gman::BSDF const closure = gman::bsdf(appearance, point, cameraToWorld, textureCache);
    GMANVector const wo = -point.I;
    result.L += nextEventEstimation(bvh, emitters, hit, point, closure, wo, beta, surfaceMagnitude, cameraToWorld,
                                    textureCache, sx, sy, i, N, k, lightWeight);
    GMANVector wi;
    bool sampleIsDelta = false;
    if (bsdfStepAndRoulette(closure, wo, point, sx, sy, i, N, k, beta, etaScale, result.finite, wi, sampleIsDelta) ==
        BSDFStepOutcome::End) {
      break;
    }
    rayEligibleForEmitterHit = sampleIsDelta;
    GMANPoint const origin = gman::offsetOrigin(hit.point, point.Ng, wi, surfaceMagnitude);
    ray = GMANRay(origin, wi);
  }
  // 1 - beta only for a path escaping before its first scattering vertex;
  // any other ending, the pass-through cap included, reads fully covered.
  result.alphaHat = (escaped && !hasScattered) ? gman::oneMinus(beta) : kWhite;
  // finite also fails here when L or alphaHat itself went non-finite, not
  // only when the BSDF step already cleared it.
  result.finite = result.finite && colorFinite(result.L) && colorFinite(result.alphaHat);
  return result;
}

// One slot's own N paths, summed for render(): the mean radiance and
// coverage estimate go to the row's call to zTestAndSet once the row knows
// whether any path's camera ray ever hit anything; a dropped path
// contributes zero to both sums and is counted.
struct SlotResult {
  GMANColor sumL = kBlack;
  GMANColor sumAlpha = kBlack;
  RtFloat minZ = RI_INFINITY;
  bool anyHit = false;
  std::size_t dropped = 0;
};

SlotResult renderSlot(GMANRayBVH const& bvh, std::vector<gman::Emitter> const& emitters, GMANViewingSystem* viewingSys,
                      GMANMatrix4 const& cameraToWorld, GMANColor const& background, gman::TextureCache* textureCache,
                      RtInt sx, RtInt sy, int xsamples, int ysamples, std::uint32_t N) {
  SlotResult result;
  for (std::uint32_t i = 0; i < N; ++i) {
    gman::Sample2D const uv0 = gman::sample2D(kSeed, sx, sy, i, N, 0u);
    RtFloat const rasterX = ((RtFloat)sx + uv0.u1) / (RtFloat)xsamples;
    RtFloat const rasterY = ((RtFloat)sy + uv0.u2) / (RtFloat)ysamples;
    GMANRay const cameraRay = viewingSys->cameraRay(rasterX, rasterY);

    PathResult const path = tracePath(bvh, emitters, cameraToWorld, background, textureCache, cameraRay, sx, sy, i, N);

    if (path.hasFirstHit && path.firstHitZ < result.minZ) {
      result.minZ = path.firstHitZ;
      result.anyHit = true;
    }

    // The sole guard keeping a non-finite path's channels out of a slot's sums.
    if (!path.finite) {
      ++result.dropped;
      continue;
    }
    result.sumL += path.L;
    result.sumAlpha += path.alphaHat;
  }
  return result;
}

// One row's own unit of work: traces every one of its slots and writes
// only its own slots of sampleBuffer and the dropped count it is handed,
// reading the BVH, the light set and the raster data it is handed as
// const, so a later caller can shard rows across gman::parallelFor workers
// unchanged.
void renderRow(GMANRayBVH const& bvh, std::vector<gman::Emitter> const& emitters, GMANViewingSystem* viewingSys,
               GMANMatrix4 const& cameraToWorld, GMANOptions::RasterInfo const& raster, RtInt width, int xsamples,
               int ysamples, std::uint32_t N, GMANColor const& background, gman::TextureCache& textureCache, int py,
               GMANSampleBuffer& sampleBuffer, std::size_t& dropped) {
  int const subsPerColumn = xsamples * ysamples;
  for (int px = 0; px < width; ++px) {
    for (int sub = 0; sub < subsPerColumn; ++sub) {
      int const subX = sub % xsamples;
      int const subY = sub / xsamples;
      int const sampleX = px * xsamples + subX;
      int const sampleY = py * ysamples + subY;
      RtInt const sx = raster.rxmin * xsamples + sampleX;
      RtInt const sy = raster.rymin * ysamples + sampleY;

      SlotResult const slot = renderSlot(bvh, emitters, viewingSys, cameraToWorld, background, &textureCache, sx, sy,
                                         xsamples, ysamples, N);
      dropped += slot.dropped;
      if (!slot.anyHit) {
        continue;
      }
      GMANColor const meanL = divideColor(slot.sumL, (RtFloat)N);
      GMANColor const meanAlpha = divideColor(slot.sumAlpha, (RtFloat)N);
      GMANAlpha const alpha(meanAlpha.getRed(), meanAlpha.getGreen(), meanAlpha.getBlue());
      sampleBuffer.zTestAndSet(sampleX, sampleY, slot.minZ, meanL, alpha);
    }
  }
}

} // namespace

void GMANPathtraceRenderer::gatherLights() {
  emitters = gman::emitters(worldManager);

  // gman::emitters already excludes ambientlight from its own walk, with
  // no count of its own; a second, separate walk recovers exactly the
  // count this renderer's own warning names.
  std::vector<GMANLight const*> ambientSeen;
  for (GMANPrimitive* primitive = worldManager.getFirst(); primitive != nullptr; primitive = worldManager.getNext()) {
    GMANRayInterface const* rayPrimitive = dynamic_cast<GMANRayInterface const*>(primitive);
    if (rayPrimitive == nullptr) {
      continue;
    }
    for (GMANLight const* light : rayPrimitive->getAppearance().lights) {
      if (light->getType() == GMAN_LIGHT_AMBIENT &&
          std::find(ambientSeen.begin(), ambientSeen.end(), light) == ambientSeen.end()) {
        ambientSeen.push_back(light);
      }
    }
  }
  skippedAmbientLights = ambientSeen.size();
}

GMANPathtraceRenderer::GMANPathtraceRenderer() : GMANRenderer() {};

GMANPathtraceRenderer::~GMANPathtraceRenderer() {};

void GMANPathtraceRenderer::render(GMANFrameBuffer* frameBuffer, GMANViewingSystem* viewingSys,
                                   const GMANOptions& options, const GMANAttributes& /*attributes*/) {
  bvh.build(worldManager);
  gatherLights();
  droppedPaths = 0;

  RtInt const width = frameBuffer->getWidth();
  RtInt const height = frameBuffer->getHeight();

  GMANOptions::RasterInfo const raster = options.getRasterInfo();
  GMANOptions::PixelSamplesStruct const& ps = options.getPixelSamples();
  int const xsamples = GMANMax(1, (int)GMANRound(ps.xsamples));
  int const ysamples = GMANMax(1, (int)GMANRound(ps.ysamples));
  std::uint32_t const N = (std::uint32_t)GMANMax(1, options.getPathtracerSamples());

  GMANColor const background = options.getBackground();
  sampleBuffer.reset(new GMANSampleBuffer(width, height, xsamples, ysamples, background));

  GMANMatrix4 const& cameraToWorld = options.getCameraToWorld();
  gman::TextureCache textureCache;

  for (int py = 0; py < height; ++py) {
    std::size_t rowDropped = 0;
    renderRow(bvh, emitters, viewingSys, cameraToWorld, raster, width, xsamples, ysamples, N, background, textureCache,
              py, *sampleBuffer, rowDropped);
    droppedPaths += rowDropped;
  }

  if (droppedPaths != 0) {
    warning("gmanpathtracer: dropped {} path(s) for a non-finite channel.", droppedPaths);
  }
  if (skippedAmbientLights != 0) {
    warning("gmanpathtracer: ambientlight lights nothing under the path tracer; skipped {} light(s).",
            skippedAmbientLights);
  }
  if (!options.getIndirectPass().empty()) {
    warning("gmanpathtracer: the path tracer computes indirect light itself; indirect pass \"{}\" not loaded.",
            options.getIndirectPass());
  }

  GMANOptions::PixelFilterStruct const& pf = options.getPixelFilter();
  sampleBuffer->resolve(frameBuffer, pf.filterfunc, pf.xwidth, pf.ywidth);
}

GMANWorldManager* GMANPathtraceRenderer::getWorldManager(void) { return &worldManager; }

GMANObjectManager* GMANPathtraceRenderer::getObjectManager(void) { return &objectManager; }
