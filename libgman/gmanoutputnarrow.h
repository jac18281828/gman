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

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "gmancolor.h"
#include "gmanquantize.h"
#include "gmantypes.h"

namespace gman {

// RISpec 3.2's own rule: round(one * v + ditheramplitude * xi) then clamp
// into [min, max], round as floor(x + 0.5). Runs in double -- at 16 bits,
// the same arithmetic in float missed an exact integer by one at a
// handful of pixels out of millions of draws, double none. A NaN v
// quantizes as 0 does; positive infinity clamps to max and negative
// infinity to min, both falling out of the clamp once floor(x + 0.5) has
// carried the infinity through. save is this function's only caller.
inline std::uint16_t quantizedSample(GMANColor::ColorSampleType v, GMANQuantize const& quantize, RtFloat xi) {
  static_assert(GMANColor::hasFloatingPointSamples, "quantizing designed for normalized floating point math");

  const double value = std::isnan(v) ? 0.0 : static_cast<double>(v);
  const double rounded = std::floor(static_cast<double>(quantize.one) * value + 0.5 +
                                    static_cast<double>(quantize.ditheramplitude) * static_cast<double>(xi));
  const double clamped = std::clamp(rounded, static_cast<double>(quantize.min), static_cast<double>(quantize.max));
  return static_cast<std::uint16_t>(clamped);
}

} // namespace gman
