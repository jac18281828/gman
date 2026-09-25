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
 * gman::BSDF and its Lambert lobe: energy, reciprocity, sidedness, sample
 * against pdf by histogram, lobe selection, clamping, failed draws,
 * capacity and no heap allocation. Every statistical check draws i.i.d.
 * inputs, unitFloat(sampleHash(kSeed, 0, 0, i, dimension)) with a distinct
 * dimension per random number, so its 5-sigma bound uses the real
 * standard error. The histograms take their expected mass from the
 * closure's own pdf, so they tie sample() to pdf().
 *
 * raybvhallocator.cpp's counting operator new brackets the allocation
 * check. AddressSanitizer replaces allocation below that override, so the
 * counting checks skip under it and print so.
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
constexpr std::uint32_t kFailedDraws = 256u;
constexpr int kNoAllocCalls = 1000;

constexpr int kBins = 8;
// Midpoints per bin along each of cosTheta and phi.
constexpr int kMassPoints = 8;

constexpr double kMinSuccessFraction = 0.9999;
constexpr double kEnergyCosFloor = 1e-2;
constexpr double kEnergyRelTol = 1e-5;
constexpr double kSphereFloor = 1e-4;
constexpr double kRelTol = 1e-6;
constexpr double kAbsTol = 1e-7;
constexpr double kMassSumTol = 1e-4;
constexpr double kUnitLengthTol = 1e-5;

constexpr double kWoAnglesDegrees[] = {0.0, 60.0, 89.0, 120.0};
constexpr std::size_t kWoCount = sizeof(kWoAnglesDegrees) / sizeof(kWoAnglesDegrees[0]);
constexpr std::size_t kTwoLobeWoIndex = 1; // 60 degrees

// One pair of sampleHash dimensions per (check, closure, wo), so no two
// random numbers in this file share one.
constexpr std::uint32_t kDimEnergySample = 0u;
constexpr std::uint32_t kDimEnergySphere = 16u;
constexpr std::uint32_t kDimReciprocity = 32u;
constexpr std::uint32_t kDimSides = 40u;
constexpr std::uint32_t kDimHistogram = 48u;
constexpr std::uint32_t kDimTwoLobe = 56u;
constexpr std::uint32_t kDimFailedDraws = 60u;
constexpr std::uint32_t kDimNoAlloc = 70u;

GMANVector const kN0Unnormalized(0.3f, -0.5f, 0.8f);
GMANColor const kR(0.8f, 0.5f, 0.2f);
GMANColor const kWhite(1.0f, 1.0f, 1.0f);
GMANColor const kBlack(0.0f, 0.0f, 0.0f);
GMANColor const kR1(0.1f, 0.2f, 0.0f);
GMANColor const kR2(0.6f, 0.3f, 0.6f);
// The mean of kR1's channels over the sum of both weights' means.
constexpr double kLobe0Probability = 1.0 / 6.0;
GMANColor const kOutOfRange(1.4f, 0.2f, -0.3f);
GMANColor const kOutOfRangeClamped(1.0f, 0.2f, 0.0f);
GMANVector const kAxisNormal(0.0f, 0.0f, 1.0f);
GMANVector const kTangentWo(1.0f, 0.0f, 0.0f);

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

