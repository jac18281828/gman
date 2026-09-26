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
 * and f; reciprocity; energy by quadrature and by sample(); sample()
 * against pdf() by histogram; sidedness; clamping, failed draws and
 * capacity; a two-lobe mixture; and no heap allocation.
 *
 * Every statistical check draws i.i.d. samples,
 * unitFloat(sampleHash(kSeed, 0, 0, i, dimension)) with a distinct
 * dimension per random number, so its 5-sigma bound uses the real standard
 * error. Two deterministic quadratures, independent of sample() and
 * pdf(), stand in for exact integrals: a directional-albedo estimate over
 * wo's whole hemisphere (energy) and a per-bin mass integral of the
 * closure's own pdf (the sampling-matches-pdf histogram). Both draw their
 * nodes from GGX's own distribution of visible normals (Heitz, "Sampling
 * the GGX Distribution of Visible Normals", JCGT 7(4), 2018) -- a
 * reimplementation local to this file, apart from libgman -- since a
 * uniform grid resolves the lobe far too slowly near a grazing wo; each
 * shows its own convergence by doubling resolution.
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

constexpr std::uint32_t kSeed = 20260925u;
constexpr double kPiD = 3.14159265358979323846;

constexpr std::uint32_t kDraws = 1u << 16;
constexpr std::uint32_t kPairs = 1u << 12;
constexpr std::uint32_t kReferencePairs = 1u << 10;
constexpr std::uint32_t kFailedDraws = 256u;
constexpr int kNoAllocCalls = 1000;

constexpr int kBins = 8;

constexpr double kRelTol = 1e-6;
constexpr double kAbsTol = 1e-7;
constexpr double kReferenceRelTol = 1e-5;
constexpr double kReferencePairRelTol = 1e-3;
constexpr double kReferencePairAbsTol = 1e-6;
constexpr double kMassSumTol = 1e-4;
constexpr double kMassDoubleTol = 1e-5;
constexpr double kMassSumDoubleTol = 1e-4;
constexpr double kQuadDoubleTol = 1e-4;
constexpr double kAlbedoBoundTol = 1e-4;
constexpr double kPerDrawBoundTol = 1e-5;
constexpr double kEnergyFloor = 1e-4;
constexpr double kUnitLengthTol = 1e-5;
constexpr double kMinSuccessBinCount = 20.0;

// The VNDF quadrature's radial substitution, u1 = 1 - (1 - v1)^kQuadPower:
// concentrates nodes where u1 approaches 1 (a visible normal near wo's own
// horizon), the region a plain uniform grid resolves too slowly.
constexpr double kQuadPower = 4.0;
constexpr int kQuadN1 = 256;
constexpr int kQuadN2 = 128;

// The plain (cosTheta, phi) sub-sampling within each of the 8x8 bins;
// refined only for the 89-degree wo, whose pdf a coarse submesh cannot
// resolve.
constexpr int kMassPointsNormal = 32;
constexpr int kMassPointsRefined = 128;
constexpr std::size_t kRefinedWoIndex = 2; // 89 degrees

constexpr double kWoAnglesDegrees[] = {0.0, 60.0, 89.0, 120.0};
constexpr std::size_t kWoCount = sizeof(kWoAnglesDegrees) / sizeof(kWoAnglesDegrees[0]);
constexpr std::size_t kTwoLobeWoIndices[] = {1, 3}; // 60 and 120 degrees

constexpr RtFloat kReferenceAlpha = 0.3f;
constexpr RtFloat kEnergyAlphas[] = {0.05f, 0.3f, 1.0f};
constexpr RtFloat kHistogramAlphas[] = {0.3f, 1.0f};

GMANVector const kN0Unnormalized(0.3f, -0.5f, 0.8f);
GMANColor const kR(0.8f, 0.5f, 0.2f);
GMANColor const kWhite(1.0f, 1.0f, 1.0f);
GMANColor const kBlack(0.0f, 0.0f, 0.0f);
GMANColor const kR1(0.5f, 0.4f, 0.3f);
GMANColor const kR2(0.5f, 0.5f, 0.5f);
constexpr RtFloat kTwoLobeAlpha = 0.3f;
// The mean of kR1's channels over the sum of both weights' means: the
// probability sample() picks the Lambert lobe.
constexpr double kLobe0Probability = 4.0 / 9.0;
GMANColor const kOutOfRange(1.4f, 0.2f, -0.3f);
GMANColor const kOutOfRangeClamped(1.0f, 0.2f, 0.0f);
GMANVector const kAxisNormal(0.0f, 0.0f, 1.0f);
GMANVector const kTangentWo(1.0f, 0.0f, 0.0f);

