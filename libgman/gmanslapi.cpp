/* SPDX-License-Identifier: LGPL-2.1-or-later */

/* This is part of GMAN, a RenderMan-compatible renderer.
 *
 * Copyright (c) 2001, 2000, 1999  John Cairns
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

#include <cmath>

#include "gmanlog.h"
#include "gmanmath.h"
#include "gmanmatrix4.h"
#include "gmanslapi.h"
#include "ri.h"

/*
 * RenderMan SL API
 *
 */

namespace {
// sin(phi) or |cos(phi)| below this is close enough to exactly zero that
// GMANFresnel's general reflectance formula divides by zero or reduces to
// 0/0; well under any angle step a fixture ray is meant to hit obliquely
// by, so a genuinely oblique hit never trips it by accident.
constexpr RtFloat kFresnelGuardTol = (RtFloat)1.0e-5;
} // namespace

// LJL - February 2001
RtFloat GMANSmoothStep(RtFloat min, RtFloat max, RtFloat value) {
  if (value < min)
    return 0.0;
  else if (value >= max)
    return 1.0;

  RtFloat u = (value - min) / (max - min);
  return (u * u * (3.0 - 2.0 * u));
}

// LJL - March 2001
// distance, ptlined, rotate, faceforward, reflect, refract, fresnel
RtFloat GMANDistance(GMANPoint const& p1, GMANPoint const& p2) {
  GMANVector res(p1, p2);
  return res.magnitude();
}

RtFloat GMANPTLined(const GMANPoint& p0, const GMANPoint& p1, const GMANPoint& q) {
  GMANVector p0q(p0, q);
  if (p0 == p1)
    return p0q.magnitude();

  RtFloat u;
  GMANVector p0p1(p0, p1);

  u = p0p1.dot(p0q) / p0p1.dot(p0p1);
  if (u < 0) {
    GMANVector res(p0, q);
    return res.magnitude();
  } else if (u > 1) {
    GMANVector res(p1, q);
    return res.magnitude();
  } else {
    GMANVector res(p1 + p0p1 * u, q);
    return res.magnitude();
  }
}

GMANPoint GMANRotate(const GMANPoint& q, RtFloat angle, const GMANPoint& p1, const GMANPoint& p2) {
  GMANMatrix4 m;
  GMANVector a(p1, p2);
  m.rot(angle, a.getX(), a.getY(), a.getZ());
  GMANVector b(p1, q);
  return p1 + b * m;
}

GMANVector GMANFaceForward(const GMANVector& n, const GMANVector& i, const GMANVector& nr) {
  if (nr.dot(i) < 0)
    return n;
  else
    return -n;
}

GMANVector GMANReflect(const GMANVector& i, const GMANVector& n) { return GMANVector(i - n * i.dot(n) * 2); }

GMANVector GMANRefract(const GMANVector& i, const GMANVector& n, RtFloat eta) {
  RtFloat cphi = i.dot(n);
  RtFloat k = 1 - eta * eta * (1 - cphi * cphi);
  if (k < 0) {
    return GMANVector(0, 0, 0);
  } else
    return (i * eta - n * (eta * cphi + sqrt(k)));
}

namespace {

// GMANFresnel's reflectance core, shared between its two overloads: the
// incidence-cosine clamp, the three numerical guards (normal incidence,
// grazing incidence, total internal reflection) and the general trig
// formula. cost is meaningful only when neither guarded nor tir: the
// general path's own r/t formulas need it, while the guarded and tir
// cases compute r/t through GMANReflect/GMANRefract instead and never
// read it. guarded covers both the normal- and grazing-incidence
// guards, whose r/t agree; tir is the one case needing its own t, the
// zero vector, rather than GMANRefract's.
struct FresnelReflectance {
  RtFloat kr;
  RtFloat kt;
  RtFloat cost;
  bool guarded;
  bool tir;
};

FresnelReflectance fresnelReflectance(RtFloat cosphi, RtFloat eta) {
  // The reflectance math below needs the incidence angle's own cosine:
  // non-negative, and clamped to 1 against the fp slop that can carry
  // fabs(i.dot(n)) a ulp past it for a grazing-normal pair, which would
  // otherwise leave 1 - incidenceCosine^2 negative and every guard below
  // false against a NaN sinphi.
  RtFloat const incidenceCosine = GMANMin(std::fabs(cosphi), (RtFloat)1.0);
  RtFloat const sinphi = std::sqrt(1 - incidenceCosine * incidenceCosine);

  if (sinphi <= kFresnelGuardTol) {
    // Normal incidence: sinamb/sinapb and tanamb/tanapb below are all 0/0.
    RtFloat const kr0 = (eta - 1) / (eta + 1);
    RtFloat const kr = kr0 * kr0;
    return {kr, 1 - kr, 0.0f, true, false};
  }
  if (incidenceCosine <= kFresnelGuardTol) {
    // Grazing incidence: tanphi below divides by zero.
    return {1.0f, 0.0f, 0.0f, true, false};
  }

  RtFloat const sint = sinphi * eta;
  if (sint >= 1) {
    // Total internal reflection: cost below is NaN.
    return {1.0f, 0.0f, 0.0f, false, true};
  }

  RtFloat const cost = std::sqrt(1 - sint * sint);
  RtFloat const sinapb = sinphi * cost + sint * incidenceCosine;
  RtFloat const sinamb = sinphi * cost - sint * incidenceCosine;
  RtFloat const tanphi = sinphi / incidenceCosine;
  RtFloat const tant = sint / cost;
  RtFloat const tanapb = (tanphi + tant) / (1 - tanphi * tant);
  RtFloat const tanamb = (tanphi - tant) / (1 + tanphi * tant);
  RtFloat const kr = 0.5 * ((sinamb * sinamb) / (sinapb * sinapb) + (tanamb * tanamb) / (tanapb * tanapb));
  return {kr, 1 - kr, cost, false, false};
}

} // namespace

RtVoid GMANFresnel(const GMANVector& i, const GMANVector& n, RtFloat eta, RtFloat& kr, RtFloat& kt) {
  FresnelReflectance const refl = fresnelReflectance(i.dot(n), eta);
  kr = refl.kr;
  kt = refl.kt;
}

RtVoid GMANFresnel(const GMANVector& i, const GMANVector& n, RtFloat eta, RtFloat& kr, RtFloat& kt, GMANVector& r,
                   GMANVector& t) {
  RtFloat const cosphi = i.dot(n);
  FresnelReflectance const refl = fresnelReflectance(cosphi, eta);
  kr = refl.kr;
  kt = refl.kt;

  if (refl.tir) {
    r = GMANReflect(i, n);
    t = GMANVector(0, 0, 0);
    return;
  }
  if (refl.guarded) {
    r = GMANReflect(i, n);
    t = GMANRefract(i, n, eta);
    return;
  }

  // The unchanged r/t formulas, kept inline rather than calling
  // GMANReflect/GMANRefract: a different floating-point path than the
  // guarded cases take, reaching the same value.
  r = (i - n * cosphi * 2);
  t = (i * eta - n * (eta * cosphi + refl.cost));
}