double channel(GMANColor const& c, int i) {
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

bool nearRel(double value, double expected, double rel) {
  return std::fabs(value - expected) <= rel * std::max(std::fabs(value), std::fabs(expected));
}

bool nearRelOrAbs(double value, double expected, double rel, double abs) {
  return nearRel(value, expected, rel) || std::fabs(value - expected) <= abs;
}

bool colorNearRel(GMANColor const& a, GMANColor const& b, double rel) {
  for (int c = 0; c < 3; ++c) {
    if (!nearRel(channel(a, c), channel(b, c), rel)) {
      return false;
    }
  }
  return true;
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

bool enoughSuccesses(std::uint32_t successes, std::uint32_t draws) {
  return static_cast<double>(successes) >= kMinSuccessFraction * static_cast<double>(draws);
}

gman::BSDF singleLobe(GMANVector const& normal, GMANColor const& weight) {
  gman::BSDF bsdf(normal);
  bsdf.addLambert(weight);
  return bsdf;
}

gman::BSDF twoLobe() {
  gman::BSDF bsdf(n0());
  bsdf.addLambert(kR1);
  bsdf.addLambert(kR2);
  return bsdf;
}

std::string woName(std::size_t woIndex) {
  return "wo at " + std::to_string(static_cast<int>(kWoAnglesDegrees[woIndex])) + " degrees";
}

// f * |cos(theta_i)| / pdf equals the weight on every sample, and a
// uniform-sphere estimate of the integral of eval * |cos(theta_i)|,
// spanning both sides, converges to the weight.
void checkEnergy() {
  GMANColor const weights[] = {kR, kWhite};
  for (std::size_t w = 0; w < 2; ++w) {
    gman::BSDF const bsdf = singleLobe(n0(), weights[w]);
    std::string const closureName = w == 0 ? "R" : "white";
    for (std::size_t k = 0; k < kWoCount; ++k) {
      GMANVector const wo = woAt(kWoAnglesDegrees[k]);
      std::string const name = "energy, " + closureName + ", " + woName(k);
      std::uint32_t const dim =
          kDimEnergySample + 8u * static_cast<std::uint32_t>(w) + 2u * static_cast<std::uint32_t>(k);

      std::uint32_t successes = 0;
      bool ratioOk = true;
      for (std::uint32_t i = 0; i < kDraws; ++i) {
        gman::BSDFSample const s = bsdf.sample(wo, uniform(i, dim), uniform(i, dim + 1u));
        if (!(s.pdf > 0.0f)) {
          continue;
        }
        ++successes;
        double const cosThetaI = std::fabs(n0().dot(s.wi));
        if (cosThetaI < kEnergyCosFloor) {
          continue;
        }
        for (int c = 0; c < 3; ++c) {
          double const ratio = channel(s.f, c) * cosThetaI / s.pdf;
          ratioOk = ratioOk && nearRel(ratio, channel(weights[w], c), kEnergyRelTol);
        }
      }
      check(enoughSuccesses(successes, kDraws), name + ": at least 99.99% of sample draws succeed");
      check(ratioOk, name + ": f * |cos(theta_i)| / pdf equals the weight within 1e-5 relative");

      std::uint32_t const sphereDim =
          kDimEnergySphere + 8u * static_cast<std::uint32_t>(w) + 2u * static_cast<std::uint32_t>(k);
      std::vector<double> terms[3];
      for (auto& t : terms) {
        t.resize(kDraws);
      }
      for (std::uint32_t i = 0; i < kDraws; ++i) {
        GMANVector const wi = gman::uniformSphere(uniform(i, sphereDim), uniform(i, sphereDim + 1u));
        GMANColor const f = bsdf.eval(wo, wi);
        double const cosThetaI = std::fabs(n0().dot(wi));
        for (int c = 0; c < 3; ++c) {
          terms[c][i] = channel(f, c) * cosThetaI / gman::uniformSpherePdf();
        }
      }
      for (int c = 0; c < 3; ++c) {
        GmanMeanStderr const stat = meanStderr(terms[c]);
        std::printf("info: %s, channel %d: sphere estimate %.6f, sigma %.6f, expected %.6f\n", name.c_str(), c,
                    stat.mean, stat.stderrOfMean, channel(weights[w], c));
        checkNear(stat.mean, channel(weights[w], c), stat.stderrOfMean, kSphereFloor,
                  name + ", channel " + std::to_string(c) +
                      ": the sphere estimate of the integral of eval * |cos(theta_i)| is the weight within 5sigma");
      }
    }
  }
}

// Reciprocity: eval(wo, wi) == eval(wi, wo).
void checkReciprocity() {
  gman::BSDF const closures[] = {singleLobe(n0(), kR), twoLobe()};
  for (std::size_t n = 0; n < 2; ++n) {
    std::uint32_t const dim = kDimReciprocity + 4u * static_cast<std::uint32_t>(n);
    bool ok = true;
    for (std::uint32_t i = 0; i < kPairs; ++i) {
      GMANVector const wo = gman::uniformSphere(uniform(i, dim), uniform(i, dim + 1u));
      GMANVector const wi = gman::uniformSphere(uniform(i, dim + 2u), uniform(i, dim + 3u));
      ok = ok && colorNearRelOrAbs(closures[n].eval(wo, wi), closures[n].eval(wi, wo), kRelTol, kAbsTol);
    }
    check(ok, std::string("reciprocity, ") + (n == 0 ? "R" : "two-lobe") +
                  ": eval(wo, wi) equals eval(wi, wo) within 1e-6 relative or 1e-7 absolute");
  }
}

// R / pi on wo's side, black with pdf 0 on the far side, and a closure
// built at 2 * N0 answering as one built at N0.
void checkSides() {
  gman::BSDF const atN0 = singleLobe(n0(), kR);
  gman::BSDF const atTwiceN0 = singleLobe(n0() * 2.0f, kR);
  GMANColor const expected = scaled(kR, 1.0 / kPiD);

  for (std::size_t k = 0; k < kWoCount; ++k) {
    GMANVector const wo = woAt(kWoAnglesDegrees[k]);
    std::string const name = "sides, " + woName(k);
    std::uint32_t const dim = kDimSides + 2u * static_cast<std::uint32_t>(k);
    RtFloat const cosThetaO = n0().dot(wo);

    bool nearSideOk = true;
    bool farSideOk = true;
    bool normalizedOk = true;
    for (std::uint32_t i = 0; i < kPairs; ++i) {
      GMANVector const wi = gman::uniformSphere(uniform(i, dim), uniform(i, dim + 1u));
      RtFloat const side = cosThetaO * n0().dot(wi);
      GMANColor const f = atN0.eval(wo, wi);
      RtFloat const p = atN0.pdf(wo, wi);
      if (side > 0.0f) {
        nearSideOk = nearSideOk && colorNearRel(f, expected, kRelTol);
      } else if (side < 0.0f) {
        farSideOk = farSideOk && colorExactly(f, kBlack) && p == 0.0f;
      }
      normalizedOk = normalizedOk && colorNearRel(atTwiceN0.eval(wo, wi), f, kRelTol) &&
                     nearRel(atTwiceN0.pdf(wo, wi), p, kRelTol);
    }
    check(nearSideOk, name + ": eval on wo's side is R / pi within 1e-6 relative");
    check(farSideOk, name + ": eval on the far side is black and pdf is 0");
    check(normalizedOk, name + ": a closure built at 2 * N0 answers eval and pdf as one at N0 within 1e-6 relative");
  }
}

struct HistogramResult {
  bool binsOk;
  double massSum;
  double maxDeviationSigma;
};

// Bins wis 8x8 in (cosTheta, phi) about wo's normal and compares each count
// with N * mass, mass integrated from bsdf.pdf by a midpoint rule of
// kMassPoints^2 points per bin.
HistogramResult histogram(gman::BSDF const& bsdf, GMANVector const& wo, std::vector<GMANVector> const& wis) {
  gman::TangentFrame const frame = gman::tangentFrame(woNormal(wo));
  double const dCos = 1.0 / kBins;
  double const dPhi = 2.0 * kPiD / kBins;

  std::vector<double> counts(kBins * kBins, 0.0);
  for (GMANVector const& wi : wis) {
    GMANVector const local = frame.toLocal(wi);
    double phi = std::atan2(static_cast<double>(local.getY()), static_cast<double>(local.getX()));
    if (phi < 0.0) {
      phi += 2.0 * kPiD;
    }
    int const cosBin = std::clamp(static_cast<int>(local.getZ() / dCos), 0, kBins - 1);
    int const phiBin = std::clamp(static_cast<int>(phi / dPhi), 0, kBins - 1);
    counts[cosBin * kBins + phiBin] += 1.0;
  }

  double const n = static_cast<double>(wis.size());
  double const cellArea = (dCos / kMassPoints) * (dPhi / kMassPoints);
  HistogramResult result{true, 0.0, 0.0};
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
          mass += static_cast<double>(bsdf.pdf(wo, frame.toWorld(local))) * cellArea;
        }
      }
      result.massSum += mass;
      double const expected = n * mass;
      double const sigma = std::sqrt(n * mass * (1.0 - mass));
      double const deviation = std::fabs(counts[i * kBins + j] - expected);
      result.binsOk = result.binsOk && deviation <= 5.0 * sigma;
      if (sigma > 0.0) {
        result.maxDeviationSigma = std::max(result.maxDeviationSigma, deviation / sigma);
      }
    }
  }
  return result;
}

