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
 * gman's direction warps, their pdfs and the tangent frame: each warp
 * driven by gman::sample2D, checked for domain, for sample/pdf agreement
 * by histogram, for the Monte Carlo identity its pdf makes exact, and for
 * its closed-form pdf values.
 */

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "check.h"
#include "gmansampling.h"

namespace {

constexpr std::uint32_t kSeed = 98765u;
constexpr RtFloat kPi = 3.14159265358979323846f;
constexpr double kPiD = 3.14159265358979323846;
constexpr int kBins = 8;

struct MeanStderr {
  double mean;
  double stderrOfMean;
};

MeanStderr meanStderr(std::vector<double> const& values) {
  double sum = 0.0;
  for (double v : values) {
    sum += v;
  }
  double const mean = sum / static_cast<double>(values.size());

  double sqSum = 0.0;
  for (double v : values) {
    sqSum += (v - mean) * (v - mean);
  }
  double const variance = sqSum / static_cast<double>(values.size() - 1);
  double const stderrOfMean = std::sqrt(variance / static_cast<double>(values.size()));
  return {mean, stderrOfMean};
}

void checkNear(double value, double expected, double sigma, double floor, std::string const& what) {
  double const tolerance = std::max(5.0 * sigma, floor);
  check(std::fabs(value - expected) <= tolerance, what);
}

RtFloat azimuth(GMANVector const& d) {
  RtFloat phi = std::atan2(d.getY(), d.getX());
  if (phi < 0.0f) {
    phi += 2.0f * kPi;
  }
  return phi;
}

// 1. Domain: every warp is unit length; hemisphere/sphere/cone stay on
// their side of the frame; concentricDisk stays inside the unit disk.
void checkDomain() {
  constexpr std::uint32_t kN = 4096u;
  bool cosineUnit = true, cosineZ = true;
  bool hemiUnit = true, hemiZ = true;
  bool sphereUnit = true;
  bool diskInside = true;

  for (std::uint32_t s = 0; s < kN; ++s) {
    gman::Sample2D const u = gman::sample2D(kSeed, 0, 0, s, kN, 0u);

    GMANVector const c = gman::cosineHemisphere(u.u1, u.u2);
    RtFloat const cLen = std::sqrt(c.getX() * c.getX() + c.getY() * c.getY() + c.getZ() * c.getZ());
    cosineUnit = cosineUnit && std::fabs(cLen - 1.0f) <= 1e-5f;
    cosineZ = cosineZ && c.getZ() >= 0.0f;

    GMANVector const h = gman::uniformHemisphere(u.u1, u.u2);
    RtFloat const hLen = std::sqrt(h.getX() * h.getX() + h.getY() * h.getY() + h.getZ() * h.getZ());
    hemiUnit = hemiUnit && std::fabs(hLen - 1.0f) <= 1e-5f;
    hemiZ = hemiZ && h.getZ() >= 0.0f;

    GMANVector const sph = gman::uniformSphere(u.u1, u.u2);
    RtFloat const sphLen = std::sqrt(sph.getX() * sph.getX() + sph.getY() * sph.getY() + sph.getZ() * sph.getZ());
    sphereUnit = sphereUnit && std::fabs(sphLen - 1.0f) <= 1e-5f;

    gman::Point2D const disk = gman::concentricDisk(u.u1, u.u2);
    diskInside = diskInside && (disk.x * disk.x + disk.y * disk.y) <= 1.0f + 1e-5f;
  }
  check(cosineUnit, "cosineHemisphere returns unit length within 1e-5");
  check(cosineZ, "cosineHemisphere returns z >= 0");
  check(hemiUnit, "uniformHemisphere returns unit length within 1e-5");
  check(hemiZ, "uniformHemisphere returns z >= 0");
  check(sphereUnit, "uniformSphere returns unit length within 1e-5");
  check(diskInside, "concentricDisk stays inside the unit disk");

  for (RtFloat cosThetaMax : {0.9f, 0.0f, -0.5f}) {
    bool coneUnit = true, coneZ = true;
    for (std::uint32_t s = 0; s < kN; ++s) {
      gman::Sample2D const u = gman::sample2D(kSeed, 1, 0, s, kN, 0u);
      GMANVector const cone = gman::uniformCone(u.u1, u.u2, cosThetaMax);
      RtFloat const len = std::sqrt(cone.getX() * cone.getX() + cone.getY() * cone.getY() + cone.getZ() * cone.getZ());
      coneUnit = coneUnit && std::fabs(len - 1.0f) <= 1e-5f;
      coneZ = coneZ && cone.getZ() >= cosThetaMax - 1e-6f;
    }
    check(coneUnit, "uniformCone returns unit length within 1e-5, cosThetaMax=" + std::to_string(cosThetaMax));
    check(coneZ, "uniformCone returns z >= cosThetaMax, cosThetaMax=" + std::to_string(cosThetaMax));
  }
}

// 2. Sample and pdf agree by histogram: an 8x8 joint (cosTheta, phi)
// histogram against the pdf's analytic mass per band, times 1/8 for phi.
template <class Warp, class MassFn>
void checkJointHistogram(std::string const& name, Warp warp, RtFloat cosMin, RtFloat cosMax, MassFn massInBand,
                         std::uint32_t dim) {
  constexpr std::uint32_t kN = 1u << 16;
  int hist[kBins][kBins] = {};

  for (std::uint32_t s = 0; s < kN; ++s) {
    gman::Sample2D const u = gman::sample2D(kSeed, 2, 0, s, kN, dim);
    GMANVector const d = warp(u.u1, u.u2);
    RtFloat const cosTheta = d.getZ();
    RtFloat const phi = azimuth(d);

    int cosBin = static_cast<int>((cosTheta - cosMin) / (cosMax - cosMin) * kBins);
    cosBin = std::min(std::max(cosBin, 0), kBins - 1);
    int phiBin = static_cast<int>(phi / (2.0f * kPi) * kBins);
    phiBin = std::min(std::max(phiBin, 0), kBins - 1);
    hist[cosBin][phiBin]++;
  }

  bool ok = true;
  for (int i = 0; i < kBins; ++i) {
    double const a = cosMin + (cosMax - cosMin) * i / kBins;
    double const b = cosMin + (cosMax - cosMin) * (i + 1) / kBins;
    double const p = massInBand(a, b) / kBins;
    double const sigma = std::sqrt(std::max(p, 0.0) * (1.0 - p) / static_cast<double>(kN));
    for (int j = 0; j < kBins; ++j) {
      double const fraction = static_cast<double>(hist[i][j]) / static_cast<double>(kN);
      if (std::fabs(fraction - p) > std::max(5.0 * sigma, 1e-4)) {
        ok = false;
      }
    }
  }
  check(ok, name + ": 8x8 (cosTheta, phi) histogram matches the pdf's analytic mass within 5sigma");
}

void checkConcentricDiskHistogram() {
  constexpr std::uint32_t kN = 1u << 16;
  int hist[kBins][kBins] = {};

  for (std::uint32_t s = 0; s < kN; ++s) {
    gman::Sample2D const u = gman::sample2D(kSeed, 3, 0, s, kN, 0u);
    gman::Point2D const d = gman::concentricDisk(u.u1, u.u2);
    double const r2 = static_cast<double>(d.x) * d.x + static_cast<double>(d.y) * d.y;
    RtFloat phi = std::atan2(d.y, d.x);
    if (phi < 0.0f) {
      phi += 2.0f * kPi;
    }

    int r2Bin = std::min(std::max(static_cast<int>(r2 * kBins), 0), kBins - 1);
    int phiBin = std::min(std::max(static_cast<int>(phi / (2.0f * kPi) * kBins), 0), kBins - 1);
    hist[r2Bin][phiBin]++;
  }

  // Uniform by area: r^2 is uniform on [0, 1) and phi is uniform on
  // [0, 2*pi), independent, so every one of the 64 joint bins gets an
  // equal share.
  bool ok = true;
  double const p = 1.0 / (kBins * kBins);
  double const sigma = std::sqrt(p * (1.0 - p) / static_cast<double>(kN));
  for (int i = 0; i < kBins; ++i) {
    for (int j = 0; j < kBins; ++j) {
      double const fraction = static_cast<double>(hist[i][j]) / static_cast<double>(kN);
      if (std::fabs(fraction - p) > std::max(5.0 * sigma, 1e-4)) {
        ok = false;
      }
    }
  }
  check(ok, "concentricDisk: 8x8 (r^2, phi) histogram is uniform within 5sigma");
}

// 3. Monte Carlo identities, accumulated in double.
void checkMonteCarloIdentities() {
  constexpr std::uint32_t kN = 1u << 16;

  std::vector<double> hemiTerms(kN);
  std::vector<double> sphereTerms(kN);
  std::vector<double> coneTerms(kN);
  constexpr RtFloat kConeC = 0.5f;

  for (std::uint32_t s = 0; s < kN; ++s) {
    gman::Sample2D const uh = gman::sample2D(kSeed, 4, 0, s, kN, 0u);
    GMANVector const h = gman::uniformHemisphere(uh.u1, uh.u2);
    hemiTerms[s] = static_cast<double>(h.getZ()) / static_cast<double>(gman::uniformHemispherePdf());

    gman::Sample2D const us = gman::sample2D(kSeed, 5, 0, s, kN, 0u);
    GMANVector const sp = gman::uniformSphere(us.u1, us.u2);
    sphereTerms[s] = static_cast<double>(std::max(0.0f, sp.getZ())) / static_cast<double>(gman::uniformSpherePdf());

    gman::Sample2D const uc = gman::sample2D(kSeed, 6, 0, s, kN, 0u);
    GMANVector const co = gman::uniformCone(uc.u1, uc.u2, kConeC);
    coneTerms[s] = static_cast<double>(co.getZ()) / static_cast<double>(gman::uniformConePdf(kConeC));
  }

  MeanStderr const hemi = meanStderr(hemiTerms);
  checkNear(hemi.mean, kPiD, hemi.stderrOfMean, 1e-3, "mean of cosTheta/pdf over the uniform hemisphere is pi");

  MeanStderr const sphere = meanStderr(sphereTerms);
  checkNear(sphere.mean, kPiD, sphere.stderrOfMean, 1e-3, "mean of max(0,cosTheta)/pdf over the sphere is pi");

  MeanStderr const cone = meanStderr(coneTerms);
  double const expectedCone = kPiD * (1.0 - static_cast<double>(kConeC) * static_cast<double>(kConeC));
  checkNear(cone.mean, expectedCone, cone.stderrOfMean, 1e-3,
            "mean of cosTheta/pdf over the cone at cosThetaMax=0.5 is pi(1-c^2)");
}

// 4. Pdf values against their closed forms.
void checkPdfValues() {
  check(std::fabs(gman::cosineHemispherePdf(1.0f) - 1.0f / kPi) <= 1e-6f, "cosineHemispherePdf(1) == 1/pi");
  check(std::fabs(gman::cosineHemispherePdf(0.5f) - 0.5f / kPi) <= 1e-6f, "cosineHemispherePdf(0.5) == 0.5/pi");
  check(gman::cosineHemispherePdf(0.0f) == 0.0f, "cosineHemispherePdf(0) == 0");
  check(gman::cosineHemispherePdf(-0.5f) == 0.0f, "cosineHemispherePdf(-0.5) == 0");

  check(std::fabs(gman::uniformHemispherePdf() - 1.0f / (2.0f * kPi)) <= 1e-6f, "uniformHemispherePdf() == 1/(2pi)");
  check(std::fabs(gman::uniformSpherePdf() - 1.0f / (4.0f * kPi)) <= 1e-6f, "uniformSpherePdf() == 1/(4pi)");

  for (RtFloat cosThetaMax : {0.9f, 0.0f, -0.5f}) {
    RtFloat const expected = 1.0f / (2.0f * kPi * (1.0f - cosThetaMax));
    check(std::fabs(gman::uniformConePdf(cosThetaMax) - expected) <= 1e-6f,
          "uniformConePdf(" + std::to_string(cosThetaMax) + ") matches its closed form");
  }
}

// 5. The tangent frame over axis normals, sampled normals and near-pole
// normals.
void checkTangentFrame() {
  std::vector<GMANVector> normals = {
      GMANVector(1, 0, 0), GMANVector(-1, 0, 0), GMANVector(0, 1, 0),           GMANVector(0, -1, 0),
      GMANVector(0, 0, 1), GMANVector(0, 0, -1), GMANVector(1e-7f, 0.0f, 1.0f), GMANVector(0.0f, 1e-7f, -1.0f),
  };
  for (std::uint32_t s = 0; s < 1000u; ++s) {
    gman::Sample2D const u = gman::sample2D(kSeed, 7, 0, s, 1000u, 0u);
    normals.push_back(gman::uniformSphere(u.u1, u.u2));
  }

  std::vector<GMANVector> const probes = {GMANVector(1, 0, 0), GMANVector(0, 1, 0), GMANVector(0.267f, 0.534f, 0.802f)};

  bool unitOrtho = true, crossOk = true, roundTripOk = true;
  for (GMANVector const& n : normals) {
    gman::TangentFrame const frame = gman::tangentFrame(n);

    auto len = [](GMANVector const& v) { return std::sqrt(v.dot(v)); };
    unitOrtho = unitOrtho && std::fabs(len(frame.t) - 1.0f) <= 1e-5f && std::fabs(len(frame.b) - 1.0f) <= 1e-5f;
    unitOrtho = unitOrtho && std::fabs(frame.t.dot(frame.b)) <= 1e-5f && std::fabs(frame.t.dot(frame.n)) <= 1e-5f &&
                std::fabs(frame.b.dot(frame.n)) <= 1e-5f;

    GMANVector const cross = frame.t.cross(frame.b);
    crossOk = crossOk && std::fabs(cross.getX() - frame.n.getX()) <= 1e-5f &&
              std::fabs(cross.getY() - frame.n.getY()) <= 1e-5f && std::fabs(cross.getZ() - frame.n.getZ()) <= 1e-5f;

    for (GMANVector const& v : probes) {
      GMANVector const roundTrip = frame.toLocal(frame.toWorld(v));
      roundTripOk = roundTripOk && std::fabs(roundTrip.getX() - v.getX()) <= 1e-5f &&
                    std::fabs(roundTrip.getY() - v.getY()) <= 1e-5f && std::fabs(roundTrip.getZ() - v.getZ()) <= 1e-5f;
    }
  }
  check(unitOrtho, "tangentFrame: t, b and n are unit and mutually orthogonal within 1e-5");
  check(crossOk, "tangentFrame: t.cross(b) equals n within 1e-5");
  check(roundTripOk, "tangentFrame: toLocal(toWorld(v)) returns v within 1e-5");
}

} // namespace

int main() {
  checkDomain();

  checkJointHistogram(
      "cosineHemisphere", gman::cosineHemisphere, 0.0f, 1.0f, [](double a, double b) { return b * b - a * a; }, 1u);
  checkJointHistogram(
      "uniformHemisphere", gman::uniformHemisphere, 0.0f, 1.0f, [](double a, double b) { return b - a; }, 2u);
  checkJointHistogram(
      "uniformSphere", gman::uniformSphere, -1.0f, 1.0f, [](double a, double b) { return (b - a) / 2.0; }, 3u);
  checkJointHistogram(
      "uniformCone(0.5)", [](RtFloat u1, RtFloat u2) { return gman::uniformCone(u1, u2, 0.5f); }, 0.5f, 1.0f,
      [](double a, double b) { return (b - a) / 0.5; }, 4u);
  checkConcentricDiskHistogram();

  checkMonteCarloIdentities();
  checkPdfValues();
  checkTangentFrame();

  return checkSummary("gman's warps, pdfs and tangent frame hold their contracts");
}
