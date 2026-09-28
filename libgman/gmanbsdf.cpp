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

constexpr RtFloat kMinEta = 0.01f;
constexpr RtFloat kMaxEta = 100.0f;

// A NaN fails both comparisons and maps to 0.
RtFloat clampToUnit(RtFloat value) {
  if (!(value > 0.0f)) {
    return 0.0f;
  }
  return value < 1.0f ? value : 1.0f;
}

// A lobe's weight, each channel clamped to [0, 1], a NaN channel to 0.
GMANColor clampColor(GMANColor const& color) {
  return GMANColor(clampToUnit(color.getRed()), clampToUnit(color.getGreen()), clampToUnit(color.getBlue()));
}

// A NaN, and anything at or below it, fails the comparison and maps to
// BSDF::kMinGGXAlpha; anything above 1 maps to 1.
RtFloat clampAlpha(RtFloat alpha) {
  if (!(alpha > BSDF::kMinGGXAlpha)) {
    return BSDF::kMinGGXAlpha;
  }
  return alpha < 1.0f ? alpha : 1.0f;
}

// A NaN, zero or negative eta fails the comparison and maps to 1, an
// index-matched interface; anything else clamps to [kMinEta, kMaxEta].
RtFloat clampEta(RtFloat eta) {
  if (!(eta > 0.0f)) {
    return 1.0f;
  }
  return std::fmax(kMinEta, std::fmin(eta, kMaxEta));
}

// wo and wi lie strictly on one side of the surface.
bool sameSide(RtFloat cosThetaO, RtFloat cosThetaI) { return cosThetaO * cosThetaI > 0.0f; }

// wo and wi lie strictly on opposite sides of the surface.
bool oppositeSide(RtFloat cosThetaO, RtFloat cosThetaI) { return cosThetaO * cosThetaI < 0.0f; }

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
// tangent. At extreme grazing incidence the squared cosine underflows
// before the cosine itself does, sending Lambda to infinity; ggxEval
// guards the resulting zero G2.
RtFloat ggxLambda(RtFloat alpha, GMANVector const& local) {
  RtFloat const cosTheta = local.getZ();
  RtFloat const sinTheta2 = local.getX() * local.getX() + local.getY() * local.getY();
  RtFloat const tanTheta2 = sinTheta2 / (cosTheta * cosTheta);
  return 0.5f * (-1.0f + std::sqrt(1.0f + alpha * alpha * tanTheta2));
}

// D(h) and both directions' Lambda, shared by ggxEval and ggxPdf. woLocal
// and wiLocal are already known to lie strictly above the local horizon.
struct GGXGeometry {
  RtFloat d;
  RtFloat lambdaO;
  RtFloat lambdaI;
};

GGXGeometry ggxGeometry(RtFloat alpha, GMANVector const& woLocal, GMANVector const& wiLocal) {
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
  GGXGeometry const g = ggxGeometry(lobe.alpha, woLocal, wiLocal);
  RtFloat const g2 = 1.0f / (1.0f + g.lambdaO + g.lambdaI);
  if (!(g2 > 0.0f)) {
    return GMANColor(0.0f, 0.0f, 0.0f);
  }
  RtFloat const factor = g.d * g2 / (4.0f * cosThetaO * cosThetaI);
  return GMANColor(lobe.weight.getRed() * factor, lobe.weight.getGreen() * factor, lobe.weight.getBlue() * factor);
}

