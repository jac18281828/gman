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

#include "gmancolor.h"
#include "gmantypes.h"

namespace gman {

// A channel clamped into [0, 1]: NaN, and anything at or below 0, maps to
// 0; positive infinity, and anything at or above 1, maps to 1. This is the
// clamp the output pipeline runs last, after gamma and quantize, so a
// float-to-integer narrowing downstream never meets an out-of-range or NaN
// value.
inline GMANColor::ColorSampleType clampedChannel(GMANColor::ColorSampleType v) {
  static_assert(GMANColor::hasFloatingPointSamples, "clamp designed for normalized floating point math");

  if (std::isnan(v) || v < 0) {
    return 0;
  }
  if (v > 1) {
    return 1;
  }
  return v;
}

// A channel narrowed to a byte: the clamp above, then the product of the
// clamped value and GMAN_BYTEMAX truncated in float. Total -- every input,
// NaN included, produces a byte in range -- since the clamp bounds the
// value before the cast. Truncating rather than rounding keeps every
// in-range byte identical to the historical output; rounding belongs to a
// real Quantize.
inline GMANByte narrowedByte(GMANColor::ColorSampleType v) {
  static_assert(GMANColor::hasFloatingPointSamples, "narrowing designed for normalized floating point math");

  return static_cast<GMANByte>(clampedChannel(v) * static_cast<GMANColor::ColorSampleType>(GMAN_BYTEMAX));
}

// Coverage narrowed from an alpha sample: the mean of its three channels,
// through narrowedByte. Alpha is linear coverage, never gamma-corrected,
// so this takes the raw sample rather than a colour already run through
// the output pipeline's gamma step.
inline GMANByte coverageByte(GMANAlpha const& alpha) {
  return narrowedByte((alpha.getRed() + alpha.getGreen() + alpha.getBlue()) /
                      static_cast<GMANColor::ColorSampleType>(3.0));
}

} // namespace gman
