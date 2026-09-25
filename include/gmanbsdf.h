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
 */

namespace gman {

enum class LobeKind { lambert };

struct GMAN_EXPORT Lobe {
  LobeKind kind;
  GMANColor weight;
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

  // Normalizes shadingNormal; every lobe measures its angles against it.
  explicit BSDF(GMANVector const& shadingNormal);

  // A two-sided Lambert lobe: reflectance / pi wherever wo and wi lie
  // strictly on one side of the normal. Each channel of reflectance clamps
  // to [0, 1], a NaN channel to 0, so the lobe creates no energy. Past
  // kMaxLobes, throws GMANError(RIE_LIMIT) and leaves the closure unchanged.
  void addLambert(GMANColor const& reflectance);

  std::size_t lobeCount() const;
  // index < lobeCount() is a precondition, checked by assert.
  Lobe const& lobe(std::size_t index) const;

  // The sum of the Lambert lobes' weights, unclamped: the closure's
  // directional reflectance, the integral of eval * |cos(theta_i)| over
  // the sphere. It stays at or below 1 per channel when the weights do.
  GMANColor rhoD() const;

  // The sum of every lobe's f.
  GMANColor eval(GMANVector const& wo, GMANVector const& wi) const;

  // The mixture sum of p_i * pdf_i, p_i being the probability sample()
  // picks lobe i.
  RtFloat pdf(GMANVector const& wo, GMANVector const& wi) const;

  // Picks lobe i with probability p_i, proportional to the mean of its
  // weight's channels, by u1; remaps u1 into [0, 1) within lobe i's share
  // and draws wi from lobe i with (u1, u2). Reports f = eval(wo, wi) and
  // pdf = pdf(wo, wi) at that wi. A draw fails for an empty or all-black
  // closure, a wo in the tangent plane, or a wi not strictly on wo's side.
  BSDFSample sample(GMANVector const& wo, RtFloat u1, RtFloat u2) const;

private:
  // The mean of lobe index's weight channels: sample()'s unnormalized
  // probability of picking it.
  RtFloat selectionWeight(std::size_t index) const;
  RtFloat totalSelectionWeight() const;

  GMANVector normal;
  std::array<Lobe, kMaxLobes> lobes{};
  std::size_t count = 0;
};

} // namespace gman