RtFloat ggxPdf(Lobe const& lobe, GMANVector const& woLocal, GMANVector const& wiLocal) {
  RtFloat const cosThetaO = woLocal.getZ();
  RtFloat const cosThetaI = wiLocal.getZ();
  if (!(cosThetaO > 0.0f) || !(cosThetaI > 0.0f)) {
    return 0.0f;
  }
  GGXGeometry const g = ggxGeometry(lobe.alpha, woLocal, wiLocal);
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
// wo's own horizon, which BSDF::sample's generic side test catches.
GMANVector ggxSample(TangentFrame const& frame, GMANVector const& woLocal, RtFloat alpha, RtFloat u1, RtFloat u2) {
  GMANVector const hLocal = ggxVisibleNormal(woLocal, alpha, u1, u2);
  RtFloat const woDotH = woLocal.dot(hLocal);
  GMANVector const wiLocal = hLocal * (2.0f * woDotH) - woLocal;
  return frame.toWorld(wiLocal);
}

GMANColor scaledColor(GMANColor c, RtFloat s) {
  c.scale(s);
  return c;
}

// c / |cosTheta|, the delta draw's f from its branch coefficient c.
GMANColor overAbsCos(GMANColor const& c, RtFloat cosTheta) { return scaledColor(c, 1.0f / std::fabs(cosTheta)); }

// wo reflected about n: invariant to n's sign, so it serves both the
// two-sided mirror lobe and the dielectric's reflection branch.
GMANVector reflectAbout(GMANVector const& n, GMANVector const& wo, RtFloat cosThetaO) {
  return n * (2.0f * cosThetaO) - wo;
}

// The dielectric's per-wo geometry: which side is index 1 and which is
// eta, the normal oriented onto wo's side, and wo's cosine against it.
struct DielectricGeometry {
  RtFloat etaO;
  RtFloat etaI;
  GMANVector nOriented;
  RtFloat cosThetaO;
};

DielectricGeometry dielectricGeometry(GMANVector const& normal, GMANVector const& wo, RtFloat eta) {
  RtFloat const cosWoN = normal.dot(wo);
  bool const entering = cosWoN > 0.0f;
  return {entering ? 1.0f : eta, entering ? eta : 1.0f, entering ? normal : -normal, std::fabs(cosWoN)};
}

// Fresnel's reflectance F and, unless total internal reflection, the
// transmitted cosine, from Snell's law and the exact unpolarized formula.
// Total internal reflection leaves cosThetaT at 0, unused since reflectance
// is 1.
struct FresnelResult {
  RtFloat reflectance;
  RtFloat cosThetaT;
};

FresnelResult dielectricFresnel(RtFloat etaO, RtFloat etaI, RtFloat cosThetaO) {
  RtFloat const sinThetaO2 = std::fmax(0.0f, 1.0f - cosThetaO * cosThetaO);
  RtFloat const ratio = etaO / etaI;
  RtFloat const sinThetaT2 = ratio * ratio * sinThetaO2;
  if (sinThetaT2 >= 1.0f) {
    return {1.0f, 0.0f};
  }
  RtFloat const cosThetaT = std::sqrt(1.0f - sinThetaT2);
  RtFloat const rs = (etaO * cosThetaO - etaI * cosThetaT) / (etaO * cosThetaO + etaI * cosThetaT);
  RtFloat const rp = (etaI * cosThetaO - etaO * cosThetaT) / (etaI * cosThetaO + etaO * cosThetaT);
  return {0.5f * (rs * rs + rp * rp), cosThetaT};
}

// The transmitted direction for wo entering nOriented's side at ratio =
// etaO / etaI, given Snell's own transmitted cosine.
GMANVector refractAbout(GMANVector const& nOriented, GMANVector const& wo, RtFloat ratio, RtFloat cosThetaO,
                        RtFloat cosThetaT) {
  return wo * (-ratio) + nOriented * (ratio * cosThetaO - cosThetaT);
}

// A delta reflection about the normal: the mirror lobe's whole draw, and the
// dielectric's reflection branch, share this fail check and this f = c /
// |cosThetaI| report.
BSDFSample deltaReflect(GMANVector const& normal, GMANVector const& wo, RtFloat cosThetaO, GMANColor const& c,
                        RtFloat pdf, std::size_t lobeIndex) {
  GMANVector const wi = reflectAbout(normal, wo, cosThetaO);
  RtFloat const cosThetaI = normal.dot(wi);
  if (!sameSide(cosThetaO, cosThetaI) || !(std::fabs(cosThetaI) > 0.0f)) {
    return failedSample();
  }
  return {wi, overAbsCos(c, cosThetaI), pdf, true, lobeIndex};
}

BSDFSample mirrorSample(GMANVector const& normal, GMANVector const& wo, RtFloat cosThetaO, Lobe const& lobe,
                        RtFloat pChosen, std::size_t lobeIndex) {
  return deltaReflect(normal, wo, cosThetaO, lobe.weight, pChosen, lobeIndex);
}

// The dielectric's draw: reflects with probability F, sharing deltaReflect,
// or transmits with probability 1 - F along Snell's law, its coefficient
// scaled by the radiance factor (etaO / etaI)^2.
BSDFSample dielectricSample(GMANVector const& normal, GMANVector const& wo, RtFloat cosThetaO, Lobe const& lobe,
                            RtFloat lobeU1, RtFloat pChosen, std::size_t lobeIndex) {
  DielectricGeometry const geo = dielectricGeometry(normal, wo, lobe.eta);
  FresnelResult const fresnel = dielectricFresnel(geo.etaO, geo.etaI, geo.cosThetaO);
  if (lobeU1 < fresnel.reflectance) {
    return deltaReflect(normal, wo, cosThetaO, scaledColor(lobe.weight, fresnel.reflectance),
                        pChosen * fresnel.reflectance, lobeIndex);
  }
  RtFloat const ratio = geo.etaO / geo.etaI;
  GMANVector const wi = refractAbout(geo.nOriented, wo, ratio, geo.cosThetaO, fresnel.cosThetaT);
  RtFloat const cosThetaI = normal.dot(wi);
  if (!oppositeSide(cosThetaO, cosThetaI) || !(std::fabs(cosThetaI) > 0.0f)) {
    return failedSample();
  }
  RtFloat const scale = ratio * ratio;
  RtFloat const transmitted = 1.0f - fresnel.reflectance;
  return {wi, overAbsCos(scaledColor(lobe.weight, transmitted * scale), cosThetaI), pChosen * transmitted, true,
          lobeIndex};
}

} // namespace

