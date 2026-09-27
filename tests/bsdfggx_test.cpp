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
 * gman::BSDF's GGX lobe: a double-precision reference for D, Lambda, G2
 * and f; reciprocity; sidedness; the eval underflow guard; clamping,
 * failed draws and capacity; and no heap allocation. bsdfggxstats_test.cpp
 * covers the energy, sample()-against-pdf() and two-lobe statistics this
 * file split off to stay under its time bound.
 *
 * Every statistical check draws i.i.d. samples,
 * unitFloat(sampleHash(kSeed, 0, 0, i, dimension)) with a distinct
 * dimension per random number, so its 5-sigma bound uses the real standard
 * error.
 *
 * raybvhallocator.cpp's counting operator new brackets the allocation
 * check, as bsdf_test.cpp's does.
 */

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

#include "check.h"
#include "gmanbsdf.h"
#include "gmancolor.h"
#include "gmanerror.h"
#include "gmansampling.h"
#include "gmanvector.h"
#include "ri.h"

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

constexpr std::uint32_t kSeed = 20260925u;
constexpr double kPiD = 3.14159265358979323846;

constexpr std::uint32_t kPairs = 1u << 12;
constexpr std::uint32_t kReferencePairs = 1u << 10;
constexpr std::uint32_t kFailedDraws = 256u;
constexpr int kNoAllocCalls = 1000;

constexpr double kRelTol = 1e-6;
constexpr double kAbsTol = 1e-7;
constexpr double kReferenceRelTol = 1e-5;
constexpr double kReferencePairRelTol = 1e-3;
constexpr double kReferencePairAbsTol = 1e-6;

constexpr double kWoAnglesDegrees[] = {0.0, 60.0, 89.0, 120.0};
constexpr std::size_t kWoCount = sizeof(kWoAnglesDegrees) / sizeof(kWoAnglesDegrees[0]);

constexpr RtFloat kReferenceAlpha = 0.3f;
constexpr RtFloat kEnergyAlphas[] = {0.05f, 0.3f, 1.0f};

GMANVector const kN0Unnormalized(0.3f, -0.5f, 0.8f);
GMANColor const kR(0.8f, 0.5f, 0.2f);
GMANColor const kBlack(0.0f, 0.0f, 0.0f);
GMANColor const kR1(0.5f, 0.4f, 0.3f);
GMANColor const kR2(0.5f, 0.5f, 0.5f);
constexpr RtFloat kTwoLobeAlpha = 0.3f;
GMANColor const kOutOfRange(1.4f, 0.2f, -0.3f);
GMANColor const kOutOfRangeClamped(1.0f, 0.2f, 0.0f);
GMANVector const kAxisNormal(0.0f, 0.0f, 1.0f);
GMANVector const kTangentWo(1.0f, 0.0f, 0.0f);

RtFloat uniform(std::uint32_t index, std::uint32_t dimension) {
  return gman::unitFloat(gman::sampleHash(kSeed, 0, 0, index, dimension));
}

// Dimension bases, one block per statistical draw so no two random numbers
// in this file share one.
constexpr std::uint32_t kDimReferencePairs = 0u; // 3 alphas * 4
constexpr std::uint32_t kDimReciprocity = 16u;   // 4
constexpr std::uint32_t kDimSidesFar = 64u;      // 4 wo * 2
constexpr std::uint32_t kDimSidesNorm = 72u;     // 4 wo * 2
constexpr std::uint32_t kDimGrazing = 80u;       // 2
constexpr std::uint32_t kDimClampSample = 84u;   // 4 wo * 2
constexpr std::uint32_t kDimClampEval = 92u;     // 4 wo * 2
constexpr std::uint32_t kDimNoAlloc = 104u;      // 2

GMANVector normalized(GMANVector v) {
  v.normalize();
  return v;
}

GMANVector n0() { return normalized(kN0Unnormalized); }

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

double channel(GMANColor const& c, int i) {
  if (i == 0) {
    return c.getRed();
  }
  return i == 1 ? c.getGreen() : c.getBlue();
}

