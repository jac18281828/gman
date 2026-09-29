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

#include <cmath>
#include <cstdint>
#include <optional>

#include "gmanattributes.h"
#include "gmanbsdf.h"
#include "gmanemitter.h"
#include "gmanlog.h"
#include "gmanmath.h"
#include "gmanpathtracerenderer.h"
#include "gmanpathtracerweights.h"
#include "gmanraybbox.h"
#include "gmanrayinterface.h"
#include "gmanrayoccluder.h"
#include "gmansampling.h"
#include "ri.h"

namespace gman {

RtFloat lightChoiceWeights(std::vector<gman::Emitter> const& emitters, GMANPoint const& p,
                           GMANRayInterface const* primitive, std::vector<RtFloat>& lightWeight) {
  RtFloat totalWeight = 0.0f;
  for (std::size_t j = 0; j < emitters.size(); ++j) {
    RtFloat const w = (emitters[j].shape == primitive) ? 0.0f : gman::lightChoiceWeight(emitters[j], p);
    lightWeight[j] = w;
    totalWeight += w;
  }
  return totalWeight;
}

RtFloat lightChoiceProbability(std::vector<gman::Emitter> const& emitters, GMANPoint const& p,
                               GMANRayInterface const* primitive, std::size_t index,
                               std::vector<RtFloat>& lightWeight) {
  RtFloat const totalWeight = lightChoiceWeights(emitters, p, primitive, lightWeight);
  return (totalWeight > 0.0f) ? lightWeight[index] / totalWeight : 0.0f;
}

RtFloat nextEventWeight(bool lightIsDelta, bool misEnabled, RtFloat pLight, RtFloat pBsdf) {
  if (lightIsDelta || !misEnabled) {
    return 1.0f;
  }
  return gman::powerHeuristic(pLight, pBsdf);
}

RtFloat emitterHitWeight(bool rayEligibleForEmitterHit, bool misEnabled, RtFloat pBsdf, RtFloat pLight) {
  if (rayEligibleForEmitterHit) {
    return 1.0f;
  }
  if (!misEnabled) {
    return 0.0f;
  }
  return gman::powerHeuristic(pBsdf, pLight);
}

EmitterHitEligibility nextEmitterHitEligibility(EmitterHitEligibility current, bool isDelta, bool isTransmission) {
  if (!isDelta) {
    return EmitterHitEligibility::weighted;
  }
  if (!isTransmission) {
    return EmitterHitEligibility::eligible;
  }
  return current == EmitterHitEligibility::eligible ? EmitterHitEligibility::eligible
                                                    : EmitterHitEligibility::suppressed;
}

} // namespace gman

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

GMANColor subtractColor(GMANColor const& a, GMANColor const& b) {
  return GMANColor(a.getRed() - b.getRed(), a.getGreen() - b.getGreen(), a.getBlue() - b.getBlue());
}

