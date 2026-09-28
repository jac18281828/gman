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
 * gman::BSDF's two delta lobes: a double-precision reference for exact
 * Fresnel, reflection and Snell refraction; direction, Fresnel, energy and
 * reciprocity for the mirror and the dielectric alone; a three-lobe
 * mixture's selection, histogram and weighted-estimator statistics;
 * clamping, eta bounds, capacity and failed draws; shadowTransmittance;
 * and no heap allocation.
 *
 * Every statistical check draws i.i.d. samples,
 * unitFloat(sampleHash(kSeed, 0, 0, i, dimension)) with a distinct
 * dimension per random number, so its 5-sigma bound uses the real standard
 * error. raybvhallocator.cpp's counting operator new brackets the
 * allocation check, as bsdf_test.cpp's does.
 */

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

#include "check.h"
#include "gmanbsdf.h"
#include "gmancolor.h"
#include "gmanerror.h"
#include "gmansampling.h"
#include "gmanvector.h"
#include "ri.h"
#include "samplingstats.h"

#if defined(__SANITIZE_ADDRESS__)
#define GMAN_ADDRESS_SANITIZED 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define GMAN_ADDRESS_SANITIZED 1
#endif
#endif
#ifndef GMAN_ADDRESS_SANITIZED
#define GMAN_ADDRESS_SANITIZED 0
#endif

// Defined in raybvhallocator.cpp, alongside the replacement operator
// new/delete it counts.
extern long raybvhAllocationCount();

