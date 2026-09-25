/* SPDX-License-Identifier: LGPL-2.1-or-later
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

/*
 * GMANReflect, GMANRefract and GMANFresnel at known
 * angles. Every direction below follows GMANRefract's own calling
 * convention -- i the incident direction, n oriented against it
 * (i.dot(n) <= 0) -- since that is what GMANFresnel's reflectance fix
 * assumes too.
 */

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "check.h"
#include "gmanshaderenvironment.h"
#include "gmanslapi.h"
#include "gmanvector.h"
#include "ri.h"

namespace {

constexpr RtFloat kTol = (RtFloat)1.0e-4;
constexpr RtFloat kDegToRad = (RtFloat)(3.14159265358979323846 / 180.0);

bool near(RtFloat a, RtFloat b, RtFloat tol) { return std::fabs(a - b) <= tol; }

bool vectorNear(GMANVector const& a, GMANVector const& b, RtFloat tol) {
  return near(a.getX(), b.getX(), tol) && near(a.getY(), b.getY(), tol) && near(a.getZ(), b.getZ(), tol);
}

// An independent reference for GMANFresnel's reflectance: the standard
// unpolarized Fresnel equations in terms of the two media's index ratio
// eta = n1/n2, not the tan/sin angle-sum-difference identities
// GMANFresnel itself uses. cosI is the incidence angle's own cosine,
// non-negative.
RtFloat closedFormKr(RtFloat cosI, RtFloat eta) {
  RtFloat const sinI = std::sqrt(std::max((RtFloat)0.0, 1 - cosI * cosI));
  RtFloat const sinT = sinI * eta;
  if (sinT >= 1.0f) {
    return 1.0f; // total internal reflection
  }
  RtFloat const cosT = std::sqrt(1 - sinT * sinT);
  RtFloat const rs = (eta * cosI - cosT) / (eta * cosI + cosT);
  RtFloat const rp = (eta * cosT - cosI) / (eta * cosT + cosI);
  return (RtFloat)0.5 * (rs * rs + rp * rp);
}

// Candidates for check 7's first pin: each normalizes to a v whose
// magnitude, squared, can round a ulp past 1 in float32 -- the exact
// amount depends on the platform's own floating-point contraction (FMA
// fuses a multiply and an add into one rounding step, changing the last
// ulp), so no single candidate is guaranteed to round past 1 on every
// compiler. clang contracts to FMA by default on arm64 but not x86-64;
// gcc never contracts without -ffast-math. Kept deliberately redundant
// -- several candidates land past 1 under either contraction mode, so a
// compiler this list was not tested against still has more than one
// chance.
struct ClampPinCandidate {
  RtFloat x, y, z;
};

constexpr ClampPinCandidate kClampPinCandidates[] = {
    {0.003f, 1.11f, 1.039f},  {0.7f, 0.7f, 0.14142f}, {0.001f, 0.999f, 0.0447f},
    {0.0173f, 0.2f, 0.9797f}, {0.6f, 0.8f, 0.0003f},  {1.0f, 0.003f, 0.007f},
};

// The first candidate whose normalized v, with n = -v, has
// fabs(v.dot(n)) > 1.0f on the running platform -- a bounded,
// deterministic search, never a randomized one, so a failure reproduces
// identically on every run. Returns false, leaving v/n unset, only if
// every candidate rounds to exactly 1 on this platform.
bool findClampPinPastOne(GMANVector& v, GMANVector& n) {
  for (ClampPinCandidate const& c : kClampPinCandidates) {
    GMANVector candidate(c.x, c.y, c.z);
    candidate.normalize();
    GMANVector const negated(-candidate.getX(), -candidate.getY(), -candidate.getZ());
    if (std::fabs(candidate.dot(negated)) > 1.0f) {
      v = candidate;
      n = negated;
      return true;
    }
  }
  return false;
}

// A unit direction in the x-z plane, degrees off -N (N = (0,0,1)):
// (sin(deg), 0, -cos(deg)). Its dot with N is -cos(deg), so this is
// entering at incidence angle deg under the i.dot(n) <= 0 convention.
GMANVector obliqueDirection(RtFloat degrees) {
  RtFloat const radians = degrees * kDegToRad;
  return GMANVector(std::sin(radians), (RtFloat)0.0, -std::cos(radians));
}

// One of check 4's seven pins, reused by check 6's six-argument parity
// pass over the same directions.
struct FresnelPin {
  std::string label;
  GMANVector i;
  GMANVector n;
  RtFloat eta;
  RtFloat wantKr;
  bool exact; // grazing and both TIR pins: kr == 1, kt == 0 exactly
  bool tir;   // both TIR pins: t is exactly the zero vector
};

std::vector<FresnelPin> const& fresnelPins() {
  static RtFloat const kIor = (RtFloat)1.5;
  static RtFloat const kEtaEnter = (RtFloat)1.0 / kIor;
  static RtFloat const kEtaExit = kIor;
  static GMANVector const kN(0.0f, 0.0f, 1.0f);
  // The exact TIR boundary float: cosphi = 0.745355964, eta = 1.5,
  // where sinphi*eta rounds to exactly 1.0f in RtFloat (32-bit).
  static RtFloat const kBoundaryCosPhi = 0.745355964f;
  static GMANVector const kBoundaryI(std::sqrt(1.0f - kBoundaryCosPhi * kBoundaryCosPhi), 0.0f, -kBoundaryCosPhi);

  static std::vector<FresnelPin> const pins = {
      {"normal incidence, entering", obliqueDirection(0.0f), kN, kEtaEnter, 0.04f, false, false},
      {"30 degrees, entering", obliqueDirection(30.0f), kN, kEtaEnter, 0.0415226f, false, false},
      {"80 degrees, entering", obliqueDirection(80.0f), kN, kEtaEnter, 0.387704f, false, false},
      {"grazing incidence, entering", GMANVector(1.0f, 0.0f, 0.0f), kN, kEtaEnter, 1.0f, true, false},
      {"30 degrees from the internal normal, exiting", obliqueDirection(30.0f), kN, kEtaExit, 0.0551902f, false, false},
      {"total internal reflection, exiting", obliqueDirection(60.0f), kN, kEtaExit, 1.0f, true, true},
      {"the exact TIR boundary float, exiting", kBoundaryI, kN, kEtaExit, 1.0f, true, true},
  };
  return pins;
}

// Check 1: reflect() at normal incidence (I anti-parallel to N) returns
// -I, and at 45 degrees returns the hand-computed mirrored direction, not
// merely one at the right angle to N.
void checkReflectPins() {
  GMANSurfaceEnv env;
  GMANVector const n(0.0f, 0.0f, 1.0f);

  GMANVector const iNormal(0.0f, 0.0f, -1.0f);
  GMANVector const gotNormal = env.reflect(iNormal, n);
  GMANVector const wantNormal = -iNormal;
  check(vectorNear(gotNormal, wantNormal, kTol), "reflect: normal incidence returns -I");

  // I at 45 degrees off -N in the x-z plane; the hand-computed mirror of
  // (sin45, 0, -cos45) about n = (0,0,1) is (sin45, 0, cos45) -- the x
  // component unchanged, the z component flipped.
  RtFloat const s45 = (RtFloat)(std::sqrt(2.0) / 2.0);
  GMANVector const i45(s45, 0.0f, -s45);
  GMANVector const got45 = env.reflect(i45, n);
  GMANVector const want45(s45, 0.0f, s45);
  check(vectorNear(got45, want45, kTol), "reflect: 45 degrees returns the hand-computed mirrored direction");
}

// Check 2: refract() at a known Snell's-law case -- ior = 1.5, an
// incidence angle short of the critical angle -- lands at the expected
// refraction angle off -N.
void checkRefractSnellCase() {
  GMANSurfaceEnv env;
  GMANVector const n(0.0f, 0.0f, 1.0f);
  RtFloat const ior = 1.5f;
  RtFloat const incidenceDegrees = 30.0f;

  GMANVector const i = obliqueDirection(incidenceDegrees);
  GMANVector const refracted = env.refract(i, n, 1.0f / ior);

  RtFloat const wantAngleRad = std::asin(std::sin(incidenceDegrees * kDegToRad) / ior);
  GMANVector const negN(-n.getX(), -n.getY(), -n.getZ());
  RtFloat const cosGotAngle = refracted.dot(negN);
  RtFloat const gotAngleRad = std::acos(cosGotAngle);
  check(near(gotAngleRad, wantAngleRad, (RtFloat)1.0e-3),
        "refract: a known Snell's-law case lands at the expected angle off -N");
}

// Check 3: refract() past the critical angle (exiting glass into air)
// returns exactly the zero vector, not merely a small one.
void checkRefractTotalInternalReflection() {
  GMANSurfaceEnv env;
  GMANVector const n(0.0f, 0.0f, 1.0f);
  RtFloat const ior = 1.5f;
  RtFloat const incidenceDegrees = 60.0f; // past asin(1/1.5) =~ 41.81 degrees

  GMANVector const i = obliqueDirection(incidenceDegrees);
  GMANVector const refracted = env.refract(i, n, ior);
  check(refracted.getX() == 0.0f && refracted.getY() == 0.0f && refracted.getZ() == 0.0f,
        "refract: past the critical angle returns exactly the zero vector");
}

// Check 4: GMANSurfaceEnv::fresnel at the seven pins above, kt == 1 - kr
// throughout and kr/kt exactly 1/0 at the three guarded cases.
void checkFresnelPins() {
  GMANSurfaceEnv env;
  for (FresnelPin const& pin : fresnelPins()) {
    RtFloat kr = 0.0f, kt = 0.0f;
    env.fresnel(pin.i, pin.n, pin.eta, kr, kt);
    check(kt == 1.0f - kr, pin.label + ": kt == 1 - kr exactly");
    if (pin.exact) {
      check(kr == pin.wantKr, pin.label + ": kr is exactly " + std::to_string(pin.wantKr));
      check(kt == 0.0f, pin.label + ": kt is exactly 0");
    } else {
      check(near(kr, pin.wantKr, kTol),
            pin.label + ": kr is " + std::to_string(pin.wantKr) + " within tolerance (got " + std::to_string(kr) + ")");
    }
  }
}

// Check 5: no NaN or Inf at any angle from 0 to 90 degrees, in 0.5-degree
// steps, both entering (eta < 1) and exiting (eta > 1), and kr matches
// the independent closed-form reference within 1e-4 throughout -- tight
// enough that raising kFresnelGuardTol far past its own 1e-5 (0.02, well
// short of the guard's own grazing regime) fails somewhere in the sweep
// rather than passing unnoticed.
void checkFresnelNoNaNAcrossSweep() {
  GMANSurfaceEnv env;
  GMANVector const n(0.0f, 0.0f, 1.0f);
  RtFloat const etas[2] = {(RtFloat)(1.0 / 1.5), (RtFloat)1.5};
  constexpr RtFloat kSweepTol = (RtFloat)1.0e-4;
  int badCount = 0;
  int sampleCount = 0;
  int mismatchCount = 0;
  for (RtFloat eta : etas) {
    for (RtFloat degrees = 0.0f; degrees <= 90.0f; degrees += 0.5f) {
      RtFloat kr = 0.0f, kt = 0.0f;
      env.fresnel(obliqueDirection(degrees), n, eta, kr, kt);
      ++sampleCount;
      if (!std::isfinite(kr) || !std::isfinite(kt)) {
        ++badCount;
        continue;
      }
      RtFloat const cosI = std::cos(degrees * kDegToRad);
      if (!near(kr, closedFormKr(cosI, eta), kSweepTol)) {
        ++mismatchCount;
      }
    }
  }
  check(sampleCount > 300, "fresnel sweep: enough samples were gathered to trust a zero count");
  check(badCount == 0, "fresnel sweep: 0-90 degrees in 0.5-degree steps, entering and exiting, produced no NaN/Inf (" +
                           std::to_string(badCount) + "/" + std::to_string(sampleCount) + " bad)");
  check(mismatchCount == 0, "fresnel sweep: kr matches the closed-form reference within 1e-4 throughout (" +
                                std::to_string(mismatchCount) + "/" + std::to_string(sampleCount) + " mismatched)");
}

// Check 6: the six-argument GMANFresnel overload, called directly (no
// GMANSurfaceEnv wrapper exists for it), at each of check 4's seven pins.
void checkFresnelSixArgParity() {
  for (FresnelPin const& pin : fresnelPins()) {
    RtFloat kr4 = 0.0f, kt4 = 0.0f;
    GMANFresnel(pin.i, pin.n, pin.eta, kr4, kt4);

    RtFloat kr6 = 0.0f, kt6 = 0.0f;
    GMANVector r, t;
    GMANFresnel(pin.i, pin.n, pin.eta, kr6, kt6, r, t);

    check(near(kr6, kr4, kTol), pin.label + ": six-arg kr agrees with the four-arg overload");
    check(near(kt6, kt4, kTol), pin.label + ": six-arg kt agrees with the four-arg overload");
    if (pin.exact) {
      check(kr6 == pin.wantKr, pin.label + ": six-arg kr is exactly " + std::to_string(pin.wantKr));
    } else {
      check(near(kr6, pin.wantKr, kTol), pin.label + ": six-arg kr matches its own pinned value");
    }

    // Exact, not within tolerance: the guarded branches call
    // GMANReflect(i, n) directly, and the general path's inline
    // r = i - n*cosphi*2 is the identical expression on the identical
    // operands GMANReflect itself computes, in the same order.
    GMANVector const wantR = GMANReflect(pin.i, pin.n);
    check(r.getX() == wantR.getX() && r.getY() == wantR.getY() && r.getZ() == wantR.getZ(),
          pin.label + ": six-arg r matches GMANReflect(i, n) exactly");

    if (pin.tir) {
      check(t.getX() == 0.0f && t.getY() == 0.0f && t.getZ() == 0.0f,
            pin.label + ": six-arg t is exactly the zero vector at TIR");
    } else {
      GMANVector const wantT = GMANRefract(pin.i, pin.n, pin.eta);
      check(vectorNear(t, wantT, kTol), pin.label + ": six-arg t matches GMANRefract(i, n, eta)");
    }
  }
}

// Check 7: GMANFresnel's incidence-cosine clamp holds at two pairs its
// unclamped formula could not. The first is an anti-parallel,
// normal-incidence pair (v normalized, n = -v) picked at run time by
// findClampPinPastOne() from kClampPinCandidates: whichever candidate's
// incidence cosine rounds a single ulp past 1 in float32 on this
// platform, which drives 1 - incidenceCosine^2 negative before the
// clamp, producing kr = kt = NaN past every guard; clamped, it lands at
// the same normal-incidence value every other normal-incidence pin
// reaches. A second pair with i.dot(n) > 0 -- outside GMANRefract's own
// calling convention, but not undefined behaviour -- exercised the
// unclamped formula's sign-dependent near-cancellation in
// sinapb/tanapb; clamped, it lands at its own closed-form value.
void checkFresnelClampGuards() {
  GMANSurfaceEnv env;

  GMANVector v;
  GMANVector n;
  bool const foundPastOne = findClampPinPastOne(v, n);
  check(foundPastOne,
        "clamp: the candidate search found a pair whose i.dot(n) genuinely rounds past 1 in float32 on this "
        "platform");
  if (!foundPastOne) {
    return;
  }
  RtFloat kr = 0.0f, kt = 0.0f;
  env.fresnel(v, n, 1.5f, kr, kt);
  check(std::isfinite(kr) && std::isfinite(kt), "clamp: the 1-ulp-over normalized pair produces no NaN");
  check(near(kr, 0.04f, kTol), "clamp: the 1-ulp-over normalized pair lands at the normal-incidence kr");

  GMANVector i3(0.6f, 0.0f, 0.8f);
  i3.normalize();
  GMANVector n3(0.5f, 0.0f, 0.866025f);
  n3.normalize();
  check(i3.dot(n3) > 0.0f, "clamp: the second pin is genuinely i.dot(n) > 0");
  RtFloat kr3 = 0.0f, kt3 = 0.0f;
  env.fresnel(i3, n3, 1.5f, kr3, kt3);
  // A range check ([0, 1]) passes for the wrong reason too: without the
  // fabs, a positive i.dot(n) clamps to a negative cosine, the grazing
  // guard fires, and kr = 1 -- in range, but not this pair's own value.
  check(near(kr3, closedFormKr(i3.dot(n3), 1.5f), kTol),
        "clamp: an i.dot(n) > 0 input lands at its own closed-form kr");
}

} // namespace

int main() {
  checkReflectPins();
  checkRefractSnellCase();
  checkRefractTotalInternalReflection();
  checkFresnelPins();
  checkFresnelNoNaNAcrossSweep();
  checkFresnelSixArgParity();
  checkFresnelClampGuards();

  return checkSummary("reflect()/refract() pinned at known angles; GMANFresnel's reflectance fixed at seven pins, "
                      "no NaN/Inf across a full sweep, six-argument overload agrees with the four-argument one, "
                      "and the incidence-cosine clamp holds at a 1-ulp-over pair and an i.dot(n) > 0 pair");
}