// Every success lies strictly on wo's side, has unit length, is no delta,
// carries expectedLobe when one is given, and reports the closure's eval
// and pdf at its wi.
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

// sample's directions match pdf by histogram.
void checkSamplingMatchesPdf() {
  gman::BSDF const bsdf = singleLobe(n0(), kR);
  for (std::size_t k = 0; k < kWoCount; ++k) {
    GMANVector const wo = woAt(kWoAnglesDegrees[k]);
    std::string const name = "sampling, " + woName(k);
    std::uint32_t const dim = kDimHistogram + 2u * static_cast<std::uint32_t>(k);

    std::vector<GMANVector> wis;
    bool successesOk = true;
    for (std::uint32_t i = 0; i < kDraws; ++i) {
      gman::BSDFSample const s = bsdf.sample(wo, uniform(i, dim), uniform(i, dim + 1u));
      if (!(s.pdf > 0.0f)) {
        continue;
      }
      wis.push_back(s.wi);
      successesOk = successesOk && successHolds(bsdf, wo, s, true, 0);
    }
    HistogramResult const h = histogram(bsdf, wo, wis);
    std::printf("info: %s: %zu successes, mass sum %.8f, largest bin deviation %.3f sigma\n", name.c_str(), wis.size(),
                h.massSum, h.maxDeviationSigma);

    check(enoughSuccesses(static_cast<std::uint32_t>(wis.size()), kDraws),
          name + ": at least 99.99% of sample draws succeed");
    check(std::fabs(h.massSum - 1.0) <= kMassSumTol, name + ": the pdf's bin masses sum to 1 within 1e-4");
    check(h.binsOk, name + ": every 8x8 (cosTheta, phi) bin count is within 5sigma of N * mass");
    check(successesOk, name + ": every success lies strictly on wo's side, has unit length, is no delta, has lobe "
                              "index 0 and reports eval and pdf at its wi");
  }
}

