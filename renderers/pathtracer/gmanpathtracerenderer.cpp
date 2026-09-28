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

// pi, once, for next-event estimation's own scale.
constexpr RtFloat kPi = (RtFloat)3.14159265358979323846;

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

// The shadow walk (decision 10 in spirit, gmanrayoccluder.h's transmission
// in shape): advances toward the light over [origin, origin + maxDistance)
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

// Traces one path from cameraRay: coverage, next-event estimation over
// lights, a BSDF-sampled bounce and Russian roulette at each scattering
// vertex, until the path escapes, is rouletted out, fails a BSDF draw or
// runs past the composite-layer cap on a straight run of pass-throughs.
// (sx, sy) is the slot's absolute sample-grid coordinate and (i, N) the
// path's own index among the slot's N; every random draw is a pure
// function of these four plus a per-vertex dimension, so no state passes
// between paths or slots.
PathResult tracePath(GMANRayBVH const& bvh, std::vector<GMANLight const*> const& lights,
                     GMANMatrix4 const& cameraToWorld, GMANColor const& background, gman::TextureCache* textureCache,
                     GMANRay cameraRay, RtInt sx, RtInt sy, std::uint32_t i, std::uint32_t N) {
  PathResult result;
  GMANColor beta = kWhite;
  RtFloat etaScale = 1.0f;
  bool hasScattered = false;
  int passThroughRun = 0;
  GMANRay ray = cameraRay;

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
      break;
    }

    gman::SurfacePoint const point = gman::hitSurfacePoint(ray, hit);
    gman::Appearance const& appearance = hitPrimitive->getAppearance();
    RtFloat const surfaceMagnitude = point.surfaceMagnitude;

    GMANColor const os = clampCoverage(appearance.Os);
    RtFloat const qPass = meanChannel(gman::oneMinus(os));
    RtFloat const coverageU = gman::sample1D(kSeed, sx, sy, i, N, 1u + 5u * k);

    if (coverageU < qPass) {
      // Coverage: pass straight through, on to vertex k + 1.
      beta = gman::multiplyChannels(beta, divideColor(gman::oneMinus(os), qPass));
      ++passThroughRun;
      if (passThroughRun > gman::kMaxCompositeLayers) {
        break;
      }
      GMANPoint const origin = gman::offsetOrigin(hit.point, point.Ng, ray.getDirection(), surfaceMagnitude);
      ray = GMANRay(origin, ray.getDirection());
      continue;
    }

    // Coverage: scatter.
    passThroughRun = 0;
    hasScattered = true;
    beta = gman::multiplyChannels(beta, divideColor(os, (RtFloat)1.0 - qPass));

    gman::BSDF const closure = gman::bsdf(appearance, point, cameraToWorld, textureCache);
    GMANVector const wo = -point.I;

    // Next-event estimation, over the light chosen by its own
    // contribution at this hit.
    if (!lights.empty()) {
      std::vector<GMANVector> lightVectors(lights.size());
      std::vector<RtFloat> lightWeight(lights.size());
      std::vector<GMANColor> lightCl(lights.size());
      RtFloat totalWeight = 0.0f;
      for (std::size_t j = 0; j < lights.size(); ++j) {
        GMANVector l;
        GMANColor cl;
        lights[j]->sample(hit.point, l, cl);
        lightVectors[j] = l;
        lightCl[j] = cl;
        RtFloat const mean = meanChannel(cl);
        RtFloat const w = (std::isfinite(mean) && mean > 0.0f) ? mean : 0.0f;
        lightWeight[j] = w;
        totalWeight += w;
      }

      if (totalWeight > 0.0f && std::isfinite(totalWeight)) {
        RtFloat const lightU = gman::sample1D(kSeed, sx, sy, i, N, 2u + 5u * k);
        RtFloat const target = lightU * totalWeight;
        RtFloat cumulative = 0.0f;
        std::size_t chosen = 0;
        std::size_t lastPositive = 0;
        bool found = false;
        for (std::size_t j = 0; j < lights.size(); ++j) {
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

        RtFloat const pj = lightWeight[chosen] / totalWeight;
        GMANVector wi = lightVectors[chosen];
        wi.normalize();
        GMANColor const f = closure.eval(wo, wi);
        if (!colorBlack(f)) {
          RtFloat const cosTerm = std::fabs(point.N.dot(wi));
          RtFloat const distance =
              (lights[chosen]->getType() == GMAN_LIGHT_DISTANT) ? RI_INFINITY : lightVectors[chosen].magnitude();
          GMANColor const v =
              shadowWalk(bvh, hit.point, point.Ng, wi, surfaceMagnitude, distance, cameraToWorld, textureCache);

          GMANColor term = gman::multiplyChannels(beta, f);
          term = scaleColor(term, cosTerm * kPi / pj);
          term = gman::multiplyChannels(term, lightCl[chosen]);
          term = gman::multiplyChannels(term, v);
          result.L += term;
        }
      }
    }

    // The BSDF step.
    gman::Sample2D const uvBsdf = gman::sample2D(kSeed, sx, sy, i, N, 4u + 5u * k);
    gman::BSDFSample const sample = closure.sample(wo, uvBsdf.u1, uvBsdf.u2);
    if (!(sample.pdf > 0.0f)) {
      break;
    }

    RtFloat const cosI = std::fabs(point.N.dot(sample.wi));
    beta = gman::multiplyChannels(beta, scaleColor(sample.f, cosI / sample.pdf));
    if (!colorFinite(beta)) {
      result.finite = false;
      break;
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
        break;
      }
      beta = divideColor(beta, q);
    }

    GMANPoint const origin = gman::offsetOrigin(hit.point, point.Ng, sample.wi, surfaceMagnitude);
    ray = GMANRay(origin, sample.wi);
  }

  result.alphaHat = hasScattered ? kWhite : gman::oneMinus(beta);

  if (!result.finite || !colorFinite(result.L) || !colorFinite(result.alphaHat)) {
    result.finite = false;
    result.L = kBlack;
    result.alphaHat = kBlack;
  }
  return result;
}

} // namespace

