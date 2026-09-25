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
 * A surface shader's BSDF: the base default (one Lambert lobe of Cs, at
 * se.N), albedo answered from the BSDF's rhoD, and matte's Lambert lobe of
 * Kd * Cs. Plugins load through GMANAttributes::setSurface, the path
 * RiSurfaceV takes; two in-file shaders stand in for out-of-tree ones.
 */

#include <cmath>
#include <memory>
#include <string>

#include "check.h"
#include "gmanattributes.h"
#include "gmanbsdf.h"
#include "gmancolor.h"
#include "gmandictionary.h"
#include "gmannormal.h"
#include "gmanparameterlist.h"
#include "gmanshaderenvironment.h"
#include "gmansurfaceshader.h"
#include "gmanvector.h"
#include "ri.h"

namespace {

constexpr double kPiD = 3.14159265358979323846;
constexpr double kTol = 1e-6;

GMANNormal const kUpNormal(0.0f, 0.0f, 1.0f);
GMANColor const kBlack(0.0f, 0.0f, 0.0f);
GMANColor const kWhite(1.0f, 1.0f, 1.0f);
GMANColor const kCs(0.5f, 0.4f, 0.3f);
GMANColor const kOutOfRangeCs(1.4f, 0.2f, -0.3f);
GMANColor const kOutOfRangeClamped(1.0f, 0.2f, 0.0f);

GMANColor const kR1(0.1f, 0.2f, 0.0f);
GMANColor const kR2(0.6f, 0.3f, 0.6f);
GMANColor const kHeavyLobe1(0.7f, 0.7f, 0.7f);
GMANColor const kHeavyLobe2(0.6f, 0.6f, 0.6f);
GMANColor const kHeavySum(1.3f, 1.3f, 1.3f);
// Neither R1 + R2 nor white, so an albedo that reads Cs shows.
GMANColor const kTwoLobeCs(0.2f, 0.2f, 0.2f);

RtFloat const kMatteKd = 0.6f;
RtFloat const kHighKd = 2.0f;
GMANColor const kHighCs(0.9f, 0.9f, 0.9f);
RtFloat const kNegativeKd = -0.3f;
GMANColor const kOpaqueOs(1.0f, 1.0f, 1.0f);
GMANColor const kTranslucentOs(0.2f, 0.2f, 0.2f);
RtFloat const kKaZero = 0.0f;
RtFloat const kKaOne = 1.0f;

// N and Ng disagree, and wo and wi lie on one side of N but on opposite
// sides of Ng.
GMANNormal const kScaledN(0.0f, 0.0f, 2.0f);
GMANNormal const kCrossNg(1.0f, 0.0f, 0.0f);
GMANVector const kWoUnnormalized(-0.5f, 0.0f, 1.0f);
GMANVector const kWiUnnormalized(0.5f, 0.3f, 1.0f);

class Plain : public GMANSurfaceShader {
public:
  GMANColor computeCi(GMANSurfaceEnv const&) const override { return kBlack; }
  GMANColor computeOi(GMANSurfaceEnv const&) const override { return kBlack; }
};

class TwoLobe : public GMANSurfaceShader {
public:
  TwoLobe(GMANColor const& firstWeight, GMANColor const& secondWeight) : first(firstWeight), second(secondWeight) {}

  GMANColor computeCi(GMANSurfaceEnv const&) const override { return kBlack; }
  GMANColor computeOi(GMANSurfaceEnv const&) const override { return kBlack; }