bool colorsBitwiseEqual(GMANColor const& a, GMANColor const& b) {
  return a.getRed() == b.getRed() && a.getGreen() == b.getGreen() && a.getBlue() == b.getBlue();
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

// The shadow walk's own transmittance, split into v, the full product of
// each blocker's (1 - Os) + Os * shadowTransmittance, and p, the product
// of each blocker's own (1 - Os) alone -- the pure coverage pass-through
// share a BSDF-sampled bounce can also reach through coverage's own
// passThrough. v - p is every blocker's own dielectric shadowTransmittance
// share, which next-event estimation's own weight (below) credits at
// weight 1, since a suppressed emitter hit never adds it. p <= v always,
// since each blocker's own factor is at least its (1 - Os) term.
struct ShadowWalkResult {
  GMANColor v = kWhite;
  GMANColor p = kWhite;
};

// The plugin's own shadow walk, in gmanrayoccluder.h's transmission's own
// shape: advances from origin over [0, maxDistance) along wi, compositing
// each blocker's own factor per channel with no early exit, and answers
// black past gman::kMaxCompositeLayers blockers. A delta draw keeps its
// own fixed wi, shortening remaining by each blocker's own hit.t; an area
// draw re-aims at shadowTarget after every blocker, since a blocker's own
// offset scales with its own size and could otherwise push the walk's
// endpoint past a small emitter's own target -- ending unblocked once
// shadowTarget lies at or behind the walk's own new origin.
ShadowWalkResult shadowWalk(GMANRayBVH const& bvh, GMANPoint origin, GMANVector wi, RtFloat maxDistance, bool isDelta,
                            GMANPoint const& shadowTarget, GMANMatrix4 const& cameraToWorld,
                            gman::TextureCache* textureCache) {
  ShadowWalkResult result;
  RtFloat remaining = maxDistance;

  for (int layer = 0; layer < gman::kMaxCompositeLayers; ++layer) {
    GMANRay const shadowRay(origin, wi, RI_EPSILON, remaining);
    GMANHit hit;
    GMANRayInterface const* hitPrimitive = nullptr;
    if (!bvh.nearestHit(shadowRay, hit, hitPrimitive)) {
      return result;
    }

    gman::Appearance const& appearance = hitPrimitive->getAppearance();
    GMANColor const os = clampCoverage(appearance.Os);
    gman::SurfacePoint const point = gman::hitSurfacePoint(shadowRay, hit);
    gman::BSDF const closure = gman::bsdf(appearance, point, cameraToWorld, textureCache);
    GMANColor const t = closure.shadowTransmittance(wi);

    GMANColor const passThroughShare = gman::oneMinus(os);
    GMANColor factor = passThroughShare;
    factor += gman::multiplyChannels(os, t);
    result.v = gman::multiplyChannels(result.v, factor);
    result.p = gman::multiplyChannels(result.p, passThroughShare);
    if (colorBlack(result.v)) {
      return result;
    }

    RtFloat const magnitude = gman::primitiveMagnitude(hitPrimitive->getBBox());
    GMANPoint const newOrigin = gman::offsetOrigin(hit.point, hit.normal, wi, magnitude);

    if (isDelta) {
      origin = newOrigin;
      remaining -= hit.t;
      continue;
    }

    GMANVector toTarget(newOrigin, shadowTarget);
    if (toTarget.dot(wi) <= 0.0f) {
      return result;
    }
    remaining = toTarget.magnitude();
    toTarget.normalize();
    origin = newOrigin;
    wi = toTarget;
  }

  // A (kMaxCompositeLayers + 1)th blocker reads as opaque, never as a leak
  // of light through a wall deeper than the cap.
  GMANRay const capRay(origin, wi, RI_EPSILON, remaining);
  GMANHit capHit;
  GMANRayInterface const* capPrimitive = nullptr;
  if (bvh.nearestHit(capRay, capHit, capPrimitive)) {
    return {kBlack, kBlack};
  }
  return result;
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

// Picks one emitter at p by its own contribution there, lightChoiceWeights'
// own weight for each: a delta emitter's fixed preview draw, or an area
// emitter's power over its own placed distance from p, floored at its own
// bounding radius so it is never zero where it actually contributes; an
// emitter whose shape is primitive is never chosen, since p sits on its own
// surface. j is drawn with probability its weight's share of the total.
// lightWeight is the caller's own per-path buffer, sized to emitters.size()
// and overwritten here, never reallocated per vertex. Answers false, chosen
// and pj untouched, when no emitter has positive weight there.
bool chooseLight(std::vector<gman::Emitter> const& emitters, GMANPoint const& p, GMANRayInterface const* primitive,
                 RtInt sx, RtInt sy, std::uint32_t i, std::uint32_t N, std::uint32_t k,
                 std::vector<RtFloat>& lightWeight, std::size_t& chosen, RtFloat& pj) {
  RtFloat const totalWeight = lightChoiceWeights(emitters, p, primitive, lightWeight);
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

  // lightChoiceWeights already computed this vertex's own weights once,
  // above; an emitter-hit's own weight recomputes this same share through
  // that one function again, at the arrival vertex, so the two sides
  // provably agree.
  pj = lightWeight[chosen] / totalWeight;
  return true;
}

// The shadow walk's own initial direction and distance from origin: a
// delta draw's own fixed wi and true distance, unchanged; an area draw's
// own unit direction to its shadowTarget and their distance, since
// origin's own small offset off the shading point moves the true walk's
// endpoint by a comparable amount.
struct ShadowWalkStart {
  GMANVector wi;
  RtFloat distance;
};

ShadowWalkStart shadowWalkStart(GMANPoint const& origin, gman::EmitterSample const& es) {
  if (es.isDelta) {
    return {es.wi, es.distance};
  }
  GMANVector toTarget(origin, es.shadowTarget);
  RtFloat const distance = toTarget.magnitude();
  toTarget.normalize();
  return {toTarget, distance};
}

// One light sample's shadowed contribution: betaF (throughput times the
// closure's response) scaled by cosTerm / pLight, the light's Cl and the
// walk's transmittance, weighted. The walk's pass-through share p (what
// coverage's own passThrough can also reach) takes weight; the remainder
// v - p, every crossing of a dielectric's shadowTransmittance, takes weight
// 1, since an emitter hit through a delta-transmission chain adds nothing.
// A weight of 1, or a walk with no dielectric crossing (v equal to p
// bitwise), weights the whole transmittance v at once.
GMANColor weightedShadowedTerm(GMANColor const& betaF, RtFloat cosTerm, RtFloat pLight, RtFloat weight,
                               GMANColor const& cl, ShadowWalkResult const& walk) {
  if (weight == 1.0f || colorsBitwiseEqual(walk.v, walk.p)) {
    GMANColor term = scaleColor(betaF, weight * cosTerm / pLight);
    term = gman::multiplyChannels(term, cl);
    return gman::multiplyChannels(term, walk.v);
  }

  GMANColor const term = gman::multiplyChannels(scaleColor(betaF, cosTerm / pLight), cl);
  GMANColor combined = subtractColor(walk.v, walk.p);
  combined += scaleColor(walk.p, weight);
  return gman::multiplyChannels(term, combined);
}

// Next-event estimation at a scattering vertex: chooses one emitter through
// chooseLight, draws its own real (u1, u2) at dimension 3 + 5k -- reserved
// for exactly this, distinct from chooseLight's own fixed preview draw --
// and, where its solid-angle pdf is positive and the closure's response
// toward it is non-black, returns its shadowed contribution to L, weighted
// against the closure's own density toward the same direction whenever
// misEnabled and the chosen light is an area emitter (nextEventWeight): a
// delta light keeps weight 1, since nothing can ever land on it by chance.
// Answers black where no emitter has positive weight there, the draw's pdf
// is 0 or the closure is black toward it. lightWeight is the caller's own
// per-path buffer, sized to emitters.size() and overwritten here, never
// reallocated per vertex. hitPrimitive is the shading point's own
// primitive, excluded from the choice: it cannot light a point on itself.
GMANColor nextEventEstimation(GMANRayBVH const& bvh, std::vector<gman::Emitter> const& emitters, GMANHit const& hit,
                              GMANRayInterface const* hitPrimitive, gman::SurfacePoint const& point,
                              gman::BSDF const& closure, GMANVector const& wo, GMANColor const& beta,
                              RtFloat surfaceMagnitude, GMANMatrix4 const& cameraToWorld,
                              gman::TextureCache* textureCache, RtInt sx, RtInt sy, std::uint32_t i, std::uint32_t N,
                              std::uint32_t k, std::vector<RtFloat>& lightWeight, bool misEnabled) {
  if (emitters.empty()) {
    return kBlack;
  }

  std::size_t chosen = 0;
  RtFloat pj = 0.0f;
  if (!chooseLight(emitters, hit.point, hitPrimitive, sx, sy, i, N, k, lightWeight, chosen, pj)) {
    return kBlack;
  }

  gman::Sample2D const uv = gman::sample2D(kSeed, sx, sy, i, N, 3u + 5u * k);
  gman::EmitterSample const es = gman::sample(emitters[chosen], hit.point, uv.u1, uv.u2);
  if (!(es.pdf > 0.0f) || colorBlack(es.Cl)) {
    return kBlack;
  }

  GMANColor const f = closure.eval(wo, es.wi);
  if (colorBlack(f)) {
    return kBlack;
  }

  RtFloat const pLight = pj * es.pdf;
  RtFloat const pBsdf = closure.pdf(wo, es.wi);
  RtFloat const weight = gman::nextEventWeight(es.isDelta, misEnabled, pLight, pBsdf);

  RtFloat const cosTerm = std::fabs(point.N.dot(es.wi));
  GMANPoint const origin = gman::offsetOrigin(hit.point, point.Ng, es.wi, surfaceMagnitude);
  ShadowWalkStart const walkStart = shadowWalkStart(origin, es);
  ShadowWalkResult const walk = shadowWalk(bvh, origin, walkStart.wi, walkStart.distance, es.isDelta, es.shadowTarget,
                                           cameraToWorld, textureCache);

  return weightedShadowedTerm(gman::multiplyChannels(beta, f), cosTerm, pLight, weight, es.Cl, walk);
}

// The BSDF draw that carries a path onward: its direction, its own
// solid-angle pdf (an emitter-hit's own MIS weight reads this, for a
// non-delta departure), whether the lobe sampled was a delta one, and
// whether wi fell on the opposite side of the geometric normal from wo (a
// delta transmission; meaningless unless isDelta) -- the emitter-hit
// eligibility chain's own input.
struct BSDFStepDraw {
  GMANVector wi;
  RtFloat pdf;
  bool isDelta;
  bool isTransmission;
};

// The BSDF step: draws a direction and updates beta by f*|wi.N|/pdf,
// tracking a dielectric transmission's own etaScale; then, from the fourth
// vertex on, Russian roulette. Answers nullopt, ending the path, on a pdf
// of 0 or a lost roulette draw; a beta gone non-finite likewise ends it and
// clears finite.
std::optional<BSDFStepDraw> bsdfStepAndRoulette(gman::BSDF const& closure, GMANVector const& wo,
                                                gman::SurfacePoint const& point, RtInt sx, RtInt sy, std::uint32_t i,
                                                std::uint32_t N, std::uint32_t k, GMANColor& beta, RtFloat& etaScale,
                                                bool& finite) {
  gman::Sample2D const uvBsdf = gman::sample2D(kSeed, sx, sy, i, N, 4u + 5u * k);
  gman::BSDFSample const sample = closure.sample(wo, uvBsdf.u1, uvBsdf.u2);
  if (!(sample.pdf > 0.0f)) {
    return std::nullopt;
  }

  RtFloat const cosI = std::fabs(point.N.dot(sample.wi));
  beta = gman::multiplyChannels(beta, scaleColor(sample.f, cosI / sample.pdf));
  if (!colorFinite(beta)) {
    finite = false;
    return std::nullopt;
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
      return std::nullopt;
    }
    beta = divideColor(beta, q);
  }

  // The geometric normal, not the shading one: the emitter-hit chain rule
  // asks whether a crossing left the surface's own geometric side, not
  // whether a bump-perturbed shading normal did.
  bool const isTransmission = (point.Ng.dot(wo) > 0.0f) != (point.Ng.dot(sample.wi) > 0.0f);
  return BSDFStepDraw{sample.wi, sample.pdf, sample.isDelta, isTransmission};
}

// The emitter-hit term at a hit, added once per vertex before anything else
// there: black unless the hit primitive's own area light is eligible and
// supported (present in emitters, which already filtered both) and its
// placed normal faces the incoming ray, and eligibility is not suppressed
// (a straight delta-transmission chain back to a non-delta departure,
// whose own shadow ray already carried this light through those
// surfaces). eligible keeps weight 1: the one case with no competing
// technique, or the one where next-event estimation's own term at the
// departing vertex is already black, so nothing here can double it.
// Otherwise (weighted) the departure was a non-delta draw, from departureP
// on departurePrimitive with its own reported density departurePdf; the
// term is weighted by emitterHitWeight against the light-choice/solid-
// angle density next-event estimation would have used for this same
// emitter from that same departure, 0 outright when misEnabled is false.
// lightWeight is the caller's own per-path buffer, sized to emitters.size()
// and free at this point, since next-event estimation at the same vertex
// runs after it.
GMANColor emitterHitLe(std::vector<gman::Emitter> const& emitters, GMANRayInterface const* hitPrimitive,
                       gman::Appearance const& appearance, gman::SurfacePoint const& point,
                       gman::EmitterHitEligibility eligibility, bool misEnabled, GMANPoint const& departureP,
                       GMANRayInterface const* departurePrimitive, RtFloat departurePdf,
                       std::vector<RtFloat>& lightWeight) {
  if (appearance.areaLight == nullptr || eligibility == gman::EmitterHitEligibility::suppressed) {
    return kBlack;
  }
  for (std::size_t j = 0; j < emitters.size(); ++j) {
    if (emitters[j].shape != hitPrimitive) {
      continue;
    }
    RtFloat const cosTheta = point.N.dot(-point.I);
    if (!(cosTheta > 0.0f)) {
      return kBlack;
    }
    bool const rayEligible = eligibility == gman::EmitterHitEligibility::eligible;
    RtFloat pLight = 0.0f;
    if (misEnabled && !rayEligible) {
      pLight = gman::lightChoiceProbability(emitters, departureP, departurePrimitive, j, lightWeight) *
               gman::lightSolidAnglePdf(emitters[j], departureP, point.P, point.N);
    }
    RtFloat const weight = gman::emitterHitWeight(rayEligible, misEnabled, departurePdf, pLight);
    return scaleColor(emitters[j].light->getCl(), weight);
  }
  return kBlack;
}

// One path's own mutable state as it walks its vertices: its accumulated
// result, its throughput and eta-scale, whether it has scattered yet, the
// current ray, its run of straight pass-through vertices, the current
// emitter-hit eligibility, the departing vertex's own shading point,
// primitive and BSDF-sample density behind a non-delta departure (read
// only when eligibility is weighted), and the chooseLight scratch buffer,
// sized once to emitters.size() and reused at every vertex.
struct PathState {
  PathResult result;
  GMANColor beta = kWhite;
  RtFloat etaScale = 1.0f;
  bool hasScattered = false;
  // The camera ray itself is eligible: it carries no earlier BSDF draw, so
  // it has no earlier next-event estimate to double.
  gman::EmitterHitEligibility eligibility = gman::EmitterHitEligibility::eligible;
  GMANPoint departureP;
  GMANRayInterface const* departurePrimitive = nullptr;
  RtFloat departurePdf = 0.0f;
  int passThroughRun = 0;
  GMANRay ray;
  std::vector<RtFloat> lightWeight;

  PathState(GMANRay const& cameraRay, std::size_t emitterCount) : ray(cameraRay), lightWeight(emitterCount) {}
};

// One vertex's own work, once a hit is known: an eligible emitter hit's own
// Le, then coverage's pass-through or scattering with next-event
// estimation and a BSDF-sampled bounce. Updates state in place; answers
// false to end the path here.
bool tracePathVertex(GMANRayBVH const& bvh, std::vector<gman::Emitter> const& emitters,
                     GMANMatrix4 const& cameraToWorld, gman::TextureCache* textureCache, GMANHit const& hit,
                     GMANRayInterface const* hitPrimitive, RtInt sx, RtInt sy, std::uint32_t i, std::uint32_t N,
                     std::uint32_t k, bool misEnabled, PathState& state) {
  gman::SurfacePoint const point = gman::hitSurfacePoint(state.ray, hit);
  gman::Appearance const& appearance = hitPrimitive->getAppearance();
  RtFloat const surfaceMagnitude = point.surfaceMagnitude;

  state.result.L += gman::multiplyChannels(
      state.beta, emitterHitLe(emitters, hitPrimitive, appearance, point, state.eligibility, misEnabled,
                               state.departureP, state.departurePrimitive, state.departurePdf, state.lightWeight));

  GMANColor const os = clampCoverage(appearance.Os);
  RtFloat const qPass = meanChannel(gman::oneMinus(os));
  RtFloat const coverageU = gman::sample1D(kSeed, sx, sy, i, N, 1u + 5u * k);
  if (coverageU < qPass) {
    return passThrough(hit, point, os, qPass, surfaceMagnitude, state.beta, state.passThroughRun, state.ray);
  }

  // Coverage: scatter.
  state.passThroughRun = 0;
  state.hasScattered = true;
  state.beta = gman::multiplyChannels(state.beta, divideColor(os, (RtFloat)1.0 - qPass));
  gman::BSDF const closure = gman::bsdf(appearance, point, cameraToWorld, textureCache);
  GMANVector const wo = -point.I;
  state.result.L +=
      nextEventEstimation(bvh, emitters, hit, hitPrimitive, point, closure, wo, state.beta, surfaceMagnitude,
                          cameraToWorld, textureCache, sx, sy, i, N, k, state.lightWeight, misEnabled);
  std::optional<BSDFStepDraw> const draw =
      bsdfStepAndRoulette(closure, wo, point, sx, sy, i, N, k, state.beta, state.etaScale, state.result.finite);
  if (!draw) {
    return false;
  }
  state.eligibility = gman::nextEmitterHitEligibility(state.eligibility, draw->isDelta, draw->isTransmission);
  if (!draw->isDelta) {
    state.departureP = hit.point;
    state.departurePrimitive = hitPrimitive;
    state.departurePdf = draw->pdf;
  }
  GMANPoint const origin = gman::offsetOrigin(hit.point, point.Ng, draw->wi, surfaceMagnitude);
  state.ray = GMANRay(origin, draw->wi);
  return true;
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
                     GMANRay cameraRay, RtInt sx, RtInt sy, std::uint32_t i, std::uint32_t N, bool misEnabled) {
  PathState state(cameraRay, emitters.size());
  bool escaped = false;
  for (std::uint32_t k = 0;; ++k) {
    GMANHit hit;
    GMANRayInterface const* hitPrimitive = nullptr;
    bool const hitFound = bvh.nearestHit(state.ray, hit, hitPrimitive);
    if (k == 0 && hitFound) {
      state.result.hasFirstHit = true;
      state.result.firstHitZ = hit.point.getZ();
    }
    if (!hitFound) {
      state.result.L += gman::multiplyChannels(state.beta, background);
      escaped = true;
      break;
    }
    if (!tracePathVertex(bvh, emitters, cameraToWorld, textureCache, hit, hitPrimitive, sx, sy, i, N, k, misEnabled,
                         state)) {
      break;
    }
  }
  // 1 - beta only for a path escaping before its first scattering vertex;
  // any other ending, the pass-through cap included, reads fully covered.
  state.result.alphaHat = (escaped && !state.hasScattered) ? gman::oneMinus(state.beta) : kWhite;
  // finite also fails here when L or alphaHat itself went non-finite, not
  // only when the BSDF step already cleared it.
  state.result.finite = state.result.finite && colorFinite(state.result.L) && colorFinite(state.result.alphaHat);
  return state.result;
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
                      RtInt sx, RtInt sy, int xsamples, int ysamples, std::uint32_t N, bool misEnabled) {
  SlotResult result;
  for (std::uint32_t i = 0; i < N; ++i) {
    gman::Sample2D const uv0 = gman::sample2D(kSeed, sx, sy, i, N, 0u);
    RtFloat const rasterX = ((RtFloat)sx + uv0.u1) / (RtFloat)xsamples;
    RtFloat const rasterY = ((RtFloat)sy + uv0.u2) / (RtFloat)ysamples;
    GMANRay const cameraRay = viewingSys->cameraRay(rasterX, rasterY);

    PathResult const path =
        tracePath(bvh, emitters, cameraToWorld, background, textureCache, cameraRay, sx, sy, i, N, misEnabled);

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
               GMANSampleBuffer& sampleBuffer, std::size_t& dropped, bool misEnabled) {
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
                                         xsamples, ysamples, N, misEnabled);
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
  std::size_t ambientCount = 0;
  emitters = gman::emitters(worldManager, &ambientCount);
  skippedAmbientLights = ambientCount;
}

GMANPathtraceRenderer::GMANPathtraceRenderer() : GMANRenderer() {};

GMANPathtraceRenderer::~GMANPathtraceRenderer() {};

void GMANPathtraceRenderer::setMultipleImportanceSampling(bool enabled) { multipleImportanceSamplingEnabled = enabled; }

void GMANPathtraceRenderer::render(GMANFrameBuffer* frameBuffer, GMANViewingSystem* viewingSys,
                                   GMANOptions const& options, GMANAttributes const& /*attributes*/) {
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
              py, *sampleBuffer, rowDropped, multipleImportanceSamplingEnabled);
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
