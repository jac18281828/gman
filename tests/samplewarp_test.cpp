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
 * gman's direction warps, their pdfs and the tangent frame. Domain
 * checks stay stratified, driven by gman::sample2D; every statistical
 * check (histogram, Monte Carlo identity) draws independent inputs, so
 * its 5-sigma standard error is the real one -- 2^16 stratified points
 * from one pixel understate a warp's error by several orders of
 * magnitude.
 */

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "check.h"
#include "gmansampling.h"
#include "samplingstats.h"

namespace {

constexpr std::uint32_t kSeed = 98765u;
constexpr RtFloat kPi = 3.14159265358979323846f;
constexpr double kPiD = 3.14159265358979323846;
constexpr int kBins = 8;

// An independent (u1, u2) draw for sample index: unitFloat(sampleHash(...))
// at two dimensions, not sample2D's stratified pattern, so 5sigma is the
// real standard error of a statistical estimate.
gman::Sample2D independentSample(std::uint32_t index, std::uint32_t dimU1, std::uint32_t dimU2) {
  return {gman::unitFloat(gman::sampleHash(kSeed, 0, 0, index, dimU1)),
          gman::unitFloat(gman::sampleHash(kSeed, 0, 0, index, dimU2))};
}

RtFloat wrapAzimuth(RtFloat y, RtFloat x) {
  RtFloat phi = std::atan2(y, x);
  if (phi < 0.0f) {
    phi += 2.0f * kPi;
  }
  return phi;
}

RtFloat azimuth(GMANVector const& d) { return wrapAzimuth(d.getY(), d.getX()); }

// Domain: cosineHemisphere, uniformHemisphere and uniformSphere return
// unit length and stay on their side of the frame; concentricDisk stays
// inside the unit disk.
void checkHemisphereSphereDiskDomain() {
  constexpr std::uint32_t kN = 4096u;
  bool cosineUnit = true, cosineZ = true;
  bool hemiUnit = true, hemiZ = true;
  bool sphereUnit = true;
  bool diskInside = true;

  for (std::uint32_t s = 0; s < kN; ++s) {
    gman::Sample2D const u = gman::sample2D(kSeed, 0, 0, s, kN, 0u);

    GMANVector const c = gman::cosineHemisphere(u.u1, u.u2);
    cosineUnit = cosineUnit && std::fabs(vectorLength(c) - 1.0f) <= 1e-5f;
    cosineZ = cosineZ && c.getZ() >= 0.0f;

    GMANVector const h = gman::uniformHemisphere(u.u1, u.u2);
    hemiUnit = hemiUnit && std::fabs(vectorLength(h) - 1.0f) <= 1e-5f;
    hemiZ = hemiZ && h.getZ() >= 0.0f;

    GMANVector const sph = gman::uniformSphere(u.u1, u.u2);
    sphereUnit = sphereUnit && std::fabs(vectorLength(sph) - 1.0f) <= 1e-5f;

    gman::Point2D const disk = gman::concentricDisk(u.u1, u.u2);
    diskInside = diskInside && (disk.x * disk.x + disk.y * disk.y) <= 1.0f + 1e-5f;
  }
  check(cosineUnit, "cosineHemisphere returns unit length within 1e-5");
  check(cosineZ, "cosineHemisphere returns z >= 0");
  check(hemiUnit, "uniformHemisphere returns unit length within 1e-5");
  check(hemiZ, "uniformHemisphere returns z >= 0");
  check(sphereUnit, "uniformSphere returns unit length within 1e-5");
  check(diskInside, "concentricDisk stays inside the unit disk");
}

// Domain: uniformCone returns unit length and z >= cosThetaMax exactly,
// at three cosThetaMax values.
void checkConeDomain() {
  constexpr std::uint32_t kN = 4096u;
  RtFloat const u1AtLowerEdge = std::nextafter(1.0f, 0.0f);
  for (RtFloat cosThetaMax : {0.9f, 0.0f, -0.5f}) {
    bool coneUnit = true, coneZ = true;
    for (std::uint32_t s = 0; s < kN; ++s) {
      gman::Sample2D const u = gman::sample2D(kSeed, 1, 0, s, kN, 0u);
      GMANVector const cone = gman::uniformCone(u.u1, u.u2, cosThetaMax);
      coneUnit = coneUnit && std::fabs(vectorLength(cone) - 1.0f) <= 1e-5f;
      coneZ = coneZ && cone.getZ() >= cosThetaMax;
    }
    // u1 at its 1-exclusive bound: the domain's own lower edge, where z
    // sits closest to cosThetaMax and a small deviation below it is
    // easiest to miss at coarser, stratified sampling.
    GMANVector const edge = gman::uniformCone(u1AtLowerEdge, 0.25f, cosThetaMax);
    coneZ = coneZ && edge.getZ() >= cosThetaMax;
    check(coneUnit, "uniformCone returns unit length within 1e-5, cosThetaMax=" + std::to_string(cosThetaMax));
    check(coneZ, "uniformCone returns z >= cosThetaMax exactly, cosThetaMax=" + std::to_string(cosThetaMax));
  }
}

// Sample and pdf agree by histogram: 8x8 joint (cosTheta, phi) bins
// against the pdf's analytic mass per band, times 1/8 for phi.
template <class Warp, class MassFn>
void checkJointHistogram(std::string const& name, Warp warp, RtFloat cosMin, RtFloat cosMax, MassFn massInBand,
                         std::uint32_t dimU1, std::uint32_t dimU2) {
  constexpr std::uint32_t kN = 1u << 16;
  int hist[kBins][kBins] = {};

  for (std::uint32_t s = 0; s < kN; ++s) {
    gman::Sample2D const u = independentSample(s, dimU1, dimU2);
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
      ok = ok && std::fabs(fraction - p) <= std::max(5.0 * sigma, 1e-4);
    }
  }
  check(ok, name + ": 8x8 (cosTheta, phi) histogram matches the pdf's analytic mass within 5sigma");
}

