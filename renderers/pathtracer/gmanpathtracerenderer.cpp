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
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <memory>
#include <numeric>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "gmanattributes.h"
#include "gmanbsdf.h"
#include "gmancolor.h"
#include "gmandenoise.h"
#include "gmanemitter.h"
#include "gmanframebuffer.h"
#include "gmanlog.h"
#include "gmanmath.h"
#include "gmanmatrix4.h"
#include "gmannormal.h"
#include "gmanoptions.h"
#include "gmanparallel.h"
#include "gmanpathtracerenderer.h"
#include "gmanpathtracerweights.h"
#include "gmanpoint.h"
#include "gmanprimitivefactory.h"
#include "gmanray.h"
#include "gmanraybvh.h"
#include "gmanrayoccluder.h"
#include "gmanrayprimitive.h"
#include "gmanrenderer.h"
#include "gmanrenderprogress.h"
#include "gmansamplebuffer.h"
#include "gmansampling.h"
#include "gmanshading.h"
#include "gmantexture.h"
#include "gmanvector.h"
#include "gmanviewingsystem.h"
#include "gmanworldmanager.h"
#include "ri.h"

namespace gman {

RtFloat lightChoiceWeights(std::vector<gman::Emitter> const& emitters, GMANPoint const& p,
                           GMANRayPrimitive const* primitive, std::vector<RtFloat>& lightWeight) {
  RtFloat totalWeight = 0.0f;
  for (std::size_t j = 0; j < emitters.size(); ++j) {
    RtFloat const w = (emitters[j].shape == primitive) ? 0.0f : gman::lightChoiceWeight(emitters[j], p);
    lightWeight[j] = w;
    totalWeight += w;
  }
  return totalWeight;
}

RtFloat lightChoiceProbability(std::vector<gman::Emitter> const& emitters, GMANPoint const& p,
                               GMANRayPrimitive const* primitive, std::size_t index,
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

void addPath(SlotStatistics& stats, GMANColor const& L, GMANColor const& alphaHat) {
  std::array<double, SlotStatistics::kChannels> const values = {
      L.getRed(), L.getGreen(), L.getBlue(), alphaHat.getRed(), alphaHat.getGreen(), alphaHat.getBlue()};
  ++stats.paths;
  double const count = static_cast<double>(stats.paths);
  for (std::size_t q = 0; q < SlotStatistics::kChannels; ++q) {
    double const delta = values[q] - stats.mean[q];
    stats.mean[q] += delta / count;
    stats.sumSquaredDeviations[q] += delta * (values[q] - stats.mean[q]);
  }
}

std::uint32_t requiredPaths(SlotStatistics const& pilot, RtFloat tolerance, std::uint32_t minPaths,
                            std::uint32_t maxPaths) {
  if (pilot.paths < 2 || !(tolerance > 0.0f)) {
    return maxPaths;
  }
  double const degrees = static_cast<double>(pilot.paths) - 1.0;
  double variance = 0.0;
  for (double const sumSquares : pilot.sumSquaredDeviations) {
    double const channelVariance = sumSquares / degrees;
    if (!std::isfinite(channelVariance)) {
      return maxPaths;
    }
    variance = std::max(variance, channelVariance);
  }
  double const squaredTolerance = static_cast<double>(tolerance) * static_cast<double>(tolerance);
  double const required = std::floor(variance / squaredTolerance) + 1.0;
  return static_cast<std::uint32_t>(std::clamp(required, static_cast<double>(minPaths), static_cast<double>(maxPaths)));
}

} // namespace gman

namespace {

// Pins every image this plugin renders; changing it moves all of them.
constexpr std::uint32_t kSeed = 0x9e3779b9u;

// Seeds the fresh batch's own pattern, an independent hash stream from kSeed's.
constexpr std::uint32_t kFreshSeed = 0x7f4a7c15u;

// Russian roulette's own survival cap: every path ends with probability 1.
constexpr RtFloat kRouletteCap = (RtFloat)0.95;

constexpr char const* kDenoiseAtrous = "atrous";
constexpr char const* kDenoiseNone = "none";

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

// A single path's own contribution to a slot: its radiance and coverage
// estimate, whether its camera ray ever hit anything and, if so, that
// first hit's own camera-space z, and whether every channel it touched
// stayed finite. A guided path also carries that first hit's albedo and its
// camera-facing normal, the normal in the channels of a colour, each black
// where it was not finite.
struct PathResult {
  GMANColor L = kBlack;
  GMANColor alphaHat = kBlack;
  GMANColor albedo = kBlack;
  GMANColor normal = kBlack;
  bool finite = true;
  bool hasFirstHit = false;
  RtFloat firstHitZ = 0.0f;
};

GMANColor finiteOrBlack(GMANColor const& c) { return colorFinite(c) ? c : kBlack; }

// The camera ray's first hit's own guides: its albedo, where shade would
// shade point, and its normal, unit length and turned to face the ray.
void recordGuides(PathResult& result, gman::Appearance const& appearance, gman::SurfacePoint const& point,
                  GMANMatrix4 const& cameraToWorld, gman::TextureCache* textureCache) {
  result.albedo = finiteOrBlack(gman::albedo(appearance, point, cameraToWorld, textureCache));
  GMANNormal facing = point.N;
  facing.normalize();
  RtFloat const side = facing.dot(point.I) > 0.0f ? -1.0f : 1.0f;
  result.normal = finiteOrBlack(GMANColor(side * facing.getX(), side * facing.getY(), side * facing.getZ()));
}

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
// bounding radius so it is positive wherever it contributes; an
// emitter whose shape is primitive is excluded, since p sits on its own
// surface. j is drawn with probability its weight's share of the total.
// lightWeight is the caller's own per-path buffer, sized to emitters.size()
// and overwritten in place. Answers false, chosen and pj untouched, when no
// emitter has positive weight there.
bool chooseLight(std::vector<gman::Emitter> const& emitters, GMANPoint const& p, GMANRayPrimitive const* primitive,
                 std::uint32_t seed, RtInt sx, RtInt sy, std::uint32_t i, std::uint32_t N, std::uint32_t k,
                 std::vector<RtFloat>& lightWeight, std::size_t& chosen, RtFloat& pj) {
  RtFloat const totalWeight = lightChoiceWeights(emitters, p, primitive, lightWeight);
  if (!(totalWeight > 0.0f) || !std::isfinite(totalWeight)) {
    return false;
  }

  RtFloat const lightU = gman::sample1D(seed, sx, sy, i, N, 2u + 5u * k);
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
                               GMANColor const& cl, gman::ShadowWalkResult const& walk) {
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
// per-path buffer, sized to emitters.size() and overwritten in place.
// hitPrimitive is the shading point's own primitive, excluded from the
// choice: it cannot light a point on itself.
GMANColor nextEventEstimation(GMANRayBVH const& bvh, std::vector<gman::Emitter> const& emitters, GMANHit const& hit,
                              GMANRayPrimitive const* hitPrimitive, gman::SurfacePoint const& point,
                              gman::BSDF const& closure, GMANVector const& wo, GMANColor const& beta,
                              RtFloat surfaceMagnitude, GMANMatrix4 const& cameraToWorld,
                              gman::TextureCache* textureCache, std::uint32_t seed, RtInt sx, RtInt sy, std::uint32_t i,
                              std::uint32_t N, std::uint32_t k, std::vector<RtFloat>& lightWeight, bool misEnabled) {
  if (emitters.empty()) {
    return kBlack;
  }

  std::size_t chosen = 0;
  RtFloat pj = 0.0f;
  if (!chooseLight(emitters, hit.point, hitPrimitive, seed, sx, sy, i, N, k, lightWeight, chosen, pj)) {
    return kBlack;
  }

  gman::Sample2D const uv = gman::sample2D(seed, sx, sy, i, N, 3u + 5u * k);
  gman::EmitterSample const es = gman::sample(emitters[chosen], hit.point, uv.u1, uv.u2);
  if (!(es.pdf > 0.0f) || colorBlack(es.Cl)) {
    return kBlack;
  }

  GMANColor const f = scaleColor(closure.eval(wo, es.wi), gman::bumpShadowing(point.N, closure.shadingNormal(), es.wi));
  if (colorBlack(f)) {
    return kBlack;
  }

  RtFloat const pLight = pj * es.pdf;
  RtFloat const pBsdf = closure.pdf(wo, es.wi);
  RtFloat const weight = gman::nextEventWeight(es.isDelta, misEnabled, pLight, pBsdf);

  RtFloat const cosTerm = std::fabs(closure.shadingNormal().dot(es.wi));
  bool const shadowStartsAtOrigin = point.shadowOrigin && es.wi.dot(point.N) > (RtFloat)0.0;
  GMANPoint const shadowStart = shadowStartsAtOrigin ? *point.shadowOrigin : hit.point;
  GMANPoint const origin = gman::offsetOrigin(shadowStart, point.Ng, es.wi, surfaceMagnitude);
  ShadowWalkStart const walkStart = shadowWalkStart(origin, es);
  bool const startsInsideFacets =
      shadowStartsAtOrigin && es.wi.dot(point.Ng) < (RtFloat)0.0 && point.primitive != nullptr;
  gman::ShadowWalkResult const walk =
      gman::shadowWalk(bvh, origin, walkStart.wi, walkStart.distance, es.isDelta, es.shadowTarget, cameraToWorld,
                       textureCache, startsInsideFacets ? point.primitive : nullptr);

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
                                                gman::SurfacePoint const& point, std::uint32_t seed, RtInt sx, RtInt sy,
                                                std::uint32_t i, std::uint32_t N, std::uint32_t k, GMANColor& beta,
                                                RtFloat& etaScale, bool& finite) {
  gman::Sample2D const uvBsdf = gman::sample2D(seed, sx, sy, i, N, 4u + 5u * k);
  gman::BSDFSample const sample = closure.sample(wo, uvBsdf.u1, uvBsdf.u2);
  if (!(sample.pdf > 0.0f)) {
    return std::nullopt;
  }

  RtFloat const cosI = std::fabs(closure.shadingNormal().dot(sample.wi));
  GMANColor const f = scaleColor(sample.f, gman::bumpShadowing(point.N, closure.shadingNormal(), sample.wi));
  beta = gman::multiplyChannels(beta, scaleColor(f, cosI / sample.pdf));
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
    RtFloat const rouletteU = gman::sample1D(seed, sx, sy, i, N, 5u + 5u * k);
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
// there: black unless the hit's own area light is eligible and supported
// (present in emitters, which already filtered both; a face of a polygon
// mesh matches the emitter naming its mesh) and the hit's own geometric
// normal faces the incoming ray, and eligibility is not suppressed
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
GMANColor emitterHitLe(std::vector<gman::Emitter> const& emitters, GMANRayPrimitive const* hitPrimitive,
                       gman::Appearance const& appearance, GMANHit const& hit, gman::SurfacePoint const& point,
                       gman::EmitterHitEligibility eligibility, bool misEnabled, GMANPoint const& departureP,
                       GMANRayPrimitive const* departurePrimitive, RtFloat departurePdf,
                       std::vector<RtFloat>& lightWeight) {
  if (appearance.areaLight == nullptr || eligibility == gman::EmitterHitEligibility::suppressed) {
    return kBlack;
  }
  std::optional<std::size_t> const emitter = gman::emitterIndex(emitters, hitPrimitive);
  if (!emitter) {
    return kBlack;
  }
  std::size_t const j = *emitter;
  GMANNormal const geometricNormal(hit.normal.getX(), hit.normal.getY(), hit.normal.getZ());
  RtFloat const cosTheta = geometricNormal.dot(-point.I);
  if (!(cosTheta > 0.0f)) {
    return kBlack;
  }
  bool const rayEligible = eligibility == gman::EmitterHitEligibility::eligible;
  RtFloat pLight = 0.0f;
  if (misEnabled && !rayEligible) {
    pLight = gman::lightChoiceProbability(emitters, departureP, departurePrimitive, j, lightWeight) *
             gman::lightSolidAnglePdf(emitters[j], departureP, point.P, geometricNormal);
  }
  RtFloat const weight = gman::emitterHitWeight(rayEligible, misEnabled, departurePdf, pLight);
  return scaleColor(emitters[j].light->getCl(), weight);
}

// One path's own mutable state as it walks its vertices: its accumulated
// result, its throughput and eta-scale, whether it has scattered yet, the
// current ray, its run of straight pass-through vertices, the current
// emitter-hit eligibility, the departing vertex's own shading point,
// primitive and BSDF-sample density behind a non-delta departure (read
// only when eligibility is weighted), and the chooseLight scratch buffer,
// sized once to emitters.size() and reused at every vertex. guided asks the
// first vertex for its guides.
struct PathState {
  PathResult result;
  GMANColor beta = kWhite;
  RtFloat etaScale = 1.0f;
  bool hasScattered = false;
  // The camera ray itself is eligible: it carries no earlier BSDF draw, so
  // it has no earlier next-event estimate to double.
  gman::EmitterHitEligibility eligibility = gman::EmitterHitEligibility::eligible;
  GMANPoint departureP;
  GMANRayPrimitive const* departurePrimitive = nullptr;
  RtFloat departurePdf = 0.0f;
  int passThroughRun = 0;
  GMANRay ray;
  std::vector<RtFloat> lightWeight;
  bool guided;

  PathState(GMANRay const& cameraRay, std::size_t emitterCount, bool wantsGuides)
      : ray(cameraRay), lightWeight(emitterCount), guided(wantsGuides) {}
};

// One vertex's own work, once a hit is known: an eligible emitter hit's own
// Le, then coverage's pass-through or scattering with next-event
// estimation and a BSDF-sampled bounce. Updates state in place; answers
// false to end the path here.
bool tracePathVertex(GMANRayBVH const& bvh, std::vector<gman::Emitter> const& emitters,
                     GMANMatrix4 const& cameraToWorld, gman::TextureCache* textureCache, GMANHit const& hit,
                     GMANRayPrimitive const* hitPrimitive, std::uint32_t seed, RtInt sx, RtInt sy, std::uint32_t i,
                     std::uint32_t N, std::uint32_t k, bool misEnabled, PathState& state) {
  gman::SurfacePoint const point = gman::hitSurfacePoint(state.ray, hit);
  gman::Appearance const& appearance = hitPrimitive->getAppearance();
  RtFloat const surfaceMagnitude = point.surfaceMagnitude;

  if (k == 0 && state.guided) {
    recordGuides(state.result, appearance, point, cameraToWorld, textureCache);
  }

  state.result.L += gman::multiplyChannels(
      state.beta, emitterHitLe(emitters, hitPrimitive, appearance, hit, point, state.eligibility, misEnabled,
                               state.departureP, state.departurePrimitive, state.departurePdf, state.lightWeight));

  GMANColor const os = gman::clampCoverage(gman::opacity(appearance, point, cameraToWorld, textureCache));
  RtFloat const qPass = meanChannel(gman::oneMinus(os));
  RtFloat const coverageU = gman::sample1D(seed, sx, sy, i, N, 1u + 5u * k);
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
                          cameraToWorld, textureCache, seed, sx, sy, i, N, k, state.lightWeight, misEnabled);
  std::optional<BSDFStepDraw> const draw =
      bsdfStepAndRoulette(closure, wo, point, seed, sx, sy, i, N, k, state.beta, state.etaScale, state.result.finite);
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
// sample-grid coordinate and (i, N) the path's own index among the N of its
// pattern; every random draw is a pure function of seed, these four and a
// per-vertex dimension, so no state passes between paths or slots.
PathResult tracePath(GMANRayBVH const& bvh, std::vector<gman::Emitter> const& emitters,
                     GMANMatrix4 const& cameraToWorld, GMANColor const& background, gman::TextureCache* textureCache,
                     GMANRay cameraRay, std::uint32_t seed, RtInt sx, RtInt sy, std::uint32_t i, std::uint32_t N,
                     bool misEnabled, bool guided) {
  PathState state(cameraRay, emitters.size(), guided);
  bool escaped = false;
  for (std::uint32_t k = 0;; ++k) {
    GMANHit hit;
    GMANRayPrimitive const* hitPrimitive = nullptr;
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
    if (!tracePathVertex(bvh, emitters, cameraToWorld, textureCache, hit, hitPrimitive, seed, sx, sy, i, N, k,
                         misEnabled, state)) {
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

// One slot's own estimate, summed for render(): the mean radiance and
// coverage estimate, and the guides' means, go to the row's calls to
// zTestAndSet once the row knows whether any averaged path's camera ray ever
// hit anything; a dropped path contributes zero to every sum and is counted.
// estimatePaths is how many paths the sums average, and paths how many the
// slot traced in all, a pilot's included.
struct SlotResult {
  GMANColor sumL = kBlack;
  GMANColor sumAlpha = kBlack;
  GMANColor sumAlbedo = kBlack;
  GMANColor sumNormal = kBlack;
  RtFloat minZ = RI_INFINITY;
  bool anyHit = false;
  std::size_t dropped = 0;
  std::uint32_t paths = 0;
  std::uint32_t estimatePaths = 0;
};

// How a slot spends its paths: under a positive pixelVariance a pilot of
// pilotSize paths sizes a fresh batch, unless two pilots would reach the
// maximum.
struct SamplingPlan {
  RtFloat pixelVariance;
  std::uint32_t pilotSize;
};

bool usesPilot(SamplingPlan const& plan, std::uint32_t N) {
  return plan.pixelVariance > 0.0f && static_cast<std::uint64_t>(plan.pilotSize) * 2 < N;
}

// Everything one slot's paths share: the scene they trace, the slot's own
// absolute sample-grid coordinate (sx, sy) and the pixel's sample layout.
struct SlotTrace {
  GMANRayBVH const& bvh;
  std::vector<gman::Emitter> const& emitters;
  GMANViewingSystem const* viewingSys;
  GMANMatrix4 const& cameraToWorld;
  GMANColor const& background;
  gman::TextureCache* textureCache;
  RtInt sx;
  RtInt sy;
  int xsamples;
  int ysamples;
  bool misEnabled;
  bool guided;
};

// The sample buffers the guides accumulate into, both null when the render
// runs no denoise stage.
struct GuideSamples {
  GMANSampleBuffer* albedo = nullptr;
  GMANSampleBuffer* normal = nullptr;
};

PathResult tracePathInSlot(SlotTrace const& slot, std::uint32_t seed, std::uint32_t i, std::uint32_t patternSize) {
  gman::Sample2D const uv0 = gman::sample2D(seed, slot.sx, slot.sy, i, patternSize, 0u);
  RtFloat const rasterX = ((RtFloat)slot.sx + uv0.u1) / (RtFloat)slot.xsamples;
  RtFloat const rasterY = ((RtFloat)slot.sy + uv0.u2) / (RtFloat)slot.ysamples;
  GMANRay const cameraRay = slot.viewingSys->cameraRay(rasterX, rasterY);
  return tracePath(slot.bvh, slot.emitters, slot.cameraToWorld, slot.background, slot.textureCache, cameraRay, seed,
                   slot.sx, slot.sy, i, patternSize, slot.misEnabled, slot.guided);
}

void accumulateEstimate(SlotResult& result, PathResult const& path) {
  ++result.paths;
  ++result.estimatePaths;
  if (path.hasFirstHit && path.firstHitZ < result.minZ) {
    result.minZ = path.firstHitZ;
    result.anyHit = true;
  }
  if (!path.finite) {
    ++result.dropped;
    return;
  }
  result.sumL += path.L;
  result.sumAlpha += path.alphaHat;
  result.sumAlbedo += path.albedo;
  result.sumNormal += path.normal;
}

void traceEstimate(SlotTrace const& slot, std::uint32_t seed, std::uint32_t count, SlotResult& result) {
  for (std::uint32_t i = 0; i < count; ++i) {
    accumulateEstimate(result, tracePathInSlot(slot, seed, i, count));
  }
}

gman::SlotStatistics tracePilot(SlotTrace const& slot, std::uint32_t pilotSize, std::uint32_t N, SlotResult& result) {
  gman::SlotStatistics pilot;
  for (std::uint32_t i = 0; i < pilotSize; ++i) {
    PathResult const path = tracePathInSlot(slot, kSeed, i, N);
    ++result.paths;
    if (!path.finite) {
      ++result.dropped;
      gman::addPath(pilot, kBlack, kBlack);
      continue;
    }
    gman::addPath(pilot, path.L, path.alphaHat);
  }
  return pilot;
}

std::uint32_t defaultPilotSize(std::uint32_t N) {
  std::uint32_t root = static_cast<std::uint32_t>(std::sqrt(static_cast<double>(N)));
  while (static_cast<std::uint64_t>(root) * root < N) {
    ++root;
  }
  return GMANMax(root, 2u);
}

std::uint32_t pilotSize(GMANOptions const& options, std::uint32_t N) {
  GMANOptions::NamedOptionValue const* const named = options.getNamedOption("pathtracer", "minsamples");
  std::uint32_t const fallback = defaultPilotSize(N);
  if (named == nullptr) {
    return fallback;
  }
  auto const* const integers = std::get_if<std::vector<RtInt>>(named);
  if (integers == nullptr || integers->empty() || integers->front() < 2) {
    warning("gmanpathtracer: Option \"pathtracer\" \"minsamples\" must be an integer of at least 2; using {}.",
            fallback);
    return fallback;
  }
  return static_cast<std::uint32_t>(integers->front());
}

bool denoiseRequested(GMANOptions const& options) {
  GMANOptions::NamedOptionValue const* const named = options.getNamedOption("pathtracer", "denoise");
  if (named == nullptr) {
    return false;
  }
  auto const* const names = std::get_if<std::vector<std::string>>(named);
  if (names != nullptr && !names->empty() && names->front() == kDenoiseAtrous) {
    return true;
  }
  if (names == nullptr || names->empty() || names->front() != kDenoiseNone) {
    warning("gmanpathtracer: Option \"pathtracer\" \"denoise\" must be \"{}\" or \"{}\"; rendering without "
            "denoising.",
            kDenoiseAtrous, kDenoiseNone);
  }
  return false;
}

int denoiseIterations(GMANOptions const& options, int fallback) {
  GMANOptions::NamedOptionValue const* const named = options.getNamedOption("pathtracer", "denoiseiterations");
  if (named == nullptr) {
    return fallback;
  }
  auto const* const integers = std::get_if<std::vector<RtInt>>(named);
  if (integers == nullptr || integers->empty() || integers->front() < 1) {
    warning("gmanpathtracer: Option \"pathtracer\" \"denoiseiterations\" must be an integer of at least 1; using {}.",
            fallback);
    return fallback;
  }
  return integers->front();
}

std::optional<RtFloat> denoiseSigma(GMANOptions const& options, char const* parameter, std::string const& fallback) {
  GMANOptions::NamedOptionValue const* const named = options.getNamedOption("pathtracer", parameter);
  if (named == nullptr) {
    return std::nullopt;
  }
  auto const* const floats = std::get_if<std::vector<RtFloat>>(named);
  if (floats == nullptr || floats->empty() || !std::isfinite(floats->front()) || !(floats->front() > 0.0f)) {
    warning("gmanpathtracer: Option \"pathtracer\" \"{}\" must be a finite positive float; using {}.", parameter,
            fallback);
    return std::nullopt;
  }
  return floats->front();
}

gman::AtrousParameters atrousParameters(GMANOptions const& options) {
  gman::AtrousParameters const defaults;
  gman::AtrousParameters parameters;
  parameters.iterations = denoiseIterations(options, defaults.iterations);
  parameters.sigmaColor = denoiseSigma(options, "denoisecolor", "the median color norm in the image");
  parameters.sigmaNormal =
      denoiseSigma(options, "denoisenormal", std::format("{}", defaults.sigmaNormal)).value_or(defaults.sigmaNormal);
  parameters.sigmaAlbedo =
      denoiseSigma(options, "denoisealbedo", std::format("{}", defaults.sigmaAlbedo)).value_or(defaults.sigmaAlbedo);
  return parameters;
}

std::vector<GMANColor> readPixels(GMANFrameBuffer const& frameBuffer) {
  RtInt const width = frameBuffer.getWidth();
  RtInt const height = frameBuffer.getHeight();
  std::vector<GMANColor> pixels;
  pixels.reserve(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
  for (RtInt y = 0; y < height; ++y) {
    for (RtInt x = 0; x < width; ++x) {
      pixels.push_back(frameBuffer.getPixel(x, y));
    }
  }
  return pixels;
}

std::vector<GMANColor> resolvePixels(GMANSampleBuffer& samples, GMANFrameBuffer* frameBuffer,
                                     GMANOptions::PixelFilterStruct const& pf) {
  samples.resolve(frameBuffer, pf.filterfunc, pf.xwidth, pf.ywidth);
  return readPixels(*frameBuffer);
}

gman::GuidedImage resolveGuided(GMANFrameBuffer* frameBuffer, GMANOptions::PixelFilterStruct const& pf,
                                GMANSampleBuffer& colorSamples, GMANSampleBuffer& albedoSamples,
                                GMANSampleBuffer& normalSamples) {
  gman::GuidedImage image;
  image.width = frameBuffer->getWidth();
  image.height = frameBuffer->getHeight();
  image.albedo = resolvePixels(albedoSamples, frameBuffer, pf);
  for (GMANColor const& pixel : resolvePixels(normalSamples, frameBuffer, pf)) {
    image.normal.emplace_back(pixel.getRed(), pixel.getGreen(), pixel.getBlue());
  }
  image.color = resolvePixels(colorSamples, frameBuffer, pf);
  return image;
}

SlotResult renderSlot(SlotTrace const& slot, std::uint32_t N, SamplingPlan const& plan) {
  SlotResult result;
  if (!usesPilot(plan, N)) {
    traceEstimate(slot, kSeed, N, result);
    return result;
  }
  gman::SlotStatistics const pilot = tracePilot(slot, plan.pilotSize, N, result);
  std::uint32_t const fresh = gman::requiredPaths(pilot, plan.pixelVariance, plan.pilotSize, N - plan.pilotSize);
  traceEstimate(slot, kFreshSeed, fresh, result);
  return result;
}

// One row's own unit of work: traces every one of its slots and writes
// only its own slots of sampleBuffer and slotPaths, the dropped count and
// the texture cache it is handed, reading the BVH, the light set, the
// viewing system and the raster data it is handed as const. render() runs
// one row per gman::parallelFor body, handing each row the cache of the
// worker that runs it.
void renderRow(GMANRayBVH const& bvh, std::vector<gman::Emitter> const& emitters, GMANViewingSystem const* viewingSys,
               GMANMatrix4 const& cameraToWorld, GMANOptions::RasterInfo const& raster, RtInt width, int xsamples,
               int ysamples, std::uint32_t N, SamplingPlan const& plan, GMANColor const& background,
               gman::TextureCache& textureCache, int py, GMANSampleBuffer& sampleBuffer, GuideSamples const& guides,
               std::vector<std::uint32_t>& slotPaths, std::size_t& dropped, bool misEnabled) {
  int const subsPerColumn = xsamples * ysamples;
  for (int px = 0; px < width; ++px) {
    for (int sub = 0; sub < subsPerColumn; ++sub) {
      int const subX = sub % xsamples;
      int const subY = sub / xsamples;
      int const sampleX = px * xsamples + subX;
      int const sampleY = py * ysamples + subY;
      RtInt const sx = raster.rxmin * xsamples + sampleX;
      RtInt const sy = raster.rymin * ysamples + sampleY;

      SlotTrace const trace{bvh, emitters, viewingSys, cameraToWorld, background, &textureCache,
                            sx,  sy,       xsamples,   ysamples,      misEnabled, guides.albedo != nullptr};
      SlotResult const slot = renderSlot(trace, N, plan);
      slotPaths[static_cast<std::size_t>(sampleY) * static_cast<std::size_t>(width * xsamples) +
                static_cast<std::size_t>(sampleX)] = slot.paths;
      dropped += slot.dropped;
      if (!slot.anyHit) {
        continue;
      }
      GMANColor const meanL = divideColor(slot.sumL, (RtFloat)slot.estimatePaths);
      GMANColor const meanAlpha = divideColor(slot.sumAlpha, (RtFloat)slot.estimatePaths);
      GMANAlpha const alpha(meanAlpha.getRed(), meanAlpha.getGreen(), meanAlpha.getBlue());
      sampleBuffer.zTestAndSet(sampleX, sampleY, slot.minZ, meanL, alpha);
      if (guides.albedo != nullptr) {
        guides.albedo->zTestAndSet(sampleX, sampleY, slot.minZ,
                                   divideColor(slot.sumAlbedo, (RtFloat)slot.estimatePaths), alpha);
        guides.normal->zTestAndSet(sampleX, sampleY, slot.minZ,
                                   divideColor(slot.sumNormal, (RtFloat)slot.estimatePaths), alpha);
      }
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

void GMANPathtraceRenderer::setWorkers(RtInt workers) { requestedWorkers = workers; }

void GMANPathtraceRenderer::render(GMANFrameBuffer* frameBuffer, GMANViewingSystem* viewingSys,
                                   GMANOptions const& options, GMANAttributes const& /*attributes*/) {
  bvh.build(worldManager);
  gatherLights();
  droppedPaths = 0;
  guided = gman::GuidedImage();
  bool const denoise = denoiseRequested(options);

  RtInt const width = frameBuffer->getWidth();
  RtInt const height = frameBuffer->getHeight();

  GMANOptions::RasterInfo const raster = options.getRasterInfo();
  GMANOptions::PixelSamplesStruct const& ps = options.getPixelSamples();
  int const xsamples = GMANMax(1, (int)GMANRound(ps.xsamples));
  int const ysamples = GMANMax(1, (int)GMANRound(ps.ysamples));
  std::uint32_t const N = (std::uint32_t)GMANMax(1, options.getPathtracerSamples());
  RtFloat const pixelVariance = options.getPixelVariance();
  SamplingPlan const plan{pixelVariance, pilotSize(options, N)};

  GMANColor const background = options.getBackground();
  sampleBuffer.reset(new GMANSampleBuffer(width, height, xsamples, ysamples, background));
  std::unique_ptr<GMANSampleBuffer> albedoSamples;
  std::unique_ptr<GMANSampleBuffer> normalSamples;
  if (denoise) {
    albedoSamples.reset(new GMANSampleBuffer(width, height, xsamples, ysamples, kBlack));
    normalSamples.reset(new GMANSampleBuffer(width, height, xsamples, ysamples, kBlack));
  }
  GuideSamples const guides{albedoSamples.get(), normalSamples.get()};

  GMANMatrix4 const& cameraToWorld = options.getCameraToWorld();
  bool const misEnabled = multipleImportanceSamplingEnabled;

  std::vector<gman::TextureCache> textureCaches(
      static_cast<std::size_t>(gman::parallelWorkers(height, requestedWorkers)));
  std::vector<std::size_t> droppedPerRow(static_cast<std::size_t>(height), 0);
  rowWorker.assign(static_cast<std::size_t>(height), 0);
  std::size_t const slotCount =
      static_cast<std::size_t>(width * xsamples) * static_cast<std::size_t>(height * ysamples);
  slotPaths.assign(slotCount, 0);

  auto const renderRowAt = [&](RtInt row, RtInt worker) {
    auto const rowIndex = static_cast<std::size_t>(row);
    rowWorker[rowIndex] = worker;
    renderRow(bvh, emitters, viewingSys, cameraToWorld, raster, width, xsamples, ysamples, N, plan, background,
              textureCaches[static_cast<std::size_t>(worker)], row, *sampleBuffer, guides, slotPaths,
              droppedPerRow[rowIndex], misEnabled);
  };
  if (gman::progressRequested(options)) {
    auto progress = gman::RowProgress("gmanpathtracer", height);
    gman::parallelFor(height, renderRowAt, requestedWorkers, std::ref(progress));
  } else {
    gman::parallelFor(height, renderRowAt, requestedWorkers);
  }

  for (std::size_t const rowDropped : droppedPerRow) {
    droppedPaths += rowDropped;
  }

  if (droppedPaths != 0) {
    warning("gmanpathtracer: dropped {} path(s) for a non-finite channel.", droppedPaths);
  }
  if (pixelVariance > 0.0f) {
    std::size_t const traced = std::accumulate(slotPaths.begin(), slotPaths.end(), std::size_t{0});
    info("gmanpathtracer: traced {} of {} paths.", traced, static_cast<std::size_t>(N) * slotCount);
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
  if (!denoise) {
    sampleBuffer->resolve(frameBuffer, pf.filterfunc, pf.xwidth, pf.ywidth);
    return;
  }

  guided = resolveGuided(frameBuffer, pf, *sampleBuffer, *albedoSamples, *normalSamples);
  denoiseFrame(frameBuffer, atrousParameters(options));
}

void GMANPathtraceRenderer::denoiseFrame(GMANFrameBuffer* frameBuffer, gman::AtrousParameters const& parameters) {
  std::vector<GMANColor> const filtered = gman::atrousDenoise(guided, parameters, requestedWorkers);
  for (RtInt y = 0; y < guided.height; ++y) {
    for (RtInt x = 0; x < guided.width; ++x) {
      frameBuffer->setPixel(x, y, filtered[static_cast<std::size_t>(y) * static_cast<std::size_t>(guided.width) + x]);
    }
  }
  info("gmanpathtracer: denoised with the a-trous filter: {} iterations, sigma color {}, normal {}, albedo {}.",
       parameters.iterations, gman::atrousColorSigma(guided, parameters), parameters.sigmaNormal,
       parameters.sigmaAlbedo);
}

GMANWorldManager* GMANPathtraceRenderer::getWorldManager(void) { return &worldManager; }

GMANPrimitiveFactory* GMANPathtraceRenderer::getPrimitiveFactory(void) { return &primitiveFactory; }
