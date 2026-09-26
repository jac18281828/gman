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

constexpr RtFloat kInvPi = 1.0f / 3.14159265358979323846f;

// A NaN fails both comparisons and maps to 0.
RtFloat clampToUnit(RtFloat value) {
  if (!(value > 0.0f)) {
    return 0.0f;
  }
  return value < 1.0f ? value : 1.0f;
}

// wo and wi lie strictly on one side of the surface.
bool sameSide(RtFloat cosThetaO, RtFloat cosThetaI) { return cosThetaO * cosThetaI > 0.0f; }

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

} // namespace

BSDF::BSDF(GMANVector const& shadingNormal) : normal(shadingNormal) { normal.normalize(); }

void BSDF::addLambert(GMANColor const& reflectance) {
  if (count == kMaxLobes) {
    throw GMANError(RIE_LIMIT, RIE_ERROR, "BSDF::addLambert: a closure holds at most kMaxLobes lobes");
  }
  GMANColor const weight(clampToUnit(reflectance.getRed()), clampToUnit(reflectance.getGreen()),
                         clampToUnit(reflectance.getBlue()));
  lobes[count] = {LobeKind::lambert, weight};
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
  GMANColor sum(0.0f, 0.0f, 0.0f);
  for (std::size_t i = 0; i < count; ++i) {
    switch (lobes[i].kind) {
    case LobeKind::lambert:
      sum += lambertEval(lobes[i], cosThetaO, cosThetaI);
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
  RtFloat mixture = 0.0f;
  for (std::size_t i = 0; i < count; ++i) {
    RtFloat const probability = selectionWeight(i) / total;
    switch (lobes[i].kind) {
    case LobeKind::lambert:
      mixture += probability * lambertPdf(cosThetaO, cosThetaI);
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