void checkConcentricDiskHistogram() {
  constexpr std::uint32_t kN = 1u << 16;
  int hist[kBins][kBins] = {};

  for (std::uint32_t s = 0; s < kN; ++s) {
    gman::Sample2D const u = independentSample(s, 30u, 31u);
    gman::Point2D const d = gman::concentricDisk(u.u1, u.u2);
    double const r2 = static_cast<double>(d.x) * d.x + static_cast<double>(d.y) * d.y;
    RtFloat const phi = wrapAzimuth(d.y, d.x);

    int const r2Bin = std::min(std::max(static_cast<int>(r2 * kBins), 0), kBins - 1);
    int const phiBin = std::min(std::max(static_cast<int>(phi / (2.0f * kPi) * kBins), 0), kBins - 1);
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
      ok = ok && std::fabs(fraction - p) <= std::max(5.0 * sigma, 1e-4);
    }
  }
  check(ok, "concentricDisk: 8x8 (r^2, phi) histogram is uniform within 5sigma");
}

// Monte Carlo identities, accumulated in double, over independent draws.
void checkMonteCarloIdentities() {
  constexpr std::uint32_t kN = 1u << 16;
  std::vector<double> hemiTerms(kN), sphereTerms(kN), coneTerms(kN);
  constexpr RtFloat kConeC = 0.5f;

  for (std::uint32_t s = 0; s < kN; ++s) {
    gman::Sample2D const uh = independentSample(s, 20u, 21u);
    GMANVector const h = gman::uniformHemisphere(uh.u1, uh.u2);
    hemiTerms[s] = static_cast<double>(h.getZ()) / static_cast<double>(gman::uniformHemispherePdf());

    gman::Sample2D const us = independentSample(s, 22u, 23u);
    GMANVector const sp = gman::uniformSphere(us.u1, us.u2);
    sphereTerms[s] = static_cast<double>(std::max(0.0f, sp.getZ())) / static_cast<double>(gman::uniformSpherePdf());

    gman::Sample2D const uc = independentSample(s, 24u, 25u);
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

// Pdf values against their closed forms.
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

// The tangent frame over axis normals, sampled normals and near-pole
// normals: t, b and n unit and orthogonal, t.cross(b) == n, and
// toLocal(toWorld(v)) recovers v.
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

    unitOrtho = unitOrtho && std::fabs(vectorLength(frame.t) - 1.0f) <= 1e-5f &&
                std::fabs(vectorLength(frame.b) - 1.0f) <= 1e-5f;
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
  checkHemisphereSphereDiskDomain();
  checkConeDomain();

  checkJointHistogram(
      "cosineHemisphere", gman::cosineHemisphere, 0.0f, 1.0f, [](double a, double b) { return b * b - a * a; }, 1u, 2u);
  checkJointHistogram(
      "uniformHemisphere", gman::uniformHemisphere, 0.0f, 1.0f, [](double a, double b) { return b - a; }, 3u, 4u);
  checkJointHistogram(
      "uniformSphere", gman::uniformSphere, -1.0f, 1.0f, [](double a, double b) { return (b - a) / 2.0; }, 5u, 6u);
  checkJointHistogram(
      "uniformCone(0.5)", [](RtFloat u1, RtFloat u2) { return gman::uniformCone(u1, u2, 0.5f); }, 0.5f, 1.0f,
      [](double a, double b) { return (b - a) / 0.5; }, 7u, 8u);
  checkConcentricDiskHistogram();

  checkMonteCarloIdentities();
  checkPdfValues();
  checkTangentFrame();

  return checkSummary("gman's warps, pdfs and tangent frame hold their contracts");
}