// Two lobes at N0, wo at 60 degrees: selection follows the weights, and
// each lobe alone samples the closure's pdf.
void checkTwoLobes() {
  gman::BSDF const bsdf = twoLobe();
  GMANVector const wo = woAt(kWoAnglesDegrees[kTwoLobeWoIndex]);
  GMANColor const sum = summed(kR1, kR2);
  GMANColor const expectedEval = scaled(sum, 1.0 / kPiD);

  check(colorExactly(bsdf.rhoD(), sum), "two lobes: rhoD is R1 + R2");
  check(bsdf.lobeCount() == 2, "two lobes: lobeCount is 2");

  std::vector<GMANVector> byLobe[2];
  std::uint32_t successes = 0;
  bool successesOk = true;
  bool evalOk = true;
  for (std::uint32_t i = 0; i < kDraws; ++i) {
    gman::BSDFSample const s = bsdf.sample(wo, uniform(i, kDimTwoLobe), uniform(i, kDimTwoLobe + 1u));
    if (!(s.pdf > 0.0f)) {
      continue;
    }
    ++successes;
    successesOk = successesOk && s.lobeIndex < 2 && successHolds(bsdf, wo, s, false, 0);
    evalOk = evalOk && colorNearRel(bsdf.eval(wo, s.wi), expectedEval, kRelTol);
    if (s.lobeIndex < 2) {
      byLobe[s.lobeIndex].push_back(s.wi);
    }
  }
  check(enoughSuccesses(successes, kDraws), "two lobes: at least 99.99% of sample draws succeed");
  check(evalOk, "two lobes: eval on wo's side is (R1 + R2) / pi within 1e-6 relative");
  check(successesOk, "two lobes: every success reports the closure's eval and pdf at its wi");

  double const n = static_cast<double>(successes);
  double const fraction = static_cast<double>(byLobe[0].size()) / n;
  double const sigma = std::sqrt(kLobe0Probability * (1.0 - kLobe0Probability) / n);
  std::printf("info: two lobes: lobe 0 fraction %.6f against %.6f, sigma %.6f\n", fraction, kLobe0Probability, sigma);
  check(std::fabs(fraction - kLobe0Probability) <= 5.0 * sigma,
        "two lobes: the fraction of successes from lobe 0 is within 5sigma of 1/6");

  for (std::size_t lobe = 0; lobe < 2; ++lobe) {
    HistogramResult const h = histogram(bsdf, wo, byLobe[lobe]);
    std::printf("info: two lobes, lobe %zu: %zu successes, largest bin deviation %.3f sigma\n", lobe,
                byLobe[lobe].size(), h.maxDeviationSigma);
    check(h.binsOk, "two lobes, lobe " + std::to_string(lobe) +
                        ": its successes alone match the closure's pdf in every 8x8 bin within 5sigma");
  }
}

bool everyDrawFails(gman::BSDF const& bsdf, GMANVector const& wo, std::uint32_t dim) {
  bool ok = true;
  for (std::uint32_t i = 0; i < kFailedDraws; ++i) {
    gman::BSDFSample const s = bsdf.sample(wo, uniform(i, dim), uniform(i, dim + 1u));
    ok = ok && s.pdf == 0.0f && colorExactly(s.f, kBlack);
  }
  return ok;
}

