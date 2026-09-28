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

#include <array>
#include <cstddef>
#include <utility>

#include "gmancolor.h"
#include "gmanvector.h"
#include "ri.h"

/*
 * How a surface scatters light at one hit: a closure of lobes a shader
 * returns from bsdf(env) and an integrator evaluates and samples. Every
 * lobe type lives here; a shader plugin combines lobes and defines none.
 *
 * Directions wo and wi are unit vectors pointing away from the surface, in
 * the space of the normal the closure was built with (camera space for a
 * shader); wo points toward the viewer. f is per unit solid angle and
 * excludes the cosine, and every pdf is per unit solid angle, so
 * f * |cos(theta_i)| / pdf weights a sample.
 *
 * A closure is a fixed-capacity value owning no heap memory: a shader
 * builds one per hit, and no plugin code runs after bsdf() returns.
 *
 * A mirror or dielectric lobe is a delta: for a given wo it scatters into
 * one direction, so it contributes nothing to eval or pdf, which state a
 * density over solid angle. sample() still picks it with probability p_i,
 * the same mixture share a non-delta lobe carries; a draw from it reports
 * isDelta true, pdf equal to the discrete probability of that exact draw
 * and f equal to the draw's coefficient over |cos(theta_i)|, so
 * f * |cos(theta_i)| / pdf still weights the sample. isDelta tells an
 * integrator that pdf is a probability, not a density: it must not feed a
 * delta's pdf to MIS's power heuristic, must give a delta bounce weight 1,
 * and finds eval black at a closure of delta lobes alone, so next-event
 * estimation there contributes nothing.
 */

namespace gman {

enum class LobeKind { lambert, ggx, mirror, dielectric };

struct GMAN_EXPORT Lobe {
  LobeKind kind;
  GMANColor weight;
  RtFloat alpha; // GGX's width; 0 for every other kind
  RtFloat eta;   // the dielectric's relative index; 0 for every other kind
};

// One draw from BSDF::sample. A failed draw reports pdf 0 and black f; its
// other fields carry no meaning.
struct GMAN_EXPORT BSDFSample {
  GMANVector wi;
  GMANColor f;
  RtFloat pdf;
  bool isDelta;
  std::size_t lobeIndex;
};

class GMAN_EXPORT BSDF {
public:
  static constexpr std::size_t kMaxLobes = 8;
  // Below this, GGX is a mirror in effect, the perfect-specular lobe's
  // territory; addGGX raises a lower alpha to it.
  static constexpr RtFloat kMinGGXAlpha = 0.01f;

  // Normalizes shadingNormal; every lobe measures its angles against it.
  explicit BSDF(GMANVector const& shadingNormal);

  // A two-sided Lambert lobe: reflectance / pi wherever wo and wi lie
  // strictly on one side of the normal. Each channel of reflectance clamps
  // to [0, 1], a NaN channel to 0, so the lobe creates no energy. Past
  // kMaxLobes, throws GMANError(RIE_LIMIT) and leaves the closure unchanged.
  void addLambert(GMANColor const& reflectance);

  // A two-sided, height-correlated Smith GGX reflection lobe, sampled by
  // visible normals. reflectance clamps as addLambert's does. alpha bounds
  // to [kMinGGXAlpha, 1], a NaN mapped to kMinGGXAlpha. Past kMaxLobes,
  // throws GMANError(RIE_LIMIT) and leaves the closure unchanged.
  void addGGX(GMANColor const& reflectance, RtFloat alpha);

  // A two-sided perfect mirror reflection, wi = 2(wo . n)n - wo, no
  // Fresnel factor. reflectance clamps as addLambert's does. Past
  // kMaxLobes, throws GMANError(RIE_LIMIT) and leaves the closure
  // unchanged.
  void addMirror(GMANColor const& reflectance);

  // A smooth dielectric interface: the closure's normal faces the side of
  // index 1, eta the relative index of the side behind it. Reflects with
  // Fresnel probability F and transmits with 1 - F, both delta branches. A
  // transmitted draw's coefficient carries the radiance scale
  // (eta_o / eta_i)^2, eta_o the index on wo's side and eta_i the index on
  // wi's side, so f * |cos(theta_i)| can exceed weight: leaving a glass
  // interface of index 1.5 at weight 1, it reaches 2.16. weight clamps as
  // addLambert's reflectance does; eta clamps to [0.01, 100], a NaN, zero
  // or negative eta stored as 1, an index-matched interface that transmits
  // straight through. Past kMaxLobes, throws GMANError(RIE_LIMIT) and
  // leaves the closure unchanged.
  void addDielectric(GMANColor const& weight, RtFloat eta);

  std::size_t lobeCount() const;
  // index < lobeCount() is a precondition, checked by assert.
  Lobe const& lobe(std::size_t index) const;

  // The sum of the Lambert lobes' weights alone, unclamped: the closure's
  // diffuse reflectance, a glossy lobe's contribution excluded. Stays at
  // or below 1 per channel when the Lambert weights sum to at most 1 per
  // channel.
  GMANColor rhoD() const;

  // The sum of every lobe's f.
  GMANColor eval(GMANVector const& wo, GMANVector const& wi) const;

  // The mixture sum of p_i * pdf_i, p_i being the probability sample()
  // picks lobe i.
  RtFloat pdf(GMANVector const& wo, GMANVector const& wi) const;

  // Picks lobe i with probability p_i, proportional to the mean of its
  // weight's channels, by u1; remaps u1 into [0, 1) within lobe i's share
  // and draws wi from lobe i with (u1, u2). A non-delta lobe reports
  // f = eval(wo, wi) and pdf = pdf(wo, wi) at that wi. A delta lobe
  // (mirror or dielectric) reports pdf equal to p_i times its branch's own
  // probability, and its coefficient over |cos(theta_i)| as f, with
  // isDelta true; see this header's own comment for what that asks of an
  // integrator. A draw fails for an empty or all-black closure, a wo in
  // the tangent plane, a reflection landing off wo's side, a transmission
  // landing off the far side, or a wi whose cos(theta_i) is 0.
  BSDFSample sample(GMANVector const& wo, RtFloat u1, RtFloat u2) const;

  // The closure's straight-through thin transmittance along a unit
  // direction w: the sum over its dielectric lobes of weight * (1 - F), F
  // evaluated as though w always enters from index 1 into that lobe's
  // eta, at |w . shadingNormal|, regardless of which side w arrives from.
  // A caller attenuates a shadow ray crossing a surface of coverage Os by
  // (1 - Os) + Os * T, T this query. Unclamped; |w . shadingNormal| == 0
  // answers black.
  GMANColor shadowTransmittance(GMANVector const& w) const;

private:
  // The mean of lobe index's weight channels: sample()'s unnormalized
  // probability of picking it.
  RtFloat selectionWeight(std::size_t index) const;
  RtFloat totalSelectionWeight() const;

  // Picks a lobe by u1 * total against the lobes' selection-weight shares of
  // [0, total), and remaps u1 into [0, 1) within the chosen lobe's share.
  // Rounding that carries the target past the last share lands on the last
  // lobe with a nonzero share. Precondition: total > 0.
  std::pair<std::size_t, RtFloat> pickLobe(RtFloat u1, RtFloat total) const;

  GMANVector normal;
  std::array<Lobe, kMaxLobes> lobes{};
  std::size_t count = 0;
};

} // namespace gman
