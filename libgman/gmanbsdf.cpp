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

#include <cassert>
#include <cmath>
#include <cstddef>

#include "gmanbsdf.h"
#include "gmancolor.h"
#include "gmanerror.h"
#include "gmansampling.h"
#include "gmanvector.h"
#include "ri.h"

namespace gman {

namespace {

constexpr RtFloat kPi = 3.14159265358979323846f;
constexpr RtFloat kTwoPi = 2.0f * kPi;
constexpr RtFloat kInvPi = 1.0f / kPi;

// A NaN fails both comparisons and maps to 0.
RtFloat clampToUnit(RtFloat value) {
  if (!(value > 0.0f)) {
    return 0.0f;
  }
  return value < 1.0f ? value : 1.0f;
}

// A NaN, and anything at or below it, fails the comparison and maps to
// BSDF::kMinGGXAlpha; anything above 1 maps to 1.
RtFloat clampAlpha(RtFloat alpha) {
  if (!(alpha > BSDF::kMinGGXAlpha)) {
    return BSDF::kMinGGXAlpha;
  }
  return alpha < 1.0f ? alpha : 1.0f;
}

// wo and wi lie strictly on one side of the surface.
bool sameSide(RtFloat cosThetaO, RtFloat cosThetaI) { return cosThetaO * cosThetaI > 0.0f; }

// shadingNormal flipped to lie on wo's side, so a lobe measures wo's and
// wi's angles, and builds its tangent frame, against a normal wo is above.
GMANVector flipToSide(GMANVector const& shadingNormal, GMANVector const& wo) {
  return shadingNormal.dot(wo) < 0.0f ? -shadingNormal : shadingNormal;
}

BSDFSample failedSample() { return {GMANVector(0.0f, 0.0f, 0.0f), GMANColor(0.0f, 0.0f, 0.0f), 0.0f, false, 0}; }

GMANColor lambertEval(Lobe const& lobe, RtFloat cosThetaO, RtFloat cosThetaI) {
  if (!sameSide(cosThetaO, cosThetaI)) {
    return GMANColor(0.0f, 0.0f, 0.0f);
  }
  return GMANColor(lobe.weight.getRed() * kInvPi, lobe.weight.getGreen() * kInvPi, lobe.weight.getBlue() * kInvPi);
}

RtFloat lambertPdf(RtFloat cosThetaO, RtFloat cosThetaI) {
  if (!sameSide(cosThetaO, cosThetaI)) {
    return 0.0f;
  }
  return std::fabs(cosThetaI) * kInvPi;
}

// A cosine-weighted direction about the normal, mirrored to wo's side.
GMANVector lambertSample(TangentFrame const& frame, RtFloat cosThetaO, RtFloat u1, RtFloat u2) {
  GMANVector local = cosineHemisphere(u1, u2);
  if (cosThetaO < 0.0f) {
    local.setZ(-local.getZ());
  }
  return frame.toWorld(local);
}

// The height-correlated Smith Lambda of a direction expressed in the
// lobe's own local frame (+z the flipped normal): tan^2(theta) from the
// local x/y and z components directly, rather than through an explicit
// tangent, so a grazing direction (z near 0) never divides by a value
// smaller than its own square.
RtFloat ggxLambda(RtFloat alpha, GMANVector const& local) {
  RtFloat const cosTheta = local.getZ();
  RtFloat const sinTheta2 = local.getX() * local.getX() + local.getY() * local.getY();
  RtFloat const tanTheta2 = sinTheta2 / (cosTheta * cosTheta);
  return 0.5f * (-1.0f + std::sqrt(1.0f + alpha * alpha * tanTheta2));
}

// D(h) and both directions' Lambda, shared by ggxEval and ggxPdf. woLocal
// and wiLocal are already known to lie strictly above the local horizon.
struct GgxGeometry {
  RtFloat d;
  RtFloat lambdaO;
  RtFloat lambdaI;
};

GgxGeometry ggxGeometry(RtFloat alpha, GMANVector const& woLocal, GMANVector const& wiLocal) {
  GMANVector h = woLocal + wiLocal;
  h.normalize();
  RtFloat const cosThetaH = h.getZ();
  RtFloat const alpha2 = alpha * alpha;
  RtFloat const denom = cosThetaH * cosThetaH * (alpha2 - 1.0f) + 1.0f;
  RtFloat const d = alpha2 * kInvPi / (denom * denom);
  return {d, ggxLambda(alpha, woLocal), ggxLambda(alpha, wiLocal)};
}

GMANColor ggxEval(Lobe const& lobe, GMANVector const& woLocal, GMANVector const& wiLocal) {
  RtFloat const cosThetaO = woLocal.getZ();
  RtFloat const cosThetaI = wiLocal.getZ();
  if (!(cosThetaO > 0.0f) || !(cosThetaI > 0.0f)) {
    return GMANColor(0.0f, 0.0f, 0.0f);
  }
  GgxGeometry const g = ggxGeometry(lobe.alpha, woLocal, wiLocal);
  RtFloat const g2 = 1.0f / (1.0f + g.lambdaO + g.lambdaI);
  RtFloat const factor = g.d * g2 / (4.0f * cosThetaO * cosThetaI);
  return GMANColor(lobe.weight.getRed() * factor, lobe.weight.getGreen() * factor, lobe.weight.getBlue() * factor);
}

RtFloat ggxPdf(Lobe const& lobe, GMANVector const& woLocal, GMANVector const& wiLocal) {
  RtFloat const cosThetaO = woLocal.getZ();
  RtFloat const cosThetaI = wiLocal.getZ();
  if (!(cosThetaO > 0.0f) || !(cosThetaI > 0.0f)) {
    return 0.0f;
  }
  GgxGeometry const g = ggxGeometry(lobe.alpha, woLocal, wiLocal);
  RtFloat const g1O = 1.0f / (1.0f + g.lambdaO);
  return g1O * g.d / (4.0f * cosThetaO);
}

// Heitz's sample of GGX's distribution of visible normals ("Sampling the
// GGX Distribution of Visible Normals", JCGT 7(4), 2018), isotropic alpha,
// entirely in woLocal's own frame: stretch wo into the hemisphere the
// distribution is uniform on, sample a disk, and unstretch back.
GMANVector ggxVisibleNormal(GMANVector const& woLocal, RtFloat alpha, RtFloat u1, RtFloat u2) {
  GMANVector vh(alpha * woLocal.getX(), alpha * woLocal.getY(), woLocal.getZ());
  vh.normalize();

  RtFloat const lengthSq = vh.getX() * vh.getX() + vh.getY() * vh.getY();
  GMANVector const t1 = lengthSq > 0.0f ? GMANVector(-vh.getY(), vh.getX(), 0.0f) * (1.0f / std::sqrt(lengthSq))
                                        : GMANVector(1.0f, 0.0f, 0.0f);
  GMANVector const t2 = vh.cross(t1);

  RtFloat const r = std::sqrt(u1);
  RtFloat const phi = kTwoPi * u2;
  RtFloat const t1Coord = r * std::cos(phi);
  RtFloat t2Coord = r * std::sin(phi);
  RtFloat const s = 0.5f * (1.0f + vh.getZ());
  t2Coord = (1.0f - s) * std::sqrt(std::fmax(0.0f, 1.0f - t1Coord * t1Coord)) + s * t2Coord;

  RtFloat const nz = std::sqrt(std::fmax(0.0f, 1.0f - t1Coord * t1Coord - t2Coord * t2Coord));
  GMANVector const nh = t1 * t1Coord + t2 * t2Coord + vh * nz;

  GMANVector ne(alpha * nh.getX(), alpha * nh.getY(), std::fmax(0.0f, nh.getZ()));
  ne.normalize();
  return ne;
}

// wo reflected about a visible-normal draw, in world space; may land below
// wiLocal's own horizon, which BSDF::sample's generic side test catches.
GMANVector ggxSample(TangentFrame const& frame, GMANVector const& woLocal, RtFloat alpha, RtFloat u1, RtFloat u2) {
  GMANVector const hLocal = ggxVisibleNormal(woLocal, alpha, u1, u2);
  RtFloat const woDotH = woLocal.dot(hLocal);
  GMANVector const wiLocal = hLocal * (2.0f * woDotH) - woLocal;
  return frame.toWorld(wiLocal);
}

} // namespace

BSDF::BSDF(GMANVector const& shadingNormal) : normal(shadingNormal) { normal.normalize(); }

void BSDF::addLambert(GMANColor const& reflectance) {
  if (count == kMaxLobes) {
    throw GMANError(RIE_LIMIT, RIE_ERROR, "BSDF::addLambert: a closure holds at most kMaxLobes lobes");
  }
  GMANColor const weight(clampToUnit(reflectance.getRed()), clampToUnit(reflectance.getGreen()),
                         clampToUnit(reflectance.getBlue()));
  lobes[count] = {LobeKind::lambert, weight, 0.0f};
  ++count;
}

void BSDF::addGGX(GMANColor const& reflectance, RtFloat alpha) {
  if (count == kMaxLobes) {
    throw GMANError(RIE_LIMIT, RIE_ERROR, "BSDF::addGGX: a closure holds at most kMaxLobes lobes");
  }
  GMANColor const weight(clampToUnit(reflectance.getRed()), clampToUnit(reflectance.getGreen()),
                         clampToUnit(reflectance.getBlue()));
  lobes[count] = {LobeKind::ggx, weight, clampAlpha(alpha)};
  ++count;
}

std::size_t BSDF::lobeCount() const { return count; }

Lobe const& BSDF::lobe(std::size_t index) const {
  assert(index < count);
  return lobes[index];
}

GMANColor BSDF::rhoD() const {
  GMANColor sum(0.0f, 0.0f, 0.0f);
  for (std::size_t i = 0; i < count; ++i) {
    if (lobes[i].kind == LobeKind::lambert) {
      sum += lobes[i].weight;
    }
  }
  return sum;
}

GMANColor BSDF::eval(GMANVector const& wo, GMANVector const& wi) const {
  RtFloat const cosThetaO = normal.dot(wo);
  RtFloat const cosThetaI = normal.dot(wi);
  TangentFrame const frame = tangentFrame(flipToSide(normal, wo));
  GMANVector const woLocal = frame.toLocal(wo);
  GMANVector const wiLocal = frame.toLocal(wi);
  GMANColor sum(0.0f, 0.0f, 0.0f);
  for (std::size_t i = 0; i < count; ++i) {
    switch (lobes[i].kind) {
    case LobeKind::lambert:
      sum += lambertEval(lobes[i], cosThetaO, cosThetaI);
      break;
    case LobeKind::ggx:
      sum += ggxEval(lobes[i], woLocal, wiLocal);
      break;
    }
  }
  return sum;
}

RtFloat BSDF::pdf(GMANVector const& wo, GMANVector const& wi) const {
  RtFloat const total = totalSelectionWeight();
  if (!(total > 0.0f)) {
    return 0.0f;
  }
  RtFloat const cosThetaO = normal.dot(wo);
  RtFloat const cosThetaI = normal.dot(wi);
  TangentFrame const frame = tangentFrame(flipToSide(normal, wo));
  GMANVector const woLocal = frame.toLocal(wo);
  GMANVector const wiLocal = frame.toLocal(wi);
  RtFloat mixture = 0.0f;
  for (std::size_t i = 0; i < count; ++i) {
    RtFloat const probability = selectionWeight(i) / total;
    switch (lobes[i].kind) {
    case LobeKind::lambert:
      mixture += probability * lambertPdf(cosThetaO, cosThetaI);
      break;
    case LobeKind::ggx:
      mixture += probability * ggxPdf(lobes[i], woLocal, wiLocal);
      break;
    }
  }
  return mixture;
}

BSDFSample BSDF::sample(GMANVector const& wo, RtFloat u1, RtFloat u2) const {
  RtFloat const total = totalSelectionWeight();
  RtFloat const cosThetaO = normal.dot(wo);
  if (!(total > 0.0f) || !(std::fabs(cosThetaO) > 0.0f)) {
    return failedSample();
  }

  auto const [chosen, lobeU1] = pickLobe(u1, total);

  GMANVector wi;
  switch (lobes[chosen].kind) {
  case LobeKind::lambert:
    wi = lambertSample(tangentFrame(normal), cosThetaO, lobeU1, u2);
    break;
  case LobeKind::ggx: {
    TangentFrame const frame = tangentFrame(flipToSide(normal, wo));
    wi = ggxSample(frame, frame.toLocal(wo), lobes[chosen].alpha, lobeU1, u2);
    break;
  }
  }

  if (!sameSide(cosThetaO, normal.dot(wi))) {
    return failedSample();
  }
  RtFloat const density = pdf(wo, wi);
  if (!(density > 0.0f)) {
    return failedSample();
  }
  return {wi, eval(wo, wi), density, false, chosen};
}

std::pair<std::size_t, RtFloat> BSDF::pickLobe(RtFloat u1, RtFloat total) const {
  RtFloat const target = u1 * total;
  std::size_t chosen = 0;
  RtFloat shareStart = 0.0f;
  RtFloat shareEnd = 0.0f;
  for (std::size_t i = 0; i < count; ++i) {
    RtFloat const share = selectionWeight(i);
    if (!(share > 0.0f)) {
      continue;
    }
    chosen = i;
    shareStart = shareEnd;
    shareEnd += share;
    if (target < shareEnd) {
      break;
    }
  }
  RtFloat const remapped = (target - shareStart) / selectionWeight(chosen);
  return {chosen, std::fmin(std::fmax(remapped, 0.0f), std::nextafter(1.0f, 0.0f))};
}

RtFloat BSDF::selectionWeight(std::size_t index) const {
  GMANColor const& weight = lobes[index].weight;
  return (weight.getRed() + weight.getGreen() + weight.getBlue()) / 3.0f;
}

RtFloat BSDF::totalSelectionWeight() const {
  RtFloat total = 0.0f;
  for (std::size_t i = 0; i < count; ++i) {
    total += selectionWeight(i);
  }
  return total;
}

} // namespace gman
