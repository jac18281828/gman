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

#ifndef __GMAN_MATH_H
#define __GMAN_MATH_H 1

// Make this file self-contained - asandro
#include <math.h>

#include "ri.h"

// GMan constants
const double PI = 3.1415926535898;
const double DEGTORAD = PI / 180.0;

#define GMANMAX(x, y) (x > y ? x : y)
#define GMANMIN(x, y) (x < y ? x : y)

#if 0
inline GMAN_EXPORT  RtFloat GMANClamp(RtFloat value, RtFloat min, RtFloat max)
{
  if(value < min) return min;
  if(value > max) return max;
  return value;
}
#endif

// Trigonometric functions - asandro
inline GMAN_EXPORT RtFloat GMANRadians(RtFloat degrees) { return (RtFloat)(degrees * DEGTORAD); }

inline GMAN_EXPORT RtFloat GMANDegrees(RtFloat radians) { return (RtFloat)(radians / DEGTORAD); }

inline GMAN_EXPORT RtFloat GMANAtan(RtFloat y, RtFloat x) { return (RtFloat)atan2(y, x); }

// Square root & logarithmic - asandro
inline GMAN_EXPORT RtFloat GMANInversesqrt(RtFloat x) {
  RtFloat y = (RtFloat)sqrt(x);
  // avoid division by zero
  if (y - RI_EPSILON > 0.0) {
    return (RtFloat)(1.0 / y);
  } else {
    return (RtFloat)RI_INFINITY;
  }
}

inline GMAN_EXPORT RtFloat GMANLogFn(RtFloat x, RtFloat base) { return (RtFloat)(log(x) / log(base)); }

// Module functions - asandro
inline GMAN_EXPORT RtFloat GMANMod(RtFloat a, RtFloat b) {
  if (a < 0)
    return b - (RtFloat)fmod(-a, b);
  else
    return (RtFloat)fmod(a, b);
}

inline GMAN_EXPORT RtFloat GMANSign(RtFloat x) { return (RtFloat)(x < 0.0 ? -1.0 : x > 0.0 ? 1.0 : 0.0); }

// Comparison functions - asandro
template <class T> inline T GMANMin(T a, T b) { return a < b ? a : b; }

template <class T> inline T GMANMax(T a, T b) { return a > b ? a : b; }

template <class T> inline T GMANClamp(T a, T min, T max) { return a < min ? min : a > max ? max : a; }

// Mixing values - asandro
template <class T> inline T GMANMix(T a, T b, RtFloat alpha) { return a * (1 - alpha) + b * alpha; }

// Rounding - asandro
inline RtFloat GMANRound(RtFloat x) { return (RtFloat)floor(x + .5); }

// Solves a*t^2 + b*t + c == 0, returning the count of real roots (0, 1 or
// 2) and writing t0 <= t1 when it returns 2. a == 0 returns 0 rather than
// falling back to a linear solve, since a degenerate ray direction is the
// only way to reach it here. The textbook (-b +/- sqrt(disc)) / 2a cancels
// catastrophically when b^2 >> 4ac -- a ray passing far from a small
// sphere, not a corner case -- so this takes the numerically stable root
// first and derives the other from the root product c/q.
inline GMAN_EXPORT int GMANQuadraticRoots(RtFloat a, RtFloat b, RtFloat c, RtFloat& t0, RtFloat& t1) {
  if (a == 0.0)
    return 0;

  RtFloat disc = b * b - 4.0 * a * c;
  if (disc < 0.0)
    return 0;
  if (disc == 0.0) {
    t0 = -b / (2.0 * a);
    return 1;
  }

  RtFloat sqrtDisc = (RtFloat)sqrt(disc);
  RtFloat sign = b < 0.0 ? -1.0 : 1.0;
  RtFloat q = -(b + sign * sqrtDisc) / 2.0;

  RtFloat r0 = q / a;
  RtFloat r1 = c / q;
  t0 = GMANMin(r0, r1);
  t1 = GMANMax(r0, r1);
  return 2;
}

#endif