BSDF::BSDF(GMANVector const& shadingNormal) : normal(shadingNormal) { normal.normalize(); }

void BSDF::addLambert(GMANColor const& reflectance) {
  if (count == kMaxLobes) {
    throw GMANError(RIE_LIMIT, RIE_ERROR, "BSDF::addLambert: a closure holds at most kMaxLobes lobes");
  }
  const auto weight = clampColor(reflectance);
  lobes[count] = {LobeKind::lambert, weight, 0.0f, 0.0f};
  ++count;
}

void BSDF::addGGX(GMANColor const& reflectance, RtFloat alpha) {
  if (count == kMaxLobes) {
    throw GMANError(RIE_LIMIT, RIE_ERROR, "BSDF::addGGX: a closure holds at most kMaxLobes lobes");
  }
  const auto weight = clampColor(reflectance);
  lobes[count] = {LobeKind::ggx, weight, clampAlpha(alpha), 0.0f};
  ++count;
}

void BSDF::addMirror(GMANColor const& reflectance) {
  if (count == kMaxLobes) {
    throw GMANError(RIE_LIMIT, RIE_ERROR, "BSDF::addMirror: a closure holds at most kMaxLobes lobes");
  }
  const auto weight = clampColor(reflectance);
  lobes[count] = {LobeKind::mirror, weight, 0.0f, 0.0f};
  ++count;
}

void BSDF::addDielectric(GMANColor const& weight, RtFloat eta) {
  if (count == kMaxLobes) {
    throw GMANError(RIE_LIMIT, RIE_ERROR, "BSDF::addDielectric: a closure holds at most kMaxLobes lobes");
  }
  const auto clampedWeight = clampColor(weight);
  lobes[count] = {LobeKind::dielectric, clampedWeight, 0.0f, clampEta(eta)};
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
    case LobeKind::mirror:
    case LobeKind::dielectric:
      break; // A delta lobe has no density: it contributes nothing to eval.
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
    case LobeKind::mirror:
    case LobeKind::dielectric:
      break; // A delta lobe has no density: it contributes nothing to pdf.
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
  Lobe const& lobe = lobes[chosen];
  RtFloat const pChosen = selectionWeight(chosen) / total;
  // A non-delta draw's trailing report: eval and pdf at the drawn wi,
  // failing where wi lands off wo's side or the density is non-positive.
  auto const finishNonDelta = [&](GMANVector const& wi) -> BSDFSample {
    if (!sameSide(cosThetaO, normal.dot(wi))) {
      return failedSample();
    }
    RtFloat const density = pdf(wo, wi);
    if (!(density > 0.0f)) {
      return failedSample();
    }
    return {wi, eval(wo, wi), density, false, chosen};
  };
  switch (lobe.kind) {
  case LobeKind::lambert:
    return finishNonDelta(lambertSample(tangentFrame(normal), cosThetaO, lobeU1, u2));
  case LobeKind::ggx: {
    TangentFrame const frame = tangentFrame(flipToSide(normal, wo));
    return finishNonDelta(ggxSample(frame, frame.toLocal(wo), lobe.alpha, lobeU1, u2));
  }
  case LobeKind::mirror:
    return mirrorSample(normal, wo, cosThetaO, lobe, pChosen, chosen);
  case LobeKind::dielectric:
    return dielectricSample(normal, wo, cosThetaO, lobe, lobeU1, pChosen, chosen);
  }
  return failedSample();
}

GMANColor BSDF::shadowTransmittance(GMANVector const& w) const {
  RtFloat const cosTheta = std::fabs(normal.dot(w));
  GMANColor sum(0.0f, 0.0f, 0.0f);
  for (std::size_t i = 0; i < count; ++i) {
    if (lobes[i].kind != LobeKind::dielectric) {
      continue;
    }
    FresnelResult const fresnel = dielectricFresnel(1.0f, lobes[i].eta, cosTheta);
    sum += scaledColor(lobes[i].weight, 1.0f - fresnel.reflectance);
  }
  return sum;
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