namespace {

constexpr std::uint32_t kSeed = 20260927u;
constexpr double kPiD = 3.14159265358979323846;

constexpr std::uint32_t kMirrorDraws = 1u << 12;
constexpr std::uint32_t kDielectricDraws = 1u << 16;
constexpr std::uint32_t kSphereDraws = 1u << 12;
constexpr std::uint32_t kReciprocityDraws = 1u << 12;
constexpr std::uint32_t kMixtureDraws = 1u << 16;
constexpr std::uint32_t kEtaOneDraws = 256u;
constexpr std::uint32_t kTangentDraws = 256u;
constexpr int kNoAllocCalls = 1000;

constexpr int kBins = 8;
constexpr int kMassPoints = 8;

constexpr double kReferenceTol = 1e-7;
constexpr double kDirTol = 1e-5;
constexpr double kPdfExactTol = 1e-6;
constexpr double kWeightRelTol = 1e-5;
constexpr double kFAbsTol = 1e-5;
constexpr double kEnergyRatioRelTol = 1e-5;
constexpr double kTightRelTol = 1e-6;
constexpr double kRelTol = 1e-6;
constexpr double kAbsTol = 1e-7;
constexpr double kMassSumTol = 1e-4;
constexpr double kEnergyFloor = 1e-4;
constexpr double kFractionFloor = 1e-4;
constexpr double kDeltaPdfTol = 1e-5;
constexpr double kShadowAbsTol = 1e-5;
constexpr double kShadowTightTol = 1e-6;
constexpr double kMinSuccessFraction = 0.9999;
constexpr double kMinBinCount = 20.0;

constexpr RtFloat kEta = 1.5f;

constexpr double kMirrorAnglesDeg[] = {0.0, 60.0, 89.0, 120.0};
constexpr std::size_t kMirrorAngleCount = sizeof(kMirrorAnglesDeg) / sizeof(kMirrorAnglesDeg[0]);
constexpr double kDielectricAnglesDeg[] = {0.0, 45.0, 60.0, 89.0, 120.0, 139.0, 150.0, 180.0};
constexpr std::size_t kDielectricAngleCount = sizeof(kDielectricAnglesDeg) / sizeof(kDielectricAnglesDeg[0]);
constexpr double kTirAngleDeg = 120.0;
// Refracting back from this angle's transmission direction lands 9.1e-5
// away in cosine from the critical angle, cos(theta_t) 0.74544680 against
// 0.74535599; F's slope there, dF/d(cosine) about -498 against -1.2 at 60
// degrees: one float ulp of cosine (about 6e-8) moves F by about 3e-5
// there, past the reciprocity check's 1e-5 tolerance. checkDielectric
// excludes it, as it excludes kTirAngleDeg, which never transmits.
constexpr double kNearCriticalAngleDeg = 89.0;

// Dimension bases, one block per statistical draw so no two random numbers
// in this file share one.
constexpr std::uint32_t kDimMirrorMain = 0u;          // 4 angles * 2
constexpr std::uint32_t kDimMirrorSphere = 16u;       // 4 angles * 2
constexpr std::uint32_t kDimDielectricMain = 32u;     // 8 angles * 2
constexpr std::uint32_t kDimDielectricSphere = 64u;   // 8 angles * 2
constexpr std::uint32_t kDimReciprocityBack = 96u;    // 6 angles * 2
constexpr std::uint32_t kDimMixtureMain = 128u;       // 2 angles * 2
constexpr std::uint32_t kDimEtaOne = 144u;            // 2
constexpr std::uint32_t kDimTangentMirror = 160u;     // 2
constexpr std::uint32_t kDimTangentDielectric = 164u; // 2
constexpr std::uint32_t kDimNoAlloc = 180u;           // 2

GMANVector const kN0Unnormalized(0.3f, -0.5f, 0.8f);
GMANColor const kR(0.8f, 0.5f, 0.2f);
GMANColor const kWhite(1.0f, 1.0f, 1.0f);
GMANColor const kBlack(0.0f, 0.0f, 0.0f);
GMANColor const kOutOfRangeWeight(1.4f, 0.2f, -0.3f);
GMANColor const kOutOfRangeClamped(1.0f, 0.2f, 0.0f);
GMANVector const kAxisNormal(0.0f, 0.0f, 1.0f);
GMANVector const kTangentWo(1.0f, 0.0f, 0.0f);

GMANColor const kMixtureLambert(0.3f, 0.2f, 0.1f);
GMANColor const kMixtureMirror(0.1f, 0.2f, 0.3f);
GMANColor const kMixtureDielectric(0.6f, 0.6f, 0.6f);
constexpr double kMixtureAnglesDeg[] = {60.0, 150.0};
constexpr double kMixtureExpectedMean[] = {0.6963956, 1.7086074};

// A fixed pair of draws for a check whose closure ignores them entirely (a
// mirror lobe's deterministic direction): no statistical claim is made, so
// no sampleHash dimension is spent on it.
constexpr RtFloat kFixedU1 = 0.37f;
constexpr RtFloat kFixedU2 = 0.61f;

RtFloat uniform(std::uint32_t index, std::uint32_t dimension) {
  return gman::unitFloat(gman::sampleHash(kSeed, 0, 0, index, dimension));
}

GMANVector normalized(GMANVector v) {
  v.normalize();
  return v;
}

GMANVector n0() { return normalized(kN0Unnormalized); }

// tangentFrame(N0).toWorld((sin(theta), 0, cos(theta))).
GMANVector woAt(double degrees) {
  double const theta = degrees * kPiD / 180.0;
  GMANVector const local(static_cast<RtFloat>(std::sin(theta)), 0.0f, static_cast<RtFloat>(std::cos(theta)));
  return gman::tangentFrame(n0()).toWorld(local);
}

// N0 flipped to wo's side.
GMANVector woNormal(GMANVector const& wo) {
  GMANVector const n = n0();
  return n.dot(wo) < 0.0f ? -n : n;
}

std::string angleName(double degrees) { return "wo at " + std::to_string(static_cast<int>(degrees)) + " degrees"; }

double channelD(GMANColor const& c, int i) {
  if (i == 0) {
    return c.getRed();
  }
  return i == 1 ? c.getGreen() : c.getBlue();
}

GMANColor scaled(GMANColor c, double s) {
  c.scale(static_cast<RtFloat>(s));
  return c;
}

GMANColor summed(GMANColor a, GMANColor const& b) {
  a += b;
  return a;
}

// c = f * |cos(theta_i)|, a draw's branch coefficient.
GMANColor drawC(GMANColor const& f, RtFloat cosThetaI) { return scaled(f, std::fabs(static_cast<double>(cosThetaI))); }

bool nearRel(double value, double expected, double rel) {
  return std::fabs(value - expected) <= rel * std::max(std::fabs(value), std::fabs(expected));
}

bool nearRelOrAbs(double value, double expected, double rel, double abs) {
  return nearRel(value, expected, rel) || std::fabs(value - expected) <= abs;
}

bool colorNearRelOrAbs(GMANColor const& a, GMANColor const& b, double rel, double abs) {
  for (int c = 0; c < 3; ++c) {
    if (!nearRelOrAbs(channelD(a, c), channelD(b, c), rel, abs)) {
      return false;
    }
  }
  return true;
}

bool colorNearRel(GMANColor const& a, GMANColor const& b, double rel) {
  for (int c = 0; c < 3; ++c) {
    if (!nearRel(channelD(a, c), channelD(b, c), rel)) {
      return false;
    }
  }
  return true;
}

// Every channel of a near a single scalar, relatively.
bool colorNearRelScalar(GMANColor const& a, double expected, double rel) {
  for (int c = 0; c < 3; ++c) {
    if (!nearRel(channelD(a, c), expected, rel)) {
      return false;
    }
  }
  return true;
}

bool colorNearAbs(GMANColor const& a, GMANColor const& b, double abs) {
  for (int c = 0; c < 3; ++c) {
    if (std::fabs(channelD(a, c) - channelD(b, c)) > abs) {
      return false;
    }
  }
  return true;
}

bool colorExactly(GMANColor const& a, GMANColor const& b) {
  return a.getRed() == b.getRed() && a.getGreen() == b.getGreen() && a.getBlue() == b.getBlue();
}

gman::BSDF buildMirror(GMANColor const& weight) {
  gman::BSDF bsdf(n0());
  bsdf.addMirror(weight);
  return bsdf;
}

gman::BSDF buildDielectric(GMANColor const& weight, RtFloat eta) {
  gman::BSDF bsdf(n0());
  bsdf.addDielectric(weight, eta);
  return bsdf;
}

gman::BSDF buildMixture() {
  gman::BSDF bsdf(n0());
  bsdf.addLambert(kMixtureLambert);
  bsdf.addMirror(kMixtureMirror);
  bsdf.addDielectric(kMixtureDielectric, kEta);
  return bsdf;
}

// ---- A double-precision vector and Fresnel/Snell reference, the test's
// own, apart from libgman. ----

struct Vec3 {
  double x, y, z;
};

Vec3 vAdd(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 vSub(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 vScale(Vec3 a, double s) { return {a.x * s, a.y * s, a.z * s}; }
Vec3 vNeg(Vec3 a) { return {-a.x, -a.y, -a.z}; }
double vDot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3 toVec3(GMANVector const& v) { return {v.getX(), v.getY(), v.getZ()}; }

bool vectorNear(GMANVector const& a, Vec3 const& b, double tol) {
  return std::fabs(static_cast<double>(a.getX()) - b.x) <= tol &&
         std::fabs(static_cast<double>(a.getY()) - b.y) <= tol && std::fabs(static_cast<double>(a.getZ()) - b.z) <= tol;
}

// wo reflected about n: invariant to n's sign.
Vec3 refReflect(Vec3 const& n, Vec3 const& wo) {
  double const cosThetaO = vDot(n, wo);
  return vSub(vScale(n, 2.0 * cosThetaO), wo);
}

// Which side of normal is index 1 and which is eta, the normal oriented
// onto wo's side, and wo's cosine against it.
struct RefGeometry {
  double etaO;
  double etaI;
  Vec3 nOriented;
  double cosThetaO;
};

RefGeometry refGeometry(Vec3 const& normal, Vec3 const& wo, double eta) {
  double const cosWoN = vDot(normal, wo);
  bool const entering = cosWoN > 0.0;
  return {entering ? 1.0 : eta, entering ? eta : 1.0, entering ? normal : vNeg(normal), std::fabs(cosWoN)};
}

// wo transmitted across nOriented's side at ratio = etaO / etaI, given
// Snell's own transmitted cosine.
Vec3 refRefract(Vec3 const& nOriented, Vec3 const& wo, double ratio, double cosThetaO, double cosThetaT) {
  return vAdd(vScale(wo, -ratio), vScale(nOriented, ratio * cosThetaO - cosThetaT));
}

struct FRef {
  double f;
  double cosThetaT;
  bool tir;
};

// The exact unpolarized Fresnel reflectance and Snell's transmitted
// cosine, from the two sides' relative indices and wo's cosine against
// the normal oriented onto wo's side.
FRef refFresnelFromCos(double etaO, double etaI, double cosThetaO) {
  double const sinThetaO2 = std::max(0.0, 1.0 - cosThetaO * cosThetaO);
  double const ratio = etaO / etaI;
  double const sinThetaT2 = ratio * ratio * sinThetaO2;
  if (sinThetaT2 >= 1.0) {
    return {1.0, 0.0, true};
  }
  double const cosThetaT = std::sqrt(1.0 - sinThetaT2);
  double const rs = (etaO * cosThetaO - etaI * cosThetaT) / (etaO * cosThetaO + etaI * cosThetaT);
  double const rp = (etaI * cosThetaO - etaO * cosThetaT) / (etaI * cosThetaO + etaO * cosThetaT);
  return {0.5 * (rs * rs + rp * rp), cosThetaT, false};
}

// F at the exact double angle degrees from N0, eta the dielectric's
// relative index: checkReference's own self-check uses this apart from any
// float wo.
FRef refFAtAngle(double degrees, double eta) {
  double const theta = degrees * kPiD / 180.0;
  double const cosWoN = std::cos(theta);
  bool const entering = cosWoN > 0.0;
  double const etaO = entering ? 1.0 : eta;
  double const etaI = entering ? eta : 1.0;
  return refFresnelFromCos(etaO, etaI, std::fabs(cosWoN));
}

// ---- The reference's own pinned values. ----

void checkReference() {
  struct FCase {
    double angleDeg;
    double expectedF;
  };
  FCase const cases[] = {
      {0.0, 0.04},         {45.0, 0.05023991},  {60.0, 0.08918671}, {89.0, 0.90418495},
      {139.0, 0.37975127}, {150.0, 0.05519017}, {180.0, 0.04},
  };
  bool fOk = true;
  for (auto const& c : cases) {
    fOk = fOk && std::fabs(refFAtAngle(c.angleDeg, kEta).f - c.expectedF) <= kReferenceTol;
  }
  check(fOk, "reference: F at 0, 45, 60, 89, 139, 150 and 180 degrees matches the pinned values within 1e-7");

  FRef const tir = refFAtAngle(kTirAngleDeg, kEta);
  check(tir.tir && tir.f == 1.0, "reference: F at 120 degrees is total internal reflection, F exactly 1");

  FRef const enter41 = refFresnelFromCos(1.0, kEta, std::cos(41.0 * kPiD / 180.0));
  FRef const enter30 = refFresnelFromCos(1.0, kEta, std::cos(30.0 * kPiD / 180.0));
  check(std::fabs(enter41.f - 0.04646058) <= kReferenceTol,
        "reference: F entering from index 1 at 41 degrees incidence matches 0.04646058 within 1e-7");
  check(std::fabs(enter30.f - 0.04152263) <= kReferenceTol,
        "reference: F entering from index 1 at 30 degrees incidence matches 0.04152263 within 1e-7");

  double const scaleAt0 = 1.0 / (static_cast<double>(kEta) * static_cast<double>(kEta));
  double const scaleAt180 = static_cast<double>(kEta) * static_cast<double>(kEta);
  check(std::fabs(scaleAt0 - 0.44444444) <= kReferenceTol,
        "reference: the scale at 0 degrees is 0.44444444 within 1e-7");
  check(std::fabs(scaleAt180 - 2.25) <= kReferenceTol, "reference: the scale at 180 degrees is 2.25 within 1e-7");
  check(std::fabs((1.0 - 0.04) * scaleAt0 - 0.42666667) <= kReferenceTol,
        "reference: a white transmission's c at 0 degrees is 0.42666667 within 1e-7");
  check(std::fabs((1.0 - 0.04) * scaleAt180 - 2.16) <= kReferenceTol,
        "reference: a white transmission's c at 180 degrees is 2.16 within 1e-7");

  double const criticalAngleDeg = std::asin(1.0 / static_cast<double>(kEta)) * 180.0 / kPiD;
  check(std::fabs(criticalAngleDeg - 41.8103149) <= kReferenceTol,
        "reference: the critical angle inside is 41.8103149 degrees within 1e-7");
}

// ---- The mirror. ----

void checkMirror() {
  gman::BSDF const closure = buildMirror(kR);
  Vec3 const n0d = toVec3(n0());

  for (std::size_t k = 0; k < kMirrorAngleCount; ++k) {
    double const angle = kMirrorAnglesDeg[k];
    GMANVector const wo = woAt(angle);
    Vec3 const woD = toVec3(wo);
    Vec3 const reflectedD = refReflect(n0d, woD);
    std::string const name = "mirror, " + angleName(angle);
    std::uint32_t const dim = kDimMirrorMain + 2u * static_cast<std::uint32_t>(k);

    bool succOk = true, deltaOk = true, dirOk = true, pdfOk = true, cOk = true;
    std::vector<GMANVector> wis;
    wis.reserve(kMirrorDraws);
    for (std::uint32_t i = 0; i < kMirrorDraws; ++i) {
      gman::BSDFSample const s = closure.sample(wo, uniform(i, dim), uniform(i, dim + 1u));
      succOk = succOk && s.pdf > 0.0f;
      if (!(s.pdf > 0.0f)) {
        continue;
      }
      deltaOk = deltaOk && s.isDelta && s.lobeIndex == 0;
      dirOk = dirOk && vectorNear(s.wi, reflectedD, kDirTol);
      pdfOk = pdfOk && std::fabs(static_cast<double>(s.pdf) - 1.0) <= kPdfExactTol;
      cOk = cOk && colorNearRel(drawC(s.f, n0().dot(s.wi)), kR, kWeightRelTol);
      wis.push_back(s.wi);
    }
    check(succOk, name + ": every draw succeeds");
    check(deltaOk, name + ": every draw reports isDelta true and lobeIndex 0");
    check(dirOk, name + ": every draw's wi equals the reference reflection within 1e-5 per component");
    check(pdfOk, name + ": every draw's pdf is 1 within 1e-6");
    check(cOk, name + ": every draw's c equals R within 1e-5 relative per channel");

    bool ownOffOk = true;
    for (auto const& wi : wis) {
      ownOffOk = ownOffOk && colorExactly(closure.eval(wo, wi), kBlack) && closure.pdf(wo, wi) == 0.0f;
    }
    check(ownOffOk, name + ": eval is black and pdf is 0 at each draw's own wi");

    std::uint32_t const sphereDim = kDimMirrorSphere + 2u * static_cast<std::uint32_t>(k);
    bool sphereOffOk = true;
    for (std::uint32_t i = 0; i < kSphereDraws; ++i) {
      GMANVector const dir = gman::uniformSphere(uniform(i, sphereDim), uniform(i, sphereDim + 1u));
      sphereOffOk = sphereOffOk && colorExactly(closure.eval(wo, dir), kBlack) && closure.pdf(wo, dir) == 0.0f;
    }
    check(sphereOffOk, name + ": eval is black and pdf is 0 at 2^12 uniformSphere directions");
  }
}

// The mirror's own reciprocity: the draw from wi returns wo with the same c.
void checkMirrorReciprocity() {
  gman::BSDF const closure = buildMirror(kR);
  bool ok = true;
  for (std::size_t k = 0; k < kMirrorAngleCount; ++k) {
    GMANVector const wo = woAt(kMirrorAnglesDeg[k]);
    gman::BSDFSample const forward = closure.sample(wo, kFixedU1, kFixedU2);
    GMANColor const c1 = drawC(forward.f, n0().dot(forward.wi));
    gman::BSDFSample const backward = closure.sample(forward.wi, kFixedU1, kFixedU2);
    GMANColor const c2 = drawC(backward.f, n0().dot(backward.wi));
    ok = ok && vectorNear(backward.wi, toVec3(wo), kDirTol) && colorNearRel(c2, c1, kWeightRelTol);
  }
  check(ok, "reciprocity, mirror: at each of the mirror's own wo angles, the draw from wi returns wo within 1e-5 "
            "with the same c within 1e-5 relative");
}

// ---- The dielectric: direction, energy and reciprocity. ----

// What the energy and reciprocity checks need from a dielectric angle's
// main draws pass.
struct DielectricAngleResult {
  bool hasFirstReflected = false;
  GMANColor firstReflectedC{0.0f, 0.0f, 0.0f};
  bool hasFirstTransmitted = false;
  GMANVector firstTransmittedWi;
  GMANColor firstTransmittedC{0.0f, 0.0f, 0.0f};
};

// The dielectric's own checks for one angle, over kDielectricDraws draws of
// a white closure; returns what the energy and reciprocity checks need from
// those same draws.
DielectricAngleResult checkDielectricAngle(double angle, RefGeometry const& geo, FRef const& fr, double scale,
                                           std::size_t angleIndex) {
  GMANVector const wo = woAt(angle);
  RtFloat const cosThetaO = n0().dot(wo);
  std::string const name = "dielectric, " + angleName(angle);
  bool const pairedCheck = angle == 60.0;
  std::uint32_t const dim = kDimDielectricMain + 2u * static_cast<std::uint32_t>(angleIndex);
  std::uint32_t const sphereDim = kDimDielectricSphere + 2u * static_cast<std::uint32_t>(angleIndex);

  gman::BSDF const closure = buildDielectric(kWhite, kEta);
  gman::BSDF const closureR = buildDielectric(kR, kEta);
  Vec3 const n0d = toVec3(n0());
  Vec3 const woD = toVec3(wo);
  Vec3 const reflectedD = refReflect(n0d, woD);
  Vec3 const transmittedD =
      fr.tir ? Vec3{0.0, 0.0, 0.0} : refRefract(geo.nOriented, woD, geo.etaO / geo.etaI, geo.cosThetaO, fr.cosThetaT);

  DielectricAngleResult result;
  std::uint32_t successes = 0, reflected = 0;
  bool deltaOk = true;
  bool reflectDirOk = true, reflectPdfOk = true, reflectCOk = true;
  bool transmitDirOk = true, transmitPdfOk = true, transmitCOk = true;
  bool ownOffOk = true, energyRatioOk = true, pairedOk = true;

  for (std::uint32_t i = 0; i < kDielectricDraws; ++i) {
    RtFloat const u1 = uniform(i, dim);
    RtFloat const u2 = uniform(i, dim + 1u);
    gman::BSDFSample const s = closure.sample(wo, u1, u2);
    if (!(s.pdf > 0.0f)) {
      continue;
    }
    ++successes;
    deltaOk = deltaOk && s.isDelta && s.lobeIndex == 0;

    RtFloat const cosThetaI = n0().dot(s.wi);
    bool const isReflected = cosThetaO * cosThetaI > 0.0f;
    GMANColor const c = drawC(s.f, cosThetaI);
    double const drawScale = isReflected ? 1.0 : scale;
    for (int ch = 0; ch < 3; ++ch) {
      energyRatioOk =
          energyRatioOk && nearRel(channelD(c, ch) / static_cast<double>(s.pdf) / drawScale, 1.0, kEnergyRatioRelTol);
    }

    if (isReflected) {
      ++reflected;
      reflectDirOk = reflectDirOk && vectorNear(s.wi, reflectedD, kDirTol);
      reflectPdfOk = reflectPdfOk && std::fabs(static_cast<double>(s.pdf) - fr.f) <= kFAbsTol;
      reflectCOk = reflectCOk && colorNearRelScalar(c, fr.f, kWeightRelTol);
      if (!result.hasFirstReflected) {
        result.hasFirstReflected = true;
        result.firstReflectedC = c;
      }
    } else {
      transmitDirOk = transmitDirOk && !fr.tir && vectorNear(s.wi, transmittedD, kDirTol);
      transmitPdfOk = transmitPdfOk && std::fabs(static_cast<double>(s.pdf) - (1.0 - fr.f)) <= kFAbsTol;
      transmitCOk = transmitCOk && colorNearRelScalar(c, (1.0 - fr.f) * scale, kWeightRelTol);
      if (!result.hasFirstTransmitted) {
        result.hasFirstTransmitted = true;
        result.firstTransmittedWi = s.wi;
        result.firstTransmittedC = c;
      }
    }
    ownOffOk = ownOffOk && colorExactly(closure.eval(wo, s.wi), kBlack) && closure.pdf(wo, s.wi) == 0.0f;

    if (pairedCheck) {
      gman::BSDFSample const sR = closureR.sample(wo, u1, u2);
      GMANColor const cR = drawC(sR.f, n0().dot(sR.wi));
      GMANColor const expectedCR(static_cast<RtFloat>(channelD(c, 0) * kR.getRed()),
                                 static_cast<RtFloat>(channelD(c, 1) * kR.getGreen()),
                                 static_cast<RtFloat>(channelD(c, 2) * kR.getBlue()));
      pairedOk = pairedOk && sR.pdf > 0.0f && colorNearRel(cR, expectedCR, kWeightRelTol);
    }
  }

  check(successes == kDielectricDraws, name + ": every draw succeeds");
  check(deltaOk, name + ": every draw reports isDelta true and lobeIndex 0");
  check(reflectDirOk, name + ": every reflected draw's wi equals the reference reflection within 1e-5 per component");
  check(reflectPdfOk, name + ": every reflected draw's pdf equals F within 1e-5 absolute");
  check(reflectCOk, name + ": every reflected draw's c equals F within 1e-5 relative");
  check(transmitDirOk,
        name + ": every transmitted draw's wi equals the reference refraction within 1e-5 per component");
  check(transmitPdfOk, name + ": every transmitted draw's pdf equals 1 - F within 1e-5 absolute");
  check(transmitCOk, name + ": every transmitted draw's c equals (1 - F) times the scale within 1e-5 relative");
  check(ownOffOk, name + ": eval is black and pdf is 0 at each draw's own wi");
  check(energyRatioOk, name + ": every draw's c/pdf divided by its scale is 1 within 1e-5 relative");
  if (pairedCheck) {
    check(pairedOk, name + ": at weight R, every draw's c is the white closure's c times R within 1e-5 relative");
  }

  if (fr.tir) {
    check(reflected == successes && successes == kDielectricDraws,
          name + ": at total internal reflection, every draw reflects");
  } else {
    double const n = static_cast<double>(successes);
    double const fraction = static_cast<double>(reflected) / n;
    double const sigma = std::sqrt(fr.f * (1.0 - fr.f) / n);
    std::printf("info: %s: reflected fraction %.6f against F %.6f, sigma %.6f (%.3f sigma)\n", name.c_str(), fraction,
                fr.f, sigma, sigma > 0.0 ? std::fabs(fraction - fr.f) / sigma : 0.0);
    check(std::fabs(fraction - fr.f) <= std::max(5.0 * sigma, kFractionFloor),
          name + ": the reflected fraction of all draws is within 5sigma of F");
  }

  bool sphereOffOk = true;
  for (std::uint32_t i = 0; i < kSphereDraws; ++i) {
    GMANVector const dir = gman::uniformSphere(uniform(i, sphereDim), uniform(i, sphereDim + 1u));
    sphereOffOk = sphereOffOk && colorExactly(closure.eval(wo, dir), kBlack) && closure.pdf(wo, dir) == 0.0f;
  }
  check(sphereOffOk, name + ": eval is black and pdf is 0 at 2^12 uniformSphere directions");

  return result;
}

// Energy, from the same first-reflected/first-transmitted draws the
// dielectric's own checks captured.
void checkDielectricEnergy(DielectricAngleResult const& result, FRef const& fr, double scale, double angle) {
  std::string const name = "energy, dielectric, " + angleName(angle);
  if (fr.tir) {
    check(result.hasFirstReflected && colorNearAbs(result.firstReflectedC, kWhite, kWeightRelTol),
          name + ": at total internal reflection, the reflected c alone is 1 within 1e-5");
    return;
  }
  bool const bothOccurred = result.hasFirstReflected && result.hasFirstTransmitted;
  check(bothOccurred, name + ": both a reflected and a transmitted draw occur");
  if (!bothOccurred) {
    return;
  }
  GMANColor const transmittedOverScale = scaled(result.firstTransmittedC, 1.0 / scale);
  GMANColor const sum = summed(result.firstReflectedC, transmittedOverScale);
  check(colorNearAbs(sum, kWhite, kWeightRelTol),
        name + ": the first reflected draw's c plus the first transmitted draw's c divided by its scale is 1 "
               "within 1e-5");
}

// Reciprocity's dielectric half, skipped at 89 degrees (see
// checkDielectric): from the first transmitted draw's wi, 2^12 draws whose
// transmitted ones return wo and whose reflected ones report the forward
// reflected pdf, and Veach's generalized reciprocity on c and c'.
void checkDielectricReciprocity(DielectricAngleResult const& result, RefGeometry const& geo, FRef const& fr,
                                double angle, std::uint32_t dim) {
  std::string const name = "reciprocity, dielectric, " + angleName(angle);
  if (!result.hasFirstTransmitted) {
    check(false, name + ": a transmitted draw exists to test reciprocity from");
    return;
  }
  gman::BSDF const closure = buildDielectric(kWhite, kEta);
  GMANVector const wi = result.firstTransmittedWi;
  Vec3 const woRef = toVec3(woAt(angle));

  bool backDirOk = true, reflectPdfOk = true, foundBack = false;
  GMANColor cPrime(0.0f, 0.0f, 0.0f);
  for (std::uint32_t i = 0; i < kReciprocityDraws; ++i) {
    gman::BSDFSample const s = closure.sample(wi, uniform(i, dim), uniform(i, dim + 1u));
    if (!(s.pdf > 0.0f)) {
      continue;
    }
    RtFloat const cosThetaO2 = n0().dot(wi);
    RtFloat const cosThetaI2 = n0().dot(s.wi);
    if (cosThetaO2 * cosThetaI2 > 0.0f) {
      reflectPdfOk = reflectPdfOk && std::fabs(static_cast<double>(s.pdf) - fr.f) <= kFAbsTol;
    } else {
      backDirOk = backDirOk && vectorNear(s.wi, woRef, kDirTol);
      if (!foundBack) {
        foundBack = true;
        cPrime = drawC(s.f, cosThetaI2);
      }
    }
  }
  check(backDirOk, name + ": every transmitted draw from wi returns wo within 1e-5 per component");
  check(reflectPdfOk, name + ": every reflected draw from wi reports the forward reflected pdf within 1e-5");
  check(foundBack, name + ": a transmitted draw from wi occurs");
  if (!foundBack) {
    return;
  }
  double const lhsFactor = (geo.etaI / geo.etaO) * (geo.etaI / geo.etaO);
  double const rhsFactor = (geo.etaO / geo.etaI) * (geo.etaO / geo.etaI);
  bool reciprocityOk = true;
  for (int ch = 0; ch < 3; ++ch) {
    double const lhs = channelD(result.firstTransmittedC, ch) * lhsFactor;
    double const rhs = channelD(cPrime, ch) * rhsFactor;
    reciprocityOk = reciprocityOk && nearRel(lhs, rhs, kWeightRelTol);
  }
  check(reciprocityOk, name + ": c * (eta_i / eta_o)^2 equals c' * (eta_o / eta_i)^2 within 1e-5 relative");
}

void checkDielectric() {
  Vec3 const n0d = toVec3(n0());
  DielectricAngleResult results[kDielectricAngleCount];
  RefGeometry geos[kDielectricAngleCount];
  FRef frs[kDielectricAngleCount];
  double scales[kDielectricAngleCount];

  for (std::size_t k = 0; k < kDielectricAngleCount; ++k) {
    double const angle = kDielectricAnglesDeg[k];
    GMANVector const wo = woAt(angle);
    Vec3 const woD = toVec3(wo);
    RefGeometry const geo = refGeometry(n0d, woD, static_cast<double>(kEta));
    FRef const fr = refFresnelFromCos(geo.etaO, geo.etaI, geo.cosThetaO);
    double const ratio = geo.etaO / geo.etaI;
    double const scale = ratio * ratio;

    geos[k] = geo;
    frs[k] = fr;
    scales[k] = scale;
    results[k] = checkDielectricAngle(angle, geo, fr, scale, k);
  }

  for (std::size_t k = 0; k < kDielectricAngleCount; ++k) {
    checkDielectricEnergy(results[k], frs[k], scales[k], kDielectricAnglesDeg[k]);
  }

  std::uint32_t backCounter = 0;
  for (std::size_t k = 0; k < kDielectricAngleCount; ++k) {
    if (kDielectricAnglesDeg[k] == kTirAngleDeg || kDielectricAnglesDeg[k] == kNearCriticalAngleDeg) {
      continue;
    }
    std::uint32_t const dim = kDimReciprocityBack + 2u * backCounter;
    ++backCounter;
    checkDielectricReciprocity(results[k], geos[k], frs[k], kDielectricAnglesDeg[k], dim);
  }

  checkMirrorReciprocity();
}

// ---- The mixture. ----

// A midpoint-rule mass of the closure's own pdf, and the deviation of a
// real sample() run's counts from N * mass, over the 64 Lambert bins plus
// one pooled delta bin, bins below 20 expected pooled into one.
void checkMixtureAngle(double angle, std::uint32_t dim) {
  GMANVector const wo = woAt(angle);
  gman::BSDF const closure = buildMixture();
  RtFloat const cosThetaO = n0().dot(wo);
  std::string const name = "mixture, " + angleName(angle);

  Vec3 const n0d = toVec3(n0());
  Vec3 const woD = toVec3(wo);
  RefGeometry const geo = refGeometry(n0d, woD, static_cast<double>(kEta));
  FRef const fr = refFresnelFromCos(geo.etaO, geo.etaI, geo.cosThetaO);
  double const ratio = geo.etaO / geo.etaI;
  double const scale = ratio * ratio;

  check(colorExactly(closure.rhoD(), kMixtureLambert), name + ": rhoD is exactly (0.3, 0.2, 0.1)");
  check(closure.lobeCount() == 3, name + ": lobeCount is 3");
  GMANColor const expectedEval = scaled(kMixtureLambert, 1.0 / kPiD);
  check(colorNearRel(closure.eval(wo, wo), expectedEval, kTightRelTol),
        name + ": eval on wo's side is (0.3, 0.2, 0.1)/pi within 1e-6 relative");
  double const expectedPdf = 0.2 * std::fabs(static_cast<double>(cosThetaO)) / kPiD;
  check(nearRel(static_cast<double>(closure.pdf(wo, wo)), expectedPdf, kTightRelTol),
        name + ": pdf is 0.2 * |cos(theta_i)| / pi within 1e-6 relative");

  std::uint32_t successes = 0, lobe0 = 0, lobe1 = 0, lobe2Reflected = 0, lobe2Transmitted = 0, deltaSuccesses = 0;
  bool lambertReportOk = true, mirrorReportOk = true, dielectricReportOk = true;
  std::vector<GMANVector> lambertWis;
  std::vector<double> energyTerms[3];
  for (auto& t : energyTerms) {
    t.assign(kMixtureDraws, 0.0);
  }

  for (std::uint32_t i = 0; i < kMixtureDraws; ++i) {
    gman::BSDFSample const s = closure.sample(wo, uniform(i, dim), uniform(i, dim + 1u));
    if (!(s.pdf > 0.0f)) {
      continue;
    }
    ++successes;
    RtFloat const cosThetaI = n0().dot(s.wi);
    double const cosThetaIAbs = std::fabs(static_cast<double>(cosThetaI));
    for (int ch = 0; ch < 3; ++ch) {
      energyTerms[ch][i] = channelD(s.f, ch) * cosThetaIAbs / static_cast<double>(s.pdf);
    }

    if (s.lobeIndex == 0) {
      ++lobe0;
      lambertReportOk =
          lambertReportOk && !s.isDelta && colorNearRelOrAbs(s.f, closure.eval(wo, s.wi), kRelTol, kAbsTol) &&
          nearRelOrAbs(static_cast<double>(s.pdf), static_cast<double>(closure.pdf(wo, s.wi)), kRelTol, kAbsTol);
      lambertWis.push_back(s.wi);
    } else if (s.lobeIndex == 1) {
      ++lobe1;
      ++deltaSuccesses;
      GMANColor const c = drawC(s.f, cosThetaI);
      mirrorReportOk = mirrorReportOk && s.isDelta && std::fabs(static_cast<double>(s.pdf) - 0.2) <= kDeltaPdfTol &&
                       colorNearRel(c, kMixtureMirror, kWeightRelTol);
    } else {
      ++deltaSuccesses;
      GMANColor const c = drawC(s.f, cosThetaI);
      bool const isReflected = cosThetaO * cosThetaI > 0.0f;
      if (isReflected) {
        ++lobe2Reflected;
        dielectricReportOk = dielectricReportOk && s.isDelta &&
                             std::fabs(static_cast<double>(s.pdf) - 0.6 * fr.f) <= kDeltaPdfTol &&
                             colorNearRelScalar(c, 0.6 * fr.f, kWeightRelTol);
      } else {
        ++lobe2Transmitted;
        dielectricReportOk = dielectricReportOk && s.isDelta &&
                             std::fabs(static_cast<double>(s.pdf) - 0.6 * (1.0 - fr.f)) <= kDeltaPdfTol &&
                             colorNearRelScalar(c, 0.6 * (1.0 - fr.f) * scale, kWeightRelTol);
      }
    }
  }

  check(successes >= static_cast<std::uint32_t>(kMinSuccessFraction * kMixtureDraws),
        name + ": at least 99.99% of draws succeed");
  check(lambertReportOk, name + ": a lambert draw reports isDelta false and matches eval/pdf at its wi");
  check(mirrorReportOk,
        name + ": a mirror draw reports isDelta true, pdf 0.2 within 1e-5 and c (0.1, 0.2, 0.3) within 1e-5 "
               "relative");
  check(dielectricReportOk, name + ": a dielectric draw reports isDelta true and its branch's pdf and c within 1e-5");

  double const n = static_cast<double>(kMixtureDraws);
  struct FractionCase {
    std::uint32_t count;
    double p;
    char const* label;
  };
  FractionCase const fractionCases[] = {
      {lobe0, 0.2, "lobeIndex 0"},
      {lobe1, 0.2, "lobeIndex 1"},
      {lobe2Reflected, 0.6 * fr.f, "lobeIndex 2 reflected"},
      {lobe2Transmitted, 0.6 * (1.0 - fr.f), "lobeIndex 2 transmitted"},
  };
  for (auto const& fc : fractionCases) {
    double const fraction = static_cast<double>(fc.count) / n;
    double const sigma = std::sqrt(fc.p * (1.0 - fc.p) / n);
    std::printf("info: %s, %s: fraction %.7f against %.7f, sigma %.7f (%.3f sigma)\n", name.c_str(), fc.label, fraction,
                fc.p, sigma, sigma > 0.0 ? std::fabs(fraction - fc.p) / sigma : 0.0);
    check(std::fabs(fraction - fc.p) <= std::max(5.0 * sigma, kFractionFloor),
          name + ": the fraction with " + fc.label + " is within 5sigma of its target");
  }

  // The 64 Lambert bins plus the delta successes' pooled bin.
  GMANVector const normal = woNormal(wo);
  gman::TangentFrame const frame = gman::tangentFrame(normal);
  double const dCos = 1.0 / kBins;
  double const dPhi = 2.0 * kPiD / kBins;
  double const cellArea = (dCos / kMassPoints) * (dPhi / kMassPoints);

  std::vector<double> counts(static_cast<std::size_t>(kBins) * static_cast<std::size_t>(kBins), 0.0);
  for (auto const& wi : lambertWis) {
    GMANVector const local = frame.toLocal(wi);
    double phi = std::atan2(static_cast<double>(local.getY()), static_cast<double>(local.getX()));
    if (phi < 0.0) {
      phi += 2.0 * kPiD;
    }
    int const cosBin = std::clamp(static_cast<int>(static_cast<double>(local.getZ()) / dCos), 0, kBins - 1);
    int const phiBin = std::clamp(static_cast<int>(phi / dPhi), 0, kBins - 1);
    counts[static_cast<std::size_t>(cosBin * kBins + phiBin)] += 1.0;
  }

  double const nSuccesses = static_cast<double>(successes);
  double lambertMassSum = 0.0;
  double pooledMass = 0.0, pooledCount = 0.0;
  bool binsOk = true;
  double maxDeviationSigma = 0.0;
  for (int i = 0; i < kBins; ++i) {
    for (int j = 0; j < kBins; ++j) {
      double mass = 0.0;
      for (int a = 0; a < kMassPoints; ++a) {
        double const cosTheta = (i + (a + 0.5) / kMassPoints) * dCos;
        double const sinTheta = std::sqrt(std::max(0.0, 1.0 - cosTheta * cosTheta));
        for (int b = 0; b < kMassPoints; ++b) {
          double const phi = (j + (b + 0.5) / kMassPoints) * dPhi;
          GMANVector const local(static_cast<RtFloat>(sinTheta * std::cos(phi)),
                                 static_cast<RtFloat>(sinTheta * std::sin(phi)), static_cast<RtFloat>(cosTheta));
          mass += static_cast<double>(closure.pdf(wo, frame.toWorld(local))) * cellArea;
        }
      }
      lambertMassSum += mass;
      double const expected = nSuccesses * mass;
      double const count = counts[static_cast<std::size_t>(i * kBins + j)];
      if (expected < kMinBinCount) {
        pooledMass += mass;
        pooledCount += count;
        continue;
      }
      double const sigma = std::sqrt(expected * (1.0 - mass));
      double const deviation = std::fabs(count - expected);
      binsOk = binsOk && deviation <= 5.0 * sigma;
      if (sigma > 0.0) {
        maxDeviationSigma = std::max(maxDeviationSigma, deviation / sigma);
      }
    }
  }

  double const deltaMass = 0.8;
  double const deltaExpected = nSuccesses * deltaMass;
  double const deltaCount = static_cast<double>(deltaSuccesses);
  if (deltaExpected < kMinBinCount) {
    pooledMass += deltaMass;
    pooledCount += deltaCount;
  } else {
    double const sigma = std::sqrt(deltaExpected * (1.0 - deltaMass));
    double const deviation = std::fabs(deltaCount - deltaExpected);
    binsOk = binsOk && deviation <= 5.0 * sigma;
    if (sigma > 0.0) {
      maxDeviationSigma = std::max(maxDeviationSigma, deviation / sigma);
    }
  }
  if (pooledMass > 0.0) {
    double const expected = nSuccesses * pooledMass;
    double const sigma = std::sqrt(expected * (1.0 - pooledMass));
    double const deviation = std::fabs(pooledCount - expected);
    binsOk = binsOk && deviation <= 5.0 * sigma;
    if (sigma > 0.0) {
      maxDeviationSigma = std::max(maxDeviationSigma, deviation / sigma);
    }
  }
  std::printf("info: %s: %u successes, lambert mass sum %.8f, largest bin deviation %.3f sigma\n", name.c_str(),
              successes, lambertMassSum, maxDeviationSigma);
  check(std::fabs(lambertMassSum - 0.2) <= kMassSumTol, name + ": the lambert masses sum to 0.2 within 1e-4");
  check(binsOk, name + ": every bin count, the delta bin included, is within 5sigma of N * mass");

  double const expectedMean = angle == kMixtureAnglesDeg[0] ? kMixtureExpectedMean[0] : kMixtureExpectedMean[1];
  for (int ch = 0; ch < 3; ++ch) {
    GmanMeanStderr const stat = meanStderr(energyTerms[ch]);
    std::printf("info: %s, channel %d: mean %.7f, sigma %.7f, expected %.7f\n", name.c_str(), ch, stat.mean,
                stat.stderrOfMean, expectedMean);
    checkNear(stat.mean, expectedMean, stat.stderrOfMean, kEnergyFloor,
              name + ", channel " + std::to_string(ch) +
                  ": the mean of f * |cos(theta_i)| / pdf is within 5sigma of the target, floor 1e-4");
  }
}

void checkMixture() {
  for (std::size_t k = 0; k < 2; ++k) {
    std::uint32_t const dim = kDimMixtureMain + 2u * static_cast<std::uint32_t>(k);
    checkMixtureAngle(kMixtureAnglesDeg[k], dim);
  }
}

// ---- Clamps, eta, capacity and failed draws. ----

void checkClampsEtaCapacityFailedDraws() {
  gman::BSDF mirrorClosure(n0());
  mirrorClosure.addMirror(kOutOfRangeWeight);
  check(mirrorClosure.lobeCount() == 1 && mirrorClosure.lobe(0).kind == gman::LobeKind::mirror &&
            colorExactly(mirrorClosure.lobe(0).weight, kOutOfRangeClamped) && mirrorClosure.lobe(0).alpha == 0.0f &&
            mirrorClosure.lobe(0).eta == 0.0f,
        "addMirror((1.4, 0.2, -0.3)) yields weight exactly (1, 0.2, 0), alpha 0 and eta 0");

  gman::BSDF dielectricClosure(n0());
  dielectricClosure.addDielectric(kOutOfRangeWeight, kEta);
  check(dielectricClosure.lobeCount() == 1 && dielectricClosure.lobe(0).kind == gman::LobeKind::dielectric &&
            colorExactly(dielectricClosure.lobe(0).weight, kOutOfRangeClamped) &&
            dielectricClosure.lobe(0).alpha == 0.0f && dielectricClosure.lobe(0).eta == kEta,
        "addDielectric((1.4, 0.2, -0.3), 1.5) yields weight exactly (1, 0.2, 0), alpha 0 and eta exactly 1.5");

  RtFloat const nan = std::nanf("");
  gman::BSDF mirrorNaN(n0());
  mirrorNaN.addMirror(GMANColor(nan, 0.2f, -0.3f));
  gman::BSDF dielectricNaN(n0());
  dielectricNaN.addDielectric(GMANColor(nan, 0.2f, -0.3f), kEta);
  check(mirrorNaN.lobe(0).weight.getRed() == 0.0f && dielectricNaN.lobe(0).weight.getRed() == 0.0f,
        "a NaN weight channel stores 0 in both addMirror and addDielectric");

  struct EtaCase {
    RtFloat input;
    RtFloat expected;
  };
  EtaCase const etaCases[] = {
      {nan, 1.0f},    {0.0f, 1.0f},   {-1.0f, 1.0f},
      {1e-3f, 0.01f}, {1e3f, 100.0f}, {std::numeric_limits<RtFloat>::infinity(), 100.0f},
  };
  bool etaOk = true;
  for (auto const& c : etaCases) {
    gman::BSDF closure(n0());
    closure.addDielectric(kWhite, c.input);
    etaOk = etaOk && closure.lobe(0).eta == c.expected;
  }
  check(etaOk, "eta of NaN, 0 and -1 store exactly 1; 1e-3 stores exactly 0.01; 1e3 and infinity store exactly 100");

  gman::BSDF const indexMatched = buildDielectric(kR, 1.0f);
  GMANVector const wo60 = woAt(60.0);
  Vec3 const negWo60D = vNeg(toVec3(wo60));
  bool transmitOk = true, pdfOneOk = true, cOk = true;
  for (std::uint32_t i = 0; i < kEtaOneDraws; ++i) {
    gman::BSDFSample const s = indexMatched.sample(wo60, uniform(i, kDimEtaOne), uniform(i, kDimEtaOne + 1u));
    transmitOk = transmitOk && s.pdf > 0.0f && s.isDelta && vectorNear(s.wi, negWo60D, kDirTol);
    pdfOneOk = pdfOneOk && std::fabs(static_cast<double>(s.pdf) - 1.0) <= kPdfExactTol;
    cOk = cOk && colorNearRel(drawC(s.f, n0().dot(s.wi)), kR, kWeightRelTol);
  }
  check(transmitOk, "at eta 1 and the 60-degree wo, every draw transmits with wi = -wo within 1e-5");
  check(pdfOneOk, "at eta 1 and the 60-degree wo, every draw's pdf is 1 within 1e-6");
  check(cOk, "at eta 1 and the 60-degree wo, every draw's c is the weight within 1e-5 relative");

  gman::BSDF lambertOnly(n0());
  lambertOnly.addLambert(kR);
  gman::BSDF ggxOnly(n0());
  ggxOnly.addGGX(kR, 0.3f);
  check(lambertOnly.lobe(0).eta == 0.0f && ggxOnly.lobe(0).eta == 0.0f, "a lambert and a ggx lobe each read eta 0");

  gman::BSDF full(n0());
  bool addsOk = true;
  for (std::size_t i = 0; i < gman::BSDF::kMaxLobes; ++i) {
    try {
      switch (i % 4) {
      case 0:
        full.addLambert(kR);
        break;
      case 1:
        full.addGGX(kR, 0.3f);
        break;
      case 2:
        full.addMirror(kR);
        break;
      default:
        full.addDielectric(kWhite, kEta);
        break;
      }
    } catch (GMANError const&) {
      addsOk = false;
    }
  }
  check(addsOk && full.lobeCount() == gman::BSDF::kMaxLobes, "kMaxLobes calls mixing all four adders succeed");

  bool mirrorThrew = false, dielectricThrew = false;
  try {
    full.addMirror(kR);
  } catch (GMANError const& e) {
    mirrorThrew = e.getCode() == RIE_LIMIT;
  }
  try {
    full.addDielectric(kWhite, kEta);
  } catch (GMANError const& e) {
    dielectricThrew = e.getCode() == RIE_LIMIT;
  }
  check(mirrorThrew && dielectricThrew && full.lobeCount() == gman::BSDF::kMaxLobes,
        "one more addMirror and one more addDielectric each throw GMANError RIE_LIMIT, and lobeCount stays "
        "kMaxLobes");

  gman::BSDF tangentMirror(kAxisNormal);
  tangentMirror.addMirror(kR);
  gman::BSDF tangentDielectric(kAxisNormal);
  tangentDielectric.addDielectric(kWhite, kEta);
  bool mirrorZero = true, dielectricZero = true;
  for (std::uint32_t i = 0; i < kTangentDraws; ++i) {
    gman::BSDFSample const s1 =
        tangentMirror.sample(kTangentWo, uniform(i, kDimTangentMirror), uniform(i, kDimTangentMirror + 1u));
    mirrorZero = mirrorZero && s1.pdf == 0.0f;
    gman::BSDFSample const s2 =
        tangentDielectric.sample(kTangentWo, uniform(i, kDimTangentDielectric), uniform(i, kDimTangentDielectric + 1u));
    dielectricZero = dielectricZero && s2.pdf == 0.0f;
  }
  check(mirrorZero, "at normal (0, 0, 1) with wo = (1, 0, 0), 256 draws from a mirror closure all answer pdf 0");
  check(dielectricZero,
        "at normal (0, 0, 1) with wo = (1, 0, 0), 256 draws from a dielectric closure all answer pdf 0");
}

// ---- Shadow transmittance. ----

double fAir(GMANVector const& w) {
  double const cosTheta = std::fabs(n0().dot(w));
  return refFresnelFromCos(1.0, static_cast<double>(kEta), cosTheta).f;
}

void checkShadowTransmittance() {
  gman::BSDF const white = buildDielectric(kWhite, kEta);
  gman::BSDF const withR = buildDielectric(kR, kEta);

  for (std::size_t k = 0; k < kDielectricAngleCount; ++k) {
    double const angle = kDielectricAnglesDeg[k];
    GMANVector const w = woAt(angle);
    double const transmittance = 1.0 - fAir(w);
    GMANColor const expected = scaled(kWhite, transmittance);
    check(colorNearAbs(white.shadowTransmittance(w), expected, kShadowAbsTol),
          "shadow, " + angleName(angle) + ": a white dielectric answers 1 - F_air within 1e-5 absolute");
    if (angle == 60.0) {
      GMANColor const expectedR = scaled(kR, transmittance);
      check(colorNearRel(withR.shadowTransmittance(w), expectedR, kWeightRelTol),
            "shadow, " + angleName(angle) +
                ": at weight R, the answer is R times the white answer within 1e-5 "
                "relative");
    }
  }

  gman::BSDF const indexMatched = buildDielectric(kWhite, 1.0f);
  check(colorNearAbs(indexMatched.shadowTransmittance(woAt(60.0)), kWhite, kShadowTightTol),
        "shadow: a dielectric of eta 1 answers white within 1e-6");

  gman::BSDF axisWhite(kAxisNormal);
  axisWhite.addDielectric(kWhite, kEta);
  check(colorExactly(axisWhite.shadowTransmittance(kTangentWo), kBlack),
        "shadow: at normal (0, 0, 1) with w = (1, 0, 0), a white dielectric answers black exactly");

  gman::BSDF noDielectric(n0());
  noDielectric.addLambert(kR);
  noDielectric.addGGX(kR, 0.3f);
  noDielectric.addMirror(kR);
  check(colorExactly(noDielectric.shadowTransmittance(woAt(0.0)), kBlack),
        "shadow: a closure of lambert, ggx and mirror lobes answers black exactly");

  gman::BSDF const mixture = buildMixture();
  for (std::size_t k = 0; k < 2; ++k) {
    double const angle = kMixtureAnglesDeg[k];
    GMANVector const w = woAt(angle);
    double const expected = 0.6 * (1.0 - fAir(w));
    check(colorNearAbs(
              mixture.shadowTransmittance(w),
              GMANColor(static_cast<RtFloat>(expected), static_cast<RtFloat>(expected), static_cast<RtFloat>(expected)),
              kShadowAbsTol),
          "shadow: the mixture answers 0.6 * (1 - F_air) within 1e-5 at " + angleName(angle));
  }

  gman::BSDF twoDielectric(n0());
  twoDielectric.addDielectric(GMANColor(0.5f, 0.5f, 0.5f), kEta);
  twoDielectric.addDielectric(GMANColor(0.3f, 0.3f, 0.3f), kEta);
  GMANVector const w60 = woAt(60.0);
  double const expectedTwo = 0.8 * (1.0 - fAir(w60));
  check(colorNearAbs(twoDielectric.shadowTransmittance(w60),
                     GMANColor(static_cast<RtFloat>(expectedTwo), static_cast<RtFloat>(expectedTwo),
                               static_cast<RtFloat>(expectedTwo)),
                     kShadowAbsTol),
        "shadow: two dielectric lobes answer 0.8 * (1 - F_air) within 1e-5: the query sums its dielectric lobes");
}

// ---- No allocation. ----

void checkAllocationDelta(bool holds, char const* message) {
#if GMAN_ADDRESS_SANITIZED
  (void)holds;
  std::printf("skip: %s (ASan-built)\n", message);
#else
  check(holds, message);
#endif
}

void checkNoAllocation() {
  gman::BSDF full(n0());
  for (std::size_t i = 0; i < gman::BSDF::kMaxLobes; ++i) {
    switch (i % 4) {
    case 0:
      full.addLambert(kR);
      break;
    case 1:
      full.addGGX(kR, 0.3f);
      break;
    case 2:
      full.addMirror(kR);
      break;
    default:
      full.addDielectric(kWhite, kEta);
      break;
    }
  }

  GMANVector const wo = woAt(60.0);
  GMANVector const wi = woAt(0.0);
  RtFloat u1[kNoAllocCalls];
  RtFloat u2[kNoAllocCalls];
  for (int i = 0; i < kNoAllocCalls; ++i) {
    u1[i] = uniform(static_cast<std::uint32_t>(i), kDimNoAlloc);
    u2[i] = uniform(static_cast<std::uint32_t>(i), kDimNoAlloc + 1u);
  }

  double sink = 0.0;
  long const before = raybvhAllocationCount();
  for (int i = 0; i < kNoAllocCalls; ++i) {
    sink += full.eval(wo, wi).getRed();
  }
  for (int i = 0; i < kNoAllocCalls; ++i) {
    sink += full.pdf(wo, wi);
  }
  for (int i = 0; i < kNoAllocCalls; ++i) {
    sink += full.sample(wo, u1[i], u2[i]).pdf;
  }
  for (int i = 0; i < kNoAllocCalls; ++i) {
    sink += full.shadowTransmittance(wo).getRed();
  }
  long const after = raybvhAllocationCount();

  check(sink >= 0.0, "the counted calls answered");
  checkAllocationDelta(after == before, "1000 calls each of eval, pdf, sample and shadowTransmittance on a full "
                                        "closure allocate nothing");
}

} // namespace

int main() {
  checkReference();
  checkMirror();
  checkDielectric();
  checkMixture();
  checkClampsEtaCapacityFailedDraws();
  checkShadowTransmittance();
  checkNoAllocation();

  return checkSummary("gman::BSDF's mirror and dielectric delta lobes match exact Fresnel and Snell reflection and "
                      "refraction, conserve energy, are reciprocal, and combine correctly in a mixture");
}