// The clamp, failed draws and capacity.
void checkClampFailuresCapacity() {
  GMANVector const wo = woAt(kWoAnglesDegrees[kTwoLobeWoIndex]);

  gman::BSDF const clamped = singleLobe(n0(), kOutOfRange);
  check(clamped.lobeCount() == 1 && clamped.lobe(0).kind == gman::LobeKind::lambert &&
            colorExactly(clamped.lobe(0).weight, kOutOfRangeClamped),
        "addLambert((1.4, 0.2, -0.3)) yields one lambert lobe of weight exactly (1, 0.2, 0)");

  gman::BSDF const empty(n0());
  check(colorExactly(empty.rhoD(), kBlack), "an empty closure's rhoD is black");
  check(colorExactly(empty.eval(wo, wo), kBlack), "an empty closure's eval is black");
  check(empty.pdf(wo, wo) == 0.0f, "an empty closure's pdf is 0");
  check(everyDrawFails(empty, wo, kDimFailedDraws), "an empty closure's 256 draws all answer pdf 0");

  gman::BSDF const zeroWeight = singleLobe(n0(), kBlack);
  check(everyDrawFails(zeroWeight, wo, kDimFailedDraws + 2u),
        "a closure of one zero-weight lobe answers pdf 0 from 256 draws");

  gman::BSDF const grazing = singleLobe(kAxisNormal, kR);
  check(everyDrawFails(grazing, kTangentWo, kDimFailedDraws + 4u),
        "wo in the tangent plane answers pdf 0 from 256 draws");

  gman::BSDF full(n0());
  bool addsOk = true;
  for (std::size_t i = 0; i < gman::BSDF::kMaxLobes; ++i) {
    try {
      full.addLambert(kR);
    } catch (GMANError const&) {
      addsOk = false;
    }
  }
  check(addsOk && full.lobeCount() == gman::BSDF::kMaxLobes, "kMaxLobes calls to addLambert succeed");

  bool threwLimit = false;
  try {
    full.addLambert(kR);
  } catch (GMANError const& e) {
    threwLimit = e.getCode() == RIE_LIMIT;
  }
  check(threwLimit, "addLambert past kMaxLobes throws GMANError with code RIE_LIMIT");
  check(full.lobeCount() == gman::BSDF::kMaxLobes, "a refused addLambert leaves lobeCount at kMaxLobes");
}

void checkAllocationDelta(bool holds, char const* message) {
#if GMAN_ADDRESS_SANITIZED
  (void)holds;
  std::printf("skip: %s (ASan-built)\n", message);
#else
  check(holds, message);
#endif
}

// Building a full closure and calling eval, pdf and sample make no heap
// allocation. The counter is first shown to see libgman's own: a refused
// addLambert builds its GMANError's message there.
void checkNoAllocation() {
  gman::BSDF full(n0());
  for (std::size_t i = 0; i < gman::BSDF::kMaxLobes; ++i) {
    full.addLambert(kR);
  }
  long const beforeThrow = raybvhAllocationCount();
  try {
    full.addLambert(kR);
  } catch (GMANError const&) {
  }
  long const afterThrow = raybvhAllocationCount();

  GMANVector const normal = n0();
  GMANVector const wo = woAt(kWoAnglesDegrees[kTwoLobeWoIndex]);
  GMANVector const wi = woAt(kWoAnglesDegrees[0]);
  RtFloat u1[kNoAllocCalls];
  RtFloat u2[kNoAllocCalls];
  for (int i = 0; i < kNoAllocCalls; ++i) {
    u1[i] = uniform(static_cast<std::uint32_t>(i), kDimNoAlloc);
    u2[i] = uniform(static_cast<std::uint32_t>(i), kDimNoAlloc + 1u);
  }

  double sink = 0.0;
  long const before = raybvhAllocationCount();
  {
    gman::BSDF bsdf(normal);
    for (std::size_t i = 0; i < gman::BSDF::kMaxLobes; ++i) {
      bsdf.addLambert(kR);
    }
    for (int i = 0; i < kNoAllocCalls; ++i) {
      sink += bsdf.eval(wo, wi).getRed();
    }
    for (int i = 0; i < kNoAllocCalls; ++i) {
      sink += bsdf.pdf(wo, wi);
    }
    for (int i = 0; i < kNoAllocCalls; ++i) {
      sink += bsdf.sample(wo, u1[i], u2[i]).pdf;
    }
  }
  long const after = raybvhAllocationCount();

  check(sink > 0.0, "the counted calls answered");
  checkAllocationDelta(afterThrow > beforeThrow, "the counter sees an allocation libgman makes");
  checkAllocationDelta(after == before,
                       "building a kMaxLobes closure and 1000 calls each of eval, pdf and sample allocate nothing");
}

} // namespace

int main() {
  checkEnergy();
  checkReciprocity();
  checkSides();
  checkSamplingMatchesPdf();
  checkTwoLobes();
  checkClampFailuresCapacity();
  checkNoAllocation();

  return checkSummary("gman::BSDF's Lambert closure conserves energy, is reciprocal and samples its own pdf");
}