bool nearRel(double value, double expected, double rel) {
  return std::fabs(value - expected) <= rel * std::max(std::fabs(value), std::fabs(expected));
}

bool nearRelOrAbs(double value, double expected, double rel, double abs) {
  return nearRel(value, expected, rel) || std::fabs(value - expected) <= abs;
}

bool colorNearRelOrAbs(GMANColor const& a, GMANColor const& b, double rel, double abs) {
  for (int c = 0; c < 3; ++c) {
    if (!nearRelOrAbs(channel(a, c), channel(b, c), rel, abs)) {
      return false;
    }
  }
  return true;
}

bool colorExactly(GMANColor const& a, GMANColor const& b) {
  return a.getRed() == b.getRed() && a.getGreen() == b.getGreen() && a.getBlue() == b.getBlue();
}

gman::BSDF singleGgx(GMANVector const& normal, GMANColor const& weight, RtFloat alpha) {
  gman::BSDF bsdf(normal);
  bsdf.addGGX(weight, alpha);
  return bsdf;
}

gman::BSDF twoLobe() {
  gman::BSDF bsdf(n0());
  bsdf.addLambert(kR1);
  bsdf.addGGX(kR2, kTwoLobeAlpha);
  return bsdf;
}

std::string woName(std::size_t woIndex) {
  return "wo at " + std::to_string(static_cast<int>(kWoAnglesDegrees[woIndex])) + " degrees";
}

// ---- A double-precision vector, the test's own, apart from libgman. ----

struct Vec3 {
  double x, y, z;
};

