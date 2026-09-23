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
 * R8 proof, §8 B: GMANReflect, GMANRefract and GMANFresnel at known
 * angles. Every direction below follows GMANRefract's own calling
 * convention -- i the incident direction, n oriented against it
 * (i.dot(n) <= 0) -- since that is what GMANFresnel's reflectance fix
 * assumes too.
 */

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
  // The exact TIR boundary float (§1): cosphi = 0.745355964, eta = 1.5,
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
// steps, both entering (eta < 1) and exiting (eta > 1).
void checkFresnelNoNaNAcrossSweep() {
  GMANSurfaceEnv env;
  GMANVector const n(0.0f, 0.0f, 1.0f);
  RtFloat const etas[2] = {(RtFloat)(1.0 / 1.5), (RtFloat)1.5};
  int badCount = 0;
  int sampleCount = 0;
  for (RtFloat eta : etas) {
    for (RtFloat degrees = 0.0f; degrees <= 90.0f; degrees += 0.5f) {
      RtFloat kr = 0.0f, kt = 0.0f;
      env.fresnel(obliqueDirection(degrees), n, eta, kr, kt);
      ++sampleCount;
      if (!std::isfinite(kr) || !std::isfinite(kt)) {
        ++badCount;
      }
    }
  }
  check(sampleCount > 300, "fresnel sweep: enough samples were gathered to trust a zero count");
  check(badCount == 0, "fresnel sweep: 0-90 degrees in 0.5-degree steps, entering and exiting, produced no NaN/Inf (" +
                           std::to_string(badCount) + "/" + std::to_string(sampleCount) + " bad)");
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

    GMANVector const wantR = GMANReflect(pin.i, pin.n);
    check(vectorNear(r, wantR, kTol), pin.label + ": six-arg r matches GMANReflect(i, n)");

    if (pin.tir) {
      check(t.getX() == 0.0f && t.getY() == 0.0f && t.getZ() == 0.0f,
            pin.label + ": six-arg t is exactly the zero vector at TIR");
    } else {
      GMANVector const wantT = GMANRefract(pin.i, pin.n, pin.eta);
      check(vectorNear(t, wantT, kTol), pin.label + ": six-arg t matches GMANRefract(i, n, eta)");
    }
  }
}

} // namespace

int main() {
  checkFresnelPins();
  checkFresnelNoNaNAcrossSweep();
  checkFresnelSixArgParity();

  return checkSummary("GMANFresnel: reflectance fixed at seven pins, no NaN/Inf across a full sweep, six-argument "
                      "overload agrees with the four-argument one");
}