RtFloat uniform(std::uint32_t index, std::uint32_t dimension) {
  return gman::unitFloat(gman::sampleHash(kSeed, 0, 0, index, dimension));
}

// Dimension bases, one block per statistical draw so no two random numbers
// in this file share one.
constexpr std::uint32_t kDimReferencePairs = 0u;  // 3 alphas * 4
constexpr std::uint32_t kDimReciprocity = 16u;    // 4
constexpr std::uint32_t kDimEnergyMC = 24u;       // 3 alphas * 4 wo * 2
constexpr std::uint32_t kDimHistogramDraws = 48u; // 2 alphas * 4 wo * 2
constexpr std::uint32_t kDimSidesFar = 64u;       // 4 wo * 2
constexpr std::uint32_t kDimSidesNorm = 72u;      // 4 wo * 2
constexpr std::uint32_t kDimGrazing = 80u;        // 2
constexpr std::uint32_t kDimClampSample = 84u;    // 4 wo * 2
constexpr std::uint32_t kDimClampEval = 92u;      // 4 wo * 2
constexpr std::uint32_t kDimTwoLobeDraws = 100u;  // 2 wo * 2
constexpr std::uint32_t kDimNoAlloc = 104u;       // 2

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
Vec3 vSub(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 vScale(Vec3 a, double s) { return {a.x * s, a.y * s, a.z * s}; }
Vec3 vNeg(Vec3 a) { return {-a.x, -a.y, -a.z}; }
double vDot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3 vCross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
Vec3 vNormalize(Vec3 a) {
  double const m = std::sqrt(vDot(a, a));
  return {a.x / m, a.y / m, a.z / m};
}
Vec3 toVec3(GMANVector const& v) { return {v.getX(), v.getY(), v.getZ()}; }
GMANVector toGMANVector(Vec3 const& v) {
  return GMANVector(static_cast<RtFloat>(v.x), static_cast<RtFloat>(v.y), static_cast<RtFloat>(v.z));
}

// An orthonormal basis (t, b) about n, any convention: quadrature nodes'
// azimuth is arbitrary as long as it is consistent within one integral.
struct Basis {
  Vec3 t, b;
};

Basis buildBasis(Vec3 n) {
  Vec3 const seed = std::fabs(n.z) < 0.9 ? Vec3{0.0, 0.0, 1.0} : Vec3{1.0, 0.0, 0.0};
  Vec3 const b = vNormalize(vCross(n, seed));
  Vec3 const t = vCross(b, n);
  return {t, b};
}

Vec3 toLocal(Basis const& frame, Vec3 n, Vec3 v) { return {vDot(v, frame.t), vDot(v, frame.b), vDot(v, n)}; }
Vec3 toWorld(Basis const& frame, Vec3 n, Vec3 v) {
  return vAdd(vAdd(vScale(frame.t, v.x), vScale(frame.b, v.y)), vScale(n, v.z));
}

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

// ---- The test's own reimplementation of Heitz's visible-normal sample,
// used only to place quadrature nodes; libgman's own sampler is what the
// sampling-matches-pdf histogram checks against these integrals. ----

Vec3 visibleNormalLocal(Vec3 woLocal, double alpha, double u1, double u2) {
  Vec3 vh = vNormalize({alpha * woLocal.x, alpha * woLocal.y, woLocal.z});
  double const lengthSq = vh.x * vh.x + vh.y * vh.y;
  Vec3 const t1 = lengthSq > 0.0 ? vScale({-vh.y, vh.x, 0.0}, 1.0 / std::sqrt(lengthSq)) : Vec3{1.0, 0.0, 0.0};
  Vec3 const t2 = vCross(vh, t1);

  double const r = std::sqrt(u1);
  double const phi = 2.0 * kPiD * u2;
  double const t1Coord = r * std::cos(phi);
  double t2Coord = r * std::sin(phi);
  double const s = 0.5 * (1.0 + vh.z);
  t2Coord = (1.0 - s) * std::sqrt(std::max(0.0, 1.0 - t1Coord * t1Coord)) + s * t2Coord;

  double const nz = std::sqrt(std::max(0.0, 1.0 - t1Coord * t1Coord - t2Coord * t2Coord));
  Vec3 const nh = vAdd(vAdd(vScale(t1, t1Coord), vScale(t2, t2Coord)), vScale(vh, nz));
  return vNormalize({alpha * nh.x, alpha * nh.y, std::max(0.0, nh.z)});
}

// ---- The directional-albedo quadrature: E = integral of eval * |cos
// theta_i| over wo's hemisphere, by importance-sampling a deterministic
// (v1, u2) grid through the visible-normal construction above. The node
// density this grid induces is exactly known in closed form -- the GGX
// lobe's own pdf -- so dividing by it and averaging is a valid, unbiased
// quadrature for any integrable eval, never just the GGX lobe alone, at
// any resolution. ----

struct Channels3 {
  double c[3];
};

Channels3 quadratureEnergy(gman::BSDF const& bsdf, GMANVector const& normal, GMANVector const& wo, RtFloat alpha,
                           int n1, int n2) {
  Vec3 const nD = toVec3(normal);
  Vec3 const woD = toVec3(wo);
  Vec3 const nf = vDot(nD, woD) < 0.0 ? vNeg(nD) : nD;
  Basis const frame = buildBasis(nf);
  Vec3 const woLocal = toLocal(frame, nf, woD);
  double const cosThetaO = woLocal.z;
  double const g1O = 1.0 / (1.0 + refLambda(alpha, cosThetaO));

  double sum[3] = {0.0, 0.0, 0.0};
  double const dv1 = 1.0 / n1;
  double const du2 = 1.0 / n2;
  for (int i = 0; i < n1; ++i) {
    double const v1 = (i + 0.5) * dv1;
    double const u1 = 1.0 - std::pow(1.0 - v1, kQuadPower);
    double const jacobian = kQuadPower * std::pow(1.0 - v1, kQuadPower - 1.0);
    for (int j = 0; j < n2; ++j) {
      double const u2 = (j + 0.5) * du2;
      Vec3 const hLocal = visibleNormalLocal(woLocal, alpha, u1, u2);
      double const woDotH = vDot(woLocal, hLocal);
      if (!(woDotH > 0.0)) {
        continue;
      }
      Vec3 const wiLocal = vSub(vScale(hLocal, 2.0 * woDotH), woLocal);
      double const cosThetaI = wiLocal.z;
      if (!(cosThetaI > 0.0)) {
        continue;
      }
      double const d = refD(alpha, hLocal.z);
      double const pdfNode = g1O * d / (4.0 * cosThetaO);
      if (!(pdfNode > 0.0)) {
        continue;
      }
      GMANVector const wi = toGMANVector(toWorld(frame, nf, wiLocal));
      GMANColor const f = bsdf.eval(wo, wi);
      double const weight = jacobian / pdfNode;
      for (int c = 0; c < 3; ++c) {
        sum[c] += channel(f, c) * cosThetaI * weight;
      }
    }
  }
  double const norm = 1.0 / (static_cast<double>(n1) * static_cast<double>(n2));
  return {{sum[0] * norm, sum[1] * norm, sum[2] * norm}};
}

// ---- The per-bin pdf mass, a plain nested (cosTheta, phi) sub-sample of
// the closure's own pdf, as bsdf_test.cpp's Lambert histogram integrates.
// ----

struct BinMasses {
  double mass[kBins][kBins];
};

BinMasses computeBinMasses(gman::BSDF const& bsdf, GMANVector const& wo, int massPoints) {
  GMANVector const normal = woNormal(wo);
  gman::TangentFrame const frame = gman::tangentFrame(normal);
  double const dCos = 1.0 / kBins;
  double const dPhi = 2.0 * kPiD / kBins;
  double const cellArea = (dCos / massPoints) * (dPhi / massPoints);

  BinMasses result{};
  for (int i = 0; i < kBins; ++i) {
    for (int j = 0; j < kBins; ++j) {
      double mass = 0.0;
      for (int a = 0; a < massPoints; ++a) {
        double const cosTheta = (i + (a + 0.5) / massPoints) * dCos;
        double const sinTheta = std::sqrt(std::max(0.0, 1.0 - cosTheta * cosTheta));
        for (int b = 0; b < massPoints; ++b) {
          double const phi = (j + (b + 0.5) / massPoints) * dPhi;
          GMANVector const local(static_cast<RtFloat>(sinTheta * std::cos(phi)),
                                 static_cast<RtFloat>(sinTheta * std::sin(phi)), static_cast<RtFloat>(cosTheta));
          mass += static_cast<double>(bsdf.pdf(wo, frame.toWorld(local))) * cellArea;
        }
      }
      result.mass[i][j] = mass;
    }
  }
  return result;
}

double maxAbsDiff(BinMasses const& a, BinMasses const& b) {
  double worst = 0.0;
  for (int i = 0; i < kBins; ++i) {
    for (int j = 0; j < kBins; ++j) {
      worst = std::max(worst, std::fabs(a.mass[i][j] - b.mass[i][j]));
    }
  }
  return worst;
}

double sumMasses(BinMasses const& m) {
  double total = 0.0;
  for (int i = 0; i < kBins; ++i) {
    for (int j = 0; j < kBins; ++j) {
      total += m.mass[i][j];
    }
  }
  return total;
}

int massPointsFor(std::size_t woIndex) { return woIndex == kRefinedWoIndex ? kMassPointsRefined : kMassPointsNormal; }

// A convergence-checked bin-mass table: asserts the doubled-resolution
// rule agrees with the base one, then returns the finer table.
BinMasses convergedBinMasses(gman::BSDF const& bsdf, GMANVector const& wo, std::size_t woIndex,
                             std::string const& label) {
  int const base = massPointsFor(woIndex);
  BinMasses const coarse = computeBinMasses(bsdf, wo, base);
  BinMasses const fine = computeBinMasses(bsdf, wo, base * 2);
  double const maxDiff = maxAbsDiff(coarse, fine);
  double const sumDiff = std::fabs(sumMasses(coarse) - sumMasses(fine));
  check(maxDiff <= kMassDoubleTol, label + ": doubling the bin-mass submesh agrees on every mass within 1e-5");
  check(sumDiff <= kMassSumDoubleTol, label + ": doubling the bin-mass submesh agrees on the sum within 1e-4");
  return fine;
}

// Every success lies strictly on wo's side, has unit length, is no delta,
// carries expectedLobe, and reports the closure's eval and pdf at its wi.
bool successHolds(gman::BSDF const& bsdf, GMANVector const& wo, gman::BSDFSample const& s, bool checkLobe,
                  std::size_t expectedLobe) {
  GMANVector const n = n0();
  bool const onWoSide = n.dot(wo) * n.dot(s.wi) > 0.0f;
  bool const unit = std::fabs(std::sqrt(static_cast<double>(s.wi.dot(s.wi))) - 1.0) <= kUnitLengthTol;
  bool const lobeOk = !checkLobe || s.lobeIndex == expectedLobe;
  bool const reportsEval = colorNearRelOrAbs(s.f, bsdf.eval(wo, s.wi), kRelTol, kAbsTol);
  bool const reportsPdf = nearRelOrAbs(s.pdf, bsdf.pdf(wo, s.wi), kRelTol, kAbsTol);
  return onWoSide && unit && !s.isDelta && lobeOk && reportsEval && reportsPdf;
}

// Bins a real sample() run and compares its counts with masses' N * mass,
// pooling any bin (including the failure bin) whose expected count is
// below 20 into one.
void checkHistogramAgainstMasses(gman::BSDF const& bsdf, GMANVector const& wo, BinMasses const& masses,
                                 std::uint32_t dim, std::string const& name, bool checkLobe, std::size_t expectedLobe) {
  GMANVector const normal = woNormal(wo);
  gman::TangentFrame const frame = gman::tangentFrame(normal);
  double const dCos = 1.0 / kBins;
  double const dPhi = 2.0 * kPiD / kBins;

  std::vector<double> counts(kBins * kBins, 0.0);
  std::uint32_t successes = 0;
  bool successesOk = true;
  for (std::uint32_t i = 0; i < kDraws; ++i) {
    gman::BSDFSample const s = bsdf.sample(wo, uniform(i, dim), uniform(i, dim + 1u));
    if (!(s.pdf > 0.0f)) {
      continue;
    }
    ++successes;
    successesOk = successesOk && successHolds(bsdf, wo, s, checkLobe, expectedLobe);
    GMANVector const local = frame.toLocal(s.wi);
    double cosTheta = static_cast<double>(local.getZ());
    double phi = std::atan2(static_cast<double>(local.getY()), static_cast<double>(local.getX()));
    if (phi < 0.0) {
      phi += 2.0 * kPiD;
    }
    int const cosBin = std::clamp(static_cast<int>(cosTheta / dCos), 0, kBins - 1);
    int const phiBin = std::clamp(static_cast<int>(phi / dPhi), 0, kBins - 1);
    counts[cosBin * kBins + phiBin] += 1.0;
  }
  check(successesOk, name + ": every success lies strictly on wo's side, has unit length, is no delta and reports "
                            "eval and pdf at its wi");

  double const n = static_cast<double>(kDraws);
  double const massSum = sumMasses(masses);
  check(massSum <= 1.0 + kMassSumTol, name + ": the pdf's bin masses sum to at most 1 within 1e-4");

  double failureCount = n - static_cast<double>(successes);
  double failureMass = std::max(0.0, 1.0 - massSum);

  double pooledCount = 0.0;
  double pooledMass = 0.0;
  bool binsOk = true;
  double maxDeviationSigma = 0.0;
  for (int i = 0; i < kBins; ++i) {
    for (int j = 0; j < kBins; ++j) {
      double const mass = masses.mass[i][j];
      double const expected = n * mass;
      if (expected < kMinSuccessBinCount) {
        pooledCount += counts[i * kBins + j];
        pooledMass += mass;
        continue;
      }
      double const sigma = std::sqrt(expected * (1.0 - mass));
      double const deviation = std::fabs(counts[i * kBins + j] - expected);
      binsOk = binsOk && deviation <= 5.0 * sigma;
      if (sigma > 0.0) {
        maxDeviationSigma = std::max(maxDeviationSigma, deviation / sigma);
      }
    }
  }
  if (n * failureMass < kMinSuccessBinCount) {
    pooledCount += failureCount;
    pooledMass += failureMass;
  } else {
    double const expected = n * failureMass;
    double const sigma = std::sqrt(expected * (1.0 - failureMass));
    double const deviation = std::fabs(failureCount - expected);
    binsOk = binsOk && deviation <= 5.0 * sigma;
    if (sigma > 0.0) {
      maxDeviationSigma = std::max(maxDeviationSigma, deviation / sigma);
    }
  }
  if (pooledMass > 0.0 || pooledCount > 0.0) {
    double const expected = n * pooledMass;
    double const sigma = std::sqrt(expected * (1.0 - pooledMass));
    double const deviation = std::fabs(pooledCount - expected);
    binsOk = binsOk && deviation <= 5.0 * sigma;
    if (sigma > 0.0) {
      maxDeviationSigma = std::max(maxDeviationSigma, deviation / sigma);
    }
  }
  std::printf("info: %s: %u successes, mass sum %.8f, largest bin deviation %.3f sigma\n", name.c_str(), successes,
              massSum, maxDeviationSigma);
  check(binsOk, name + ": every 8x8 (cosTheta, phi) bin count, the failure bin and the pooled bin included, is "
                       "within 5sigma of N * mass");
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

// Energy, by per-draw bound, quadrature and Monte Carlo agreement.
void checkEnergy() {
  for (std::size_t a = 0; a < 3; ++a) {
    RtFloat const alpha = kEnergyAlphas[a];
    gman::BSDF const bsdf = singleGgx(n0(), kWhite, alpha);
    for (std::size_t k = 0; k < kWoCount; ++k) {
      GMANVector const wo = woAt(kWoAnglesDegrees[k]);
      std::string const name = "energy, alpha " + std::to_string(alpha) + ", " + woName(k);

      Channels3 const coarse = quadratureEnergy(bsdf, n0(), wo, alpha, kQuadN1, kQuadN2);
      Channels3 const fine = quadratureEnergy(bsdf, n0(), wo, alpha, 2 * kQuadN1, 2 * kQuadN2);
      bool quadConverged = true;
      for (int c = 0; c < 3; ++c) {
        quadConverged = quadConverged && std::fabs(coarse.c[c] - fine.c[c]) <= kQuadDoubleTol;
      }
      check(quadConverged, name + ": doubling the energy quadrature's resolution agrees within 1e-4 absolute");
      bool albedoBoundOk = true;
      for (int c = 0; c < 3; ++c) {
        albedoBoundOk = albedoBoundOk && fine.c[c] <= 1.0 + kAlbedoBoundTol;
      }
      check(albedoBoundOk, name + ": the quadrature estimate E is at most 1 + 1e-4 per channel");

      std::uint32_t const dim =
          kDimEnergyMC + 2u * (4u * static_cast<std::uint32_t>(a) + static_cast<std::uint32_t>(k));
      std::uint32_t successes = 0;
      bool perDrawOk = true;
      std::vector<double> terms[3];
      for (auto& t : terms) {
        t.resize(kDraws, 0.0);
      }
      for (std::uint32_t i = 0; i < kDraws; ++i) {
        gman::BSDFSample const s = bsdf.sample(wo, uniform(i, dim), uniform(i, dim + 1u));
        if (!(s.pdf > 0.0f)) {
          continue;
        }
        ++successes;
        double const cosThetaI = std::fabs(n0().dot(s.wi));
        for (int c = 0; c < 3; ++c) {
          double const ratio = channel(s.f, c) * cosThetaI / s.pdf;
          perDrawOk = perDrawOk && ratio <= 1.0 + kPerDrawBoundTol;
          terms[c][i] = ratio;
        }
      }
      check(perDrawOk, name + ": every successful draw's f * |cos(theta_i)| / pdf is at most 1 + 1e-5 per channel");

      bool mcOk = true;
      bool mcBoundOk = true;
      for (int c = 0; c < 3; ++c) {
        GmanMeanStderr const stat = meanStderr(terms[c]);
        std::printf("info: %s, channel %d: quadrature E %.6f, Monte Carlo estimate %.6f, sigma %.6f\n", name.c_str(), c,
                    fine.c[c], stat.mean, stat.stderrOfMean);
        mcOk = mcOk && std::fabs(stat.mean - fine.c[c]) <= std::max(5.0 * stat.stderrOfMean, kEnergyFloor);
        mcBoundOk = mcBoundOk && stat.mean <= 1.0 + 5.0 * stat.stderrOfMean;
      }
      check(mcOk, name + ": the Monte Carlo estimate is within 5sigma of E per channel, floor 1e-4");
      check(mcBoundOk, name + ": the Monte Carlo estimate is at most 1 + 5sigma per channel");
      (void)successes;
    }
  }
}

// sample()'s directions match pdf() by histogram.
void checkSamplingMatchesPdf() {
  for (std::size_t a = 0; a < 2; ++a) {
    RtFloat const alpha = kHistogramAlphas[a];
    gman::BSDF const bsdf = singleGgx(n0(), kR, alpha);
    for (std::size_t k = 0; k < kWoCount; ++k) {
      GMANVector const wo = woAt(kWoAnglesDegrees[k]);
      std::string const name = "sampling, alpha " + std::to_string(alpha) + ", " + woName(k);
      BinMasses const masses = convergedBinMasses(bsdf, wo, k, name);
      std::uint32_t const dim =
          kDimHistogramDraws + 2u * (4u * static_cast<std::uint32_t>(a) + static_cast<std::uint32_t>(k));
      checkHistogramAgainstMasses(bsdf, wo, masses, dim, name, true, 0);
    }
  }
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

// A two-lobe closure -- Lambert then GGX.
void checkTwoLobes() {
  gman::BSDF const bsdf = twoLobe();
  check(colorExactly(bsdf.rhoD(), kR1), "two lobes: rhoD is exactly R1");
  check(bsdf.lobeCount() == 2, "two lobes: lobeCount is 2");

  for (std::size_t idx = 0; idx < 2; ++idx) {
    std::size_t const k = kTwoLobeWoIndices[idx];
    GMANVector const wo = woAt(kWoAnglesDegrees[k]);
    std::string const name = "two lobes, " + woName(k);

    BinMasses const masses = convergedBinMasses(bsdf, wo, k, name);
    std::uint32_t const dim = kDimTwoLobeDraws + 2u * static_cast<std::uint32_t>(idx);

    // A histogram-compatible pass counting lobe-0 successes, matching
    // checkHistogramAgainstMasses' own draws exactly (same dimension). A
    // failed draw's lobeIndex carries no meaning, so the denominator is
    // every draw, not just the successful ones.
    std::uint32_t lobe0 = 0;
    for (std::uint32_t i = 0; i < kDraws; ++i) {
      gman::BSDFSample const s = bsdf.sample(wo, uniform(i, dim), uniform(i, dim + 1u));
      if (s.pdf > 0.0f && s.lobeIndex == 0) {
        ++lobe0;
      }
    }
    double const n = static_cast<double>(kDraws);
    double const fraction = static_cast<double>(lobe0) / n;
    double const sigma = std::sqrt(kLobe0Probability * (1.0 - kLobe0Probability) / n);
    std::printf("info: %s: lobe 0 fraction %.6f against %.6f, sigma %.6f\n", name.c_str(), fraction, kLobe0Probability,
                sigma);
    check(std::fabs(fraction - kLobe0Probability) <= 5.0 * sigma,
          name + ": the fraction of all draws succeeding with lobeIndex 0 is within 5sigma of 4/9");

    checkHistogramAgainstMasses(bsdf, wo, masses, dim, name, false, 0);

    Channels3 const coarse = quadratureEnergy(bsdf, n0(), wo, kTwoLobeAlpha, kQuadN1, kQuadN2);
    Channels3 const fine = quadratureEnergy(bsdf, n0(), wo, kTwoLobeAlpha, 2 * kQuadN1, 2 * kQuadN2);
    bool quadConverged = true;
    for (int c = 0; c < 3; ++c) {
      quadConverged = quadConverged && std::fabs(coarse.c[c] - fine.c[c]) <= kQuadDoubleTol;
    }
    check(quadConverged, name + ": doubling the energy quadrature's resolution agrees within 1e-4 absolute");

    std::vector<double> terms[3];
    for (auto& t : terms) {
      t.resize(kDraws, 0.0);
    }
    for (std::uint32_t i = 0; i < kDraws; ++i) {
      gman::BSDFSample const s = bsdf.sample(wo, uniform(i, dim), uniform(i, dim + 1u));
      if (!(s.pdf > 0.0f)) {
        continue;
      }
      double const cosThetaI = std::fabs(n0().dot(s.wi));
      for (int c = 0; c < 3; ++c) {
        terms[c][i] = channel(s.f, c) * cosThetaI / s.pdf;
      }
    }
    bool mcOk = true;
    for (int c = 0; c < 3; ++c) {
      GmanMeanStderr const stat = meanStderr(terms[c]);
      mcOk = mcOk && std::fabs(stat.mean - fine.c[c]) <= std::max(5.0 * stat.stderrOfMean, kEnergyFloor);
      if (c == 0) { // red: weights sum to exactly 1
        mcOk = mcOk && stat.mean <= 1.0 + 5.0 * stat.stderrOfMean;
      }
    }
    check(mcOk, name + ": the Monte Carlo estimate is within 5sigma of E per channel, and the red channel is at "
                       "most 1 + 5sigma");
  }
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
#if GMAN_ADDRESS_SANITIZED
  std::printf("skip: no-allocation check (ASan-built)\n");
#else
  check(after == before, "1000 calls each of eval, pdf and sample on a full closure allocate nothing");
#endif
}

} // namespace

int main() {
  checkReference();
  checkReciprocity();
  checkEnergy();
  checkSamplingMatchesPdf();
  checkSides();
  checkClampsAndCapacity();
  checkTwoLobes();
  checkNoAllocation();

  return checkSummary("gman::BSDF's GGX lobe conserves energy, is reciprocal and samples its own pdf");
}