Vec3 vAdd(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
double vDot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3 vNeg(Vec3 a) { return {-a.x, -a.y, -a.z}; }
Vec3 vNormalize(Vec3 a) {
  double const m = std::sqrt(vDot(a, a));
  return {a.x / m, a.y / m, a.z / m};
}
Vec3 toVec3(GMANVector const& v) { return {v.getX(), v.getY(), v.getZ()}; }

// ---- The height-correlated Smith GGX lobe's D, Lambda and f, in double,
// independent of libgman. ----

double refD(double alpha, double cosThetaH) {
  double const alpha2 = alpha * alpha;
  double const denom = cosThetaH * cosThetaH * (alpha2 - 1.0) + 1.0;
  return alpha2 / (kPiD * denom * denom);
}

double refLambda(double alpha, double cosTheta) {
  double const sinTheta2 = std::max(0.0, 1.0 - cosTheta * cosTheta);
  double const tanTheta2 = sinTheta2 / (cosTheta * cosTheta);
  return 0.5 * (-1.0 + std::sqrt(1.0 + alpha * alpha * tanTheta2));
}

struct RefFactor {
  bool sideOk;
  double factor; // D * G2 / (4 cosThetaO cosThetaI), meaningless unless sideOk
};

RefFactor refFactor(double alpha, Vec3 n, Vec3 wo, Vec3 wi) {
  Vec3 const nf = vDot(n, wo) < 0.0 ? vNeg(n) : n;
  double const cosThetaO = vDot(nf, wo);
  double const cosThetaI = vDot(nf, wi);
  if (!(cosThetaO > 0.0) || !(cosThetaI > 0.0)) {
    return {false, 0.0};
  }
  Vec3 const h = vNormalize(vAdd(wo, wi));
  double const cosThetaH = vDot(nf, h);
  double const d = refD(alpha, cosThetaH);
  double const g2 = 1.0 / (1.0 + refLambda(alpha, cosThetaO) + refLambda(alpha, cosThetaI));
  return {true, d * g2 / (4.0 * cosThetaO * cosThetaI)};
}

// The height-correlated Smith GGX reference, in double, independent of
// libgman.
void checkReference() {
  GMANVector const wo = n0();
  gman::BSDF const bsdf = singleGgx(wo, kR, kReferenceAlpha);
  double const expectedFactor = 1.0 / (4.0 * kPiD * kReferenceAlpha * kReferenceAlpha);

  GMANColor const evalResult = bsdf.eval(wo, wo);
  RtFloat const pdfResult = bsdf.pdf(wo, wo);
  bool evalOk = true;
  for (int c = 0; c < 3; ++c) {
    evalOk = evalOk && nearRel(channel(evalResult, c), channel(kR, c) * expectedFactor, kReferenceRelTol);
  }
  check(evalOk, "reference: eval at wo = wi = N0, alpha 0.3 is R / (4 pi alpha^2) within 1e-5 relative");
  check(nearRel(pdfResult, expectedFactor, kReferenceRelTol),
        "reference: pdf at wo = wi = N0, alpha 0.3 is 1 / (4 pi alpha^2) within 1e-5 relative");

  Vec3 const n0d = toVec3(n0());
  for (std::size_t a = 0; a < 3; ++a) {
    RtFloat const alpha = kEnergyAlphas[a];
    gman::BSDF const closure = singleGgx(n0(), kR, alpha);
    std::uint32_t const dim = kDimReferencePairs + 4u * static_cast<std::uint32_t>(a);
    bool ok = true;
    for (std::uint32_t i = 0; i < kReferencePairs; ++i) {
      GMANVector const wo2 = gman::uniformSphere(uniform(i, dim), uniform(i, dim + 1u));
      GMANVector const wi2 = gman::uniformSphere(uniform(i, dim + 2u), uniform(i, dim + 3u));
      RefFactor const ref = refFactor(alpha, n0d, toVec3(wo2), toVec3(wi2));
      GMANColor const actual = closure.eval(wo2, wi2);
      if (!ref.sideOk) {
        ok = ok && colorExactly(actual, kBlack);
        continue;
      }
      for (int c = 0; c < 3; ++c) {
        double const expected = channel(kR, c) * ref.factor;
        ok = ok && nearRelOrAbs(channel(actual, c), expected, kReferencePairRelTol, kReferencePairAbsTol);
      }
    }
    check(ok, "reference: eval over " + std::to_string(kReferencePairs) + " uniform-sphere pairs at alpha " +
                  std::to_string(alpha) +
                  " matches the reference within 1e-3 relative or 1e-6 absolute, black off wo's side");
  }
}

// Reciprocity: eval(wo, wi) == eval(wi, wo), for the two-lobe closure.
void checkReciprocity() {
  gman::BSDF const closure = twoLobe();
  bool ok = true;
  for (std::uint32_t i = 0; i < kPairs; ++i) {
    GMANVector const wo = gman::uniformSphere(uniform(i, kDimReciprocity), uniform(i, kDimReciprocity + 1u));
    GMANVector const wi = gman::uniformSphere(uniform(i, kDimReciprocity + 2u), uniform(i, kDimReciprocity + 3u));
    ok = ok && colorNearRelOrAbs(closure.eval(wo, wi), closure.eval(wi, wo), kRelTol, kAbsTol);
  }
  check(ok, "reciprocity: eval(wo, wi) equals eval(wi, wo) within 1e-6 relative or 1e-7 absolute, two-lobe closure");
}

// Sides, and a closure built at 2 * N0 answering as one at N0.
void checkSides() {
  gman::BSDF const atN0 = singleGgx(n0(), kR, kReferenceAlpha);
  gman::BSDF const atTwiceN0 = singleGgx(n0() * 2.0f, kR, kReferenceAlpha);

  for (std::size_t k = 0; k < kWoCount; ++k) {
    GMANVector const wo = woAt(kWoAnglesDegrees[k]);
    GMANVector const normal = woNormal(wo);
    std::string const name = "sides, " + woName(k);

    std::uint32_t const farDim = kDimSidesFar + 2u * static_cast<std::uint32_t>(k);
    bool farSideOk = true;
    for (std::uint32_t i = 0; i < kPairs; ++i) {
      GMANVector const raw = gman::uniformSphere(uniform(i, farDim), uniform(i, farDim + 1u));
      GMANVector const wi = normal.dot(raw) > 0.0f ? -raw : raw;
      farSideOk = farSideOk && colorExactly(atN0.eval(wo, wi), kBlack) && atN0.pdf(wo, wi) == 0.0f;
    }
    check(farSideOk, name + ": eval is black and pdf is 0 on the far side of wo's normal");

    std::uint32_t const normDim = kDimSidesNorm + 2u * static_cast<std::uint32_t>(k);
    bool normalizedOk = true;
    for (std::uint32_t i = 0; i < kPairs; ++i) {
      GMANVector const wi = gman::uniformSphere(uniform(i, normDim), uniform(i, normDim + 1u));
      GMANColor const f = atN0.eval(wo, wi);
      RtFloat const p = atN0.pdf(wo, wi);
      normalizedOk = normalizedOk && colorNearRelOrAbs(atTwiceN0.eval(wo, wi), f, kRelTol, kAbsTol) &&
                     nearRelOrAbs(atTwiceN0.pdf(wo, wi), p, kRelTol, kAbsTol);
    }
    check(normalizedOk, name + ": a closure built at 2 * N0 answers eval and pdf as one at N0 within 1e-6 relative");
  }

  gman::BSDF const grazing = singleGgx(kAxisNormal, kR, kReferenceAlpha);
  bool grazingOk = true;
  for (std::uint32_t i = 0; i < kFailedDraws; ++i) {
    gman::BSDFSample const s = grazing.sample(kTangentWo, uniform(i, kDimGrazing), uniform(i, kDimGrazing + 1u));
    grazingOk = grazingOk && s.pdf == 0.0f;
  }
  check(grazingOk, "sides: wo in the tangent plane answers pdf 0 from 256 draws");
}

// Both cosines underflowing before Lambda diverges gives G2's reciprocal a
// 0 numerator over a 0 denominator; eval answers black rather than NaN.
void checkUnderflowGuard() {
  gman::BSDF const closure = singleGgx(kAxisNormal, kR, kReferenceAlpha);
  GMANVector wo(1.0f, 0.0f, 1e-30f);
  wo.normalize();
  GMANVector wi(0.0f, 1.0f, 1e-30f);
  wi.normalize();
  GMANColor const f = closure.eval(wo, wi);
  bool const finite = std::isfinite(f.getRed()) && std::isfinite(f.getGreen()) && std::isfinite(f.getBlue());
  check(finite && colorExactly(f, kBlack), "underflow: eval at cosThetaO and cosThetaI of 1e-30 is finite and black");
}

// Clamps, failed draws and capacity.
void checkClampsAndCapacity() {
  RtFloat const nan = std::nanf("");

  gman::BSDF const clamped = singleGgx(n0(), kOutOfRange, kReferenceAlpha);
  check(clamped.lobeCount() == 1 && clamped.lobe(0).kind == gman::LobeKind::ggx &&
            colorExactly(clamped.lobe(0).weight, kOutOfRangeClamped),
        "addGGX((1.4, 0.2, -0.3), 0.3) yields one ggx lobe of weight exactly (1, 0.2, 0)");

  gman::BSDF const nanWeight = singleGgx(n0(), GMANColor(nan, 0.2f, -0.3f), kReferenceAlpha);
  check(nanWeight.lobe(0).weight.getRed() == 0.0f, "a NaN weight channel stores 0");

  struct AlphaCase {
    RtFloat input;
    RtFloat expected;
  };
  AlphaCase const alphaCases[] = {{0.0f, gman::BSDF::kMinGGXAlpha},
                                  {-1.0f, gman::BSDF::kMinGGXAlpha},
                                  {nan, gman::BSDF::kMinGGXAlpha},
                                  {5.0f, 1.0f}};
  bool alphaOk = true;
  for (auto const& c : alphaCases) {
    gman::BSDF const b = singleGgx(n0(), kR, c.input);
    alphaOk = alphaOk && b.lobe(0).alpha == c.expected;
  }
  check(alphaOk, "alpha 0, -1 and NaN store kMinGGXAlpha exactly; alpha 5 stores 1");

  gman::BSDF const atMin = singleGgx(n0(), kR, gman::BSDF::kMinGGXAlpha);
  bool finiteOk = true;
  for (std::size_t k = 0; k < kWoCount; ++k) {
    GMANVector const wo = woAt(kWoAnglesDegrees[k]);
    std::uint32_t const sampleDim = kDimClampSample + 2u * static_cast<std::uint32_t>(k);
    for (std::uint32_t i = 0; i < kPairs; ++i) {
      gman::BSDFSample const s = atMin.sample(wo, uniform(i, sampleDim), uniform(i, sampleDim + 1u));
      finiteOk = finiteOk && std::isfinite(s.pdf) && std::isfinite(s.f.getRed()) && std::isfinite(s.f.getGreen()) &&
                 std::isfinite(s.f.getBlue());
    }
    std::uint32_t const evalDim = kDimClampEval + 2u * static_cast<std::uint32_t>(k);
    for (std::uint32_t i = 0; i < kPairs; ++i) {
      GMANVector const wi = gman::uniformSphere(uniform(i, evalDim), uniform(i, evalDim + 1u));
      GMANColor const f = atMin.eval(wo, wi);
      RtFloat const p = atMin.pdf(wo, wi);
      finiteOk = finiteOk && std::isfinite(f.getRed()) && std::isfinite(f.getGreen()) && std::isfinite(f.getBlue()) &&
                 std::isfinite(p);
    }
  }
  check(finiteOk, "at kMinGGXAlpha, sample, eval and pdf are all finite over 2^12 draws and directions per wo");

  gman::BSDF lambertOnly(n0());
  lambertOnly.addLambert(kR);
  check(lambertOnly.lobe(0).alpha == 0.0f, "a lambert lobe's alpha reads 0");

  gman::BSDF full(n0());
  bool addsOk = true;
  for (std::size_t i = 0; i < gman::BSDF::kMaxLobes; ++i) {
    try {
      if (i % 2 == 0) {
        full.addLambert(kR);
      } else {
        full.addGGX(kR, kReferenceAlpha);
      }
    } catch (GMANError const&) {
      addsOk = false;
    }
  }
  check(addsOk && full.lobeCount() == gman::BSDF::kMaxLobes, "kMaxLobes calls mixing addLambert and addGGX succeed");

  bool threwLimit = false;
  try {
    full.addGGX(kR, kReferenceAlpha);
  } catch (GMANError const& e) {
    threwLimit = e.getCode() == RIE_LIMIT;
  }
  check(threwLimit, "addGGX past kMaxLobes throws GMANError with code RIE_LIMIT");
  check(full.lobeCount() == gman::BSDF::kMaxLobes, "a refused addGGX leaves lobeCount at kMaxLobes");
}

void checkAllocationDelta(bool holds, char const* message) {
#if GMAN_ADDRESS_SANITIZED
  (void)holds;
  std::printf("skip: %s (ASan-built)\n", message);
#else
  check(holds, message);
#endif
}

// No heap allocation.
void checkNoAllocation() {
  gman::BSDF full(n0());
  for (std::size_t i = 0; i < gman::BSDF::kMaxLobes; ++i) {
    if (i % 2 == 0) {
      full.addLambert(kR);
    } else {
      full.addGGX(kR, kReferenceAlpha);
    }
  }

  GMANVector const wo = woAt(kWoAnglesDegrees[1]);
  GMANVector const wi = woAt(kWoAnglesDegrees[0]);
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
  long const after = raybvhAllocationCount();

  check(sink >= 0.0, "the counted calls answered");
  checkAllocationDelta(after == before, "1000 calls each of eval, pdf and sample on a full closure allocate nothing");
}

} // namespace

int main() {
  checkReference();
  checkReciprocity();
  checkSides();
  checkUnderflowGuard();
  checkClampsAndCapacity();
  checkNoAllocation();

  return checkSummary("gman::BSDF's GGX lobe is reciprocal, sided and finite, and its closure clamps and bounds");
}