  gman::BSDF bsdf(GMANSurfaceEnv const& se) const override {
    gman::BSDF closure(se.N);
    closure.addLambert(first);
    closure.addLambert(second);
    return closure;
  }

private:
  GMANColor first;
  GMANColor second;
};

GMANVector normalized(GMANVector v) {
  v.normalize();
  return v;
}

bool colorExactly(GMANColor const& a, GMANColor const& b) {
  return a.getRed() == b.getRed() && a.getGreen() == b.getGreen() && a.getBlue() == b.getBlue();
}

bool colorNear(GMANColor const& a, GMANColor const& b, double tol) {
  return std::fabs(a.getRed() - b.getRed()) <= tol && std::fabs(a.getGreen() - b.getGreen()) <= tol &&
         std::fabs(a.getBlue() - b.getBlue()) <= tol;
}

bool nearRel(double value, double expected, double rel) {
  return std::fabs(value - expected) <= rel * std::fabs(expected);
}

bool colorNearRel(GMANColor const& a, GMANColor const& b, double rel) {
  return nearRel(a.getRed(), b.getRed(), rel) && nearRel(a.getGreen(), b.getGreen(), rel) &&
         nearRel(a.getBlue(), b.getBlue(), rel);
}

GMANColor kdCs(RtFloat kd, GMANColor const& cs) {
  return GMANColor(kd * cs.getRed(), kd * cs.getGreen(), kd * cs.getBlue());
}

// One lambert lobe of exactly weight.
bool isLambertOf(gman::BSDF const& closure, GMANColor const& weight) {
  return closure.lobeCount() == 1 && closure.lobe(0).kind == gman::LobeKind::lambert &&
         colorExactly(closure.lobe(0).weight, weight);
}

// The same lobes, weights exactly equal.
bool sameClosure(gman::BSDF const& a, gman::BSDF const& b) {
  if (a.lobeCount() != b.lobeCount()) {
    return false;
  }
  for (std::size_t i = 0; i < a.lobeCount(); ++i) {
    if (a.lobe(i).kind != b.lobe(i).kind || !colorExactly(a.lobe(i).weight, b.lobe(i).weight)) {
      return false;
    }
  }
  return true;
}

GMANSurfaceEnv envWith(GMANColor const& cs, GMANColor const& os) {
  GMANSurfaceEnv se;
  se.Cs = cs;
  se.Os = os;
  se.N = kUpNormal;
  se.Ng = kUpNormal;
  return se;
}

GMANParameterList floatParam(RtToken token, RtFloat value) {
  static GMANDictionary dictionary;
  RtToken tokens[1] = {token};
  RtPointer parms[1] = {&value};
  return GMANParameterList(dictionary, 1, tokens, parms);
}

GMANParameterList kdAndKaParam(RtFloat kd, RtFloat ka) {
  static GMANDictionary dictionary;
  RtToken tokens[2] = {RI_KD, RI_KA};
  RtPointer parms[2] = {&kd, &ka};
  return GMANParameterList(dictionary, 2, tokens, parms);
}

// A shader loaded through setSurface; null when the plugin failed to load.
std::shared_ptr<GMANSurfaceShader const> loadSurface(std::string const& name, GMANParameterList const& params) {
  GMANAttributes attributes;
  attributes.setSurface(name, params);
  return attributes.getSurface(0.0);
}

// The base default: one Lambert lobe of Cs, clamped, and albedo equal to
// it; a shipped shader overriding albedo alone inherits it.
void checkDefault() {
  Plain const plain;
  GMANSurfaceEnv const se = envWith(kOutOfRangeCs, kOpaqueOs);
  check(isLambertOf(plain.bsdf(se), kOutOfRangeClamped),
        "default: bsdf at Cs = (1.4, 0.2, -0.3) is one lambert lobe of weight exactly (1, 0.2, 0)");
  check(colorExactly(plain.albedo(se), kOutOfRangeClamped), "default: albedo at Cs = (1.4, 0.2, -0.3) is (1, 0.2, 0)");

  auto const plastic = loadSurface(RI_PLASTIC, GMANParameterList());
  check(plastic != nullptr, "default: plastic loads through setSurface");
  if (plastic != nullptr) {
    check(isLambertOf(plastic->bsdf(envWith(kCs, kOpaqueOs)), kCs),
          "default: plastic at default parameters answers one lambert lobe of weight Cs");
  }
}

// albedo is the BSDF's rhoD, clamped; rhoD itself stays unclamped.
void checkAlbedoFollowsBsdf() {
  GMANSurfaceEnv const se = envWith(kTwoLobeCs, kOpaqueOs);
  GMANColor sum = kR1;
  sum += kR2;

  TwoLobe const light(kR1, kR2);
  check(colorNear(light.albedo(se), sum, kTol), "albedo: a two-lobe bsdf's albedo is R1 + R2 within 1e-6");

  TwoLobe const heavy(kHeavyLobe1, kHeavyLobe2);
  check(colorExactly(heavy.albedo(se), kWhite), "albedo: lobes summing past 1 answer albedo exactly (1, 1, 1)");
  check(colorNear(heavy.bsdf(se).rhoD(), kHeavySum, kTol),
        "albedo: the same closure's rhoD is (1.3, 1.3, 1.3) within 1e-6, unclamped");
}

// matte: one Lambert lobe of Kd * Cs, clamped, with Os and Ka left out.
void checkMatte() {
  auto const matte = loadSurface(RI_MATTE, floatParam(RI_KD, kMatteKd));
  check(matte != nullptr, "matte: loads through setSurface");
  if (matte == nullptr) {
    return;
  }
  GMANSurfaceEnv const se = envWith(kCs, kOpaqueOs);
  gman::BSDF const closure = matte->bsdf(se);
  check(closure.lobeCount() == 1 && closure.lobe(0).kind == gman::LobeKind::lambert &&
            colorNear(closure.lobe(0).weight, kdCs(kMatteKd, kCs), kTol),
        "matte: bsdf is one lambert lobe of Kd * Cs within 1e-6");
  check(colorExactly(matte->albedo(se), closure.rhoD()), "matte: albedo equals its closure's rhoD exactly");

  auto const high = loadSurface(RI_MATTE, floatParam(RI_KD, kHighKd));
  check(high != nullptr && isLambertOf(high->bsdf(envWith(kHighCs, kOpaqueOs)), kWhite),
        "matte: Kd = 2 at Cs = (0.9, 0.9, 0.9) clamps the weight to (1, 1, 1)");

  auto const negative = loadSurface(RI_MATTE, floatParam(RI_KD, kNegativeKd));
  check(negative != nullptr && isLambertOf(negative->bsdf(se), kBlack), "matte: Kd = -0.3 clamps the weight to black");

  check(sameClosure(matte->bsdf(envWith(kCs, kOpaqueOs)), matte->bsdf(envWith(kCs, kTranslucentOs))),
        "matte: the closure is identical at Os = (1, 1, 1) and (0.2, 0.2, 0.2)");

  auto const kaZero = loadSurface(RI_MATTE, kdAndKaParam(kMatteKd, kKaZero));
  auto const kaOne = loadSurface(RI_MATTE, kdAndKaParam(kMatteKd, kKaOne));
  check(kaZero != nullptr && kaOne != nullptr && sameClosure(kaZero->bsdf(se), kaOne->bsdf(se)),
        "matte: the closure is identical at Ka = 0 and Ka = 1");
}

// matte builds at se.N, normalized, never at se.Ng.
void checkMatteNormal() {
  auto const matte = loadSurface(RI_MATTE, floatParam(RI_KD, kMatteKd));
  check(matte != nullptr, "matte normal: loads through setSurface");
  if (matte == nullptr) {
    return;
  }
  GMANSurfaceEnv se = envWith(kCs, kOpaqueOs);
  se.N = kScaledN;
  se.Ng = kCrossNg;

  GMANVector const wo = normalized(kWoUnnormalized);
  GMANVector const wi = normalized(kWiUnnormalized);
  GMANVector const unitN = normalized(kScaledN);
  gman::BSDF const closure = matte->bsdf(se);

  GMANColor expectedEval = kdCs(kMatteKd, kCs);
  expectedEval.scale(static_cast<RtFloat>(1.0 / kPiD));
  double const expectedPdf = unitN.dot(wi) / kPiD;

  check(colorNearRel(closure.eval(wo, wi), expectedEval, kTol),
        "matte normal: eval is Kd * Cs / pi within 1e-6 relative, wo and wi straddling Ng");
  check(nearRel(closure.pdf(wo, wi), expectedPdf, kTol),
        "matte normal: pdf is cos(theta_i) / pi against the unit N within 1e-6 relative");
}

} // namespace

int main() {
  checkDefault();
  checkAlbedoFollowsBsdf();
  checkMatte();
  checkMatteNormal();

  return checkSummary("a surface's BSDF: the Lambert-of-Cs default, albedo from rhoD, and matte's Kd * Cs");
}
