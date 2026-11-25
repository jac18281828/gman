/* SPDX-License-Identifier: LGPL-2.1-or-later */

/* This is part of GMAN, a RenderMan-compatible renderer.
 *
 * Copyright (c) 2001, 2000, 1999 John Cairns
 *
 * Author: John Cairns <john@2ad.com>
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

// Make this file self-contained - asandro
#include <algorithm>
#include <cmath>
#include <concepts>
#include <numbers>
#include <type_traits>

#include <math.h>

#include "ri.h"

// GMan constants
inline constexpr double DEGTORAD = std::numbers::pi / 180.0;

// Trigonometric functions - asandro
constexpr RtFloat GMANRadians(RtFloat degrees) { return (RtFloat)(degrees * DEGTORAD); }

constexpr RtFloat GMANDegrees(RtFloat radians) { return (RtFloat)(radians / DEGTORAD); }

inline RtFloat GMANAtan(RtFloat y, RtFloat x) { return (RtFloat)atan2(y, x); }

// Square root & logarithmic - asandro
inline RtFloat GMANInversesqrt(RtFloat x) {
  RtFloat y = (RtFloat)sqrt(x);
  // avoid division by zero
  if (y - RI_EPSILON > 0.0) {
    return (RtFloat)(1.0 / y);
  } else {
    return (RtFloat)RI_INFINITY;
  }
}

inline RtFloat GMANLogFn(RtFloat x, RtFloat base) { return (RtFloat)(log(x) / log(base)); }

// Module functions - asandro
inline RtFloat GMANMod(RtFloat a, RtFloat b) {
  if (a < 0)
    return b - (RtFloat)fmod(-a, b);
  else
    return (RtFloat)fmod(a, b);
}

constexpr RtFloat GMANSign(RtFloat x) { return (RtFloat)(x < 0.0 ? -1.0 : x > 0.0 ? 1.0 : 0.0); }

// Comparison functions - asandro
//
// GMANMin, GMANMax and GMANClamp drop a NaN operand. For a floating type,
// GMANMin and GMANMax answer the number when one operand is NaN and NaN only
// when both are, as C's fmin and fmax (IEEE 754-2008 minNum and maxNum) do,
// and GMANClamp(v, lo, hi) is GMANMin(GMANMax(v, lo), hi): a NaN v answers lo
// and a NaN bound is ignored. Every other type follows std::min, std::max and
// std::clamp. All three are constexpr.
template <class T> constexpr T GMANMin(T a, T b) {
  if constexpr (std::floating_point<T>) {
    if (std::is_constant_evaluated()) {
      if (a != a)
        return b;
      return b < a ? b : a;
    }
    return std::fmin(a, b);
  } else {
    return std::min(a, b);
  }
}

template <class T> constexpr T GMANMax(T a, T b) {
  if constexpr (std::floating_point<T>) {
    if (std::is_constant_evaluated()) {
      if (a != a)
        return b;
      return b > a ? b : a;
    }
    return std::fmax(a, b);
  } else {
    return std::max(a, b);
  }
}

template <class T> constexpr T GMANClamp(T a, T min, T max) {
  if constexpr (std::floating_point<T>) {
    return GMANMin(GMANMax(a, min), max);
  } else {
    return std::clamp(a, min, max);
  }
}

// Mixing values - asandro
template <class T> constexpr T GMANMix(T a, T b, RtFloat alpha) { return a * (1 - alpha) + b * alpha; }

// Rounding - asandro
inline RtFloat GMANRound(RtFloat x) { return (RtFloat)floor(x + .5); }