void GMANPathtraceRenderer::gatherLights() {
  lights.clear();
  std::vector<GMANLight const*> ambientSeen;

  for (GMANPrimitive* primitive = worldManager.getFirst(); primitive != nullptr; primitive = worldManager.getNext()) {
    GMANRayInterface const* rayPrimitive = dynamic_cast<GMANRayInterface const*>(primitive);
    if (rayPrimitive == nullptr) {
      continue;
    }
    for (GMANLight const* light : rayPrimitive->getAppearance().lights) {
      if (light->getType() == GMAN_LIGHT_AMBIENT) {
        if (std::find(ambientSeen.begin(), ambientSeen.end(), light) == ambientSeen.end()) {
          ambientSeen.push_back(light);
        }
        continue;
      }
      if (std::find(lights.begin(), lights.end(), light) == lights.end()) {
        lights.push_back(light);
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
    for (int px = 0; px < width; ++px) {
      for (int subY = 0; subY < ysamples; ++subY) {
        for (int subX = 0; subX < xsamples; ++subX) {
          int const sampleX = px * xsamples + subX;
          int const sampleY = py * ysamples + subY;
          RtInt const sx = raster.rxmin * xsamples + sampleX;
          RtInt const sy = raster.rymin * ysamples + sampleY;

          GMANColor sumL = kBlack;
          GMANColor sumAlpha = kBlack;
          RtFloat minZ = RI_INFINITY;
          bool anyHit = false;

          for (std::uint32_t i = 0; i < N; ++i) {
            gman::Sample2D const uv0 = gman::sample2D(kSeed, sx, sy, i, N, 0u);
            RtFloat const rasterX = ((RtFloat)sx + uv0.u1) / (RtFloat)xsamples;
            RtFloat const rasterY = ((RtFloat)sy + uv0.u2) / (RtFloat)ysamples;
            GMANRay const cameraRay = viewingSys->cameraRay(rasterX, rasterY);

            PathResult const path =
                tracePath(bvh, lights, cameraToWorld, background, &textureCache, cameraRay, sx, sy, i, N);

            if (path.hasFirstHit && path.firstHitZ < minZ) {
              minZ = path.firstHitZ;
              anyHit = true;
            }

            if (!path.finite) {
              ++droppedPaths;
              continue;
            }
            sumL += path.L;
            sumAlpha += path.alphaHat;
          }

          if (!anyHit) {
            continue;
          }
          GMANColor const meanL = divideColor(sumL, (RtFloat)N);
          GMANColor const meanAlpha = divideColor(sumAlpha, (RtFloat)N);
          GMANAlpha const alpha(meanAlpha.getRed(), meanAlpha.getGreen(), meanAlpha.getBlue());
          sampleBuffer->zTestAndSet(sampleX, sampleY, minZ, meanL, alpha);
        }
      }
    }
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
