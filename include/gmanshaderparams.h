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

#include <cmath>
#include <numbers>
#include <string>
#include <string_view>

#include "gmanbsdf.h"
#include "gmancolor.h"
#include "gmanparameterlist.h"

namespace gmanshader {

// Every shader parameter is optional (RiSurface "matte" may pass none of
// them), so each helper answers its default for a name the list does not
// carry. A name matches a parameter declared by RiDeclare or inline, in any
// dictionary; a parameter of the wrong type or too short for the helper
// answers the default too.
inline RtFloat getFloatParam(GMANParameterList const& pl, std::string_view name, RtFloat def) {
  auto const values = pl.floats(name);
  return values.empty() ? def : values[0];
}

inline GMANColor getColorParam(GMANParameterList const& pl, std::string_view name, GMANColor const& def) {
  auto const values = pl.floats(name);
  return values.size() < 3 ? def : GMANColor(values[0], values[1], values[2]);
}

inline std::string getStringParam(GMANParameterList const& pl, std::string_view name, std::string const& def) {
  auto const values = pl.strings(name);
  return values.empty() ? def : values[0];
}

// Clamps an albedo() answer's channels to [0, 1], the range every override
// answers in. GMANMax(NaN, 0) takes its right operand, since a NaN
// comparison is always false, so a NaN channel maps to exactly 0.
inline GMANColor clampAlbedo(GMANColor const& c) {
  auto const clampChannel = [](RtFloat v) { return GMANMin(GMANMax(v, (RtFloat)0.0), (RtFloat)1.0); };
  return GMANColor(clampChannel(c.getRed()), clampChannel(c.getGreen()), clampChannel(c.getBlue()));
}

// GGX's alpha whose D falls to half where specular()'s Blinn-Phong
// highlight does, for RenderMan roughness r: 0 for r <= 0 or NaN, which
// BSDF::addGGX raises to kMinGGXAlpha.
inline RtFloat alphaFromRoughness(RtFloat roughness) {
  if (!(roughness > (RtFloat)0.0)) {
    return (RtFloat)0.0;
  }
  double const cosSquaredHalfMax = std::pow(4.0, -(double)roughness);
  return (RtFloat)std::sqrt((1.0 - cosSquaredHalfMax) / (std::numbers::sqrt2 - cosSquaredHalfMax));
}

// The scalar plastic's and paintedplastic's GGX lobe shares across
// channels so the two lobes never exceed 1 combined: the least headroom
// (1 - diffuse_c) / specular_c over channels with specular_c > 0, or 1
// when every specular_c is 0. diffuse and specular are each already
// clamped to [0, 1].
inline RtFloat ggxHeadroom(GMANColor const& diffuse, GMANColor const& specular) {
  RtFloat k = (RtFloat)1.0;
  RtFloat const d[3] = {diffuse.getRed(), diffuse.getGreen(), diffuse.getBlue()};
  RtFloat const s[3] = {specular.getRed(), specular.getGreen(), specular.getBlue()};
  for (int c = 0; c < 3; ++c) {
    if (s[c] > (RtFloat)0.0) {
      k = GMANMin(k, (1.0f - d[c]) / s[c]);
    }
  }
  return k;
}

// plastic's and paintedplastic's closure: a Lambert lobe of d plus a GGX
// lobe of s scaled by the headroom d leaves it, at roughness's alpha. d
// and s are each already clamped to [0, 1].
inline void addPlasticLobes(gman::BSDF& closure, GMANColor const& d, GMANColor const& s, RtFloat roughness) {
  RtFloat const k = ggxHeadroom(d, s);
  closure.addLambert(d);
  closure.addGGX(GMANColor(k * s.getRed(), k * s.getGreen(), k * s.getBlue()), alphaFromRoughness(roughness));
}

} // namespace gmanshader
