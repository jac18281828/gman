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
 * se.N), albedo answered from the BSDF's rhoD, matte's Lambert lobe of
 * Kd * Cs, the GGX closures of plastic, paintedplastic, metal and
 * shinymetal (their headroom-fitted specular lobe, their roughness-to-
 * alpha mapping, and the inputs each reads), and mirror's and glass's
 * delta closures, glass's orientation by Ng included. Plugins load through
 * GMANAttributes::setSurface, the path RiSurfaceV takes; countingshader
 * stands in for a plugin without a bsdf override, and Plain and TwoLobe
 * in-file for out-of-tree ones.
 */

#include <cmath>
#include <cstdint>
#include <memory>
#include <string>

#include "check.h"
#include "checkertexture.h"
#include "gmanattributes.h"
#include "gmanbsdf.h"
#include "gmancolor.h"
#include "gmandictionary.h"
#include "gmannormal.h"
#include "gmanparameterlist.h"
#include "gmansampling.h"
#include "gmanshaderenvironment.h"
#include "gmansurfaceshader.h"
#include "gmanvector.h"
#include "ri.h"

namespace {

constexpr double kPiD = 3.14159265358979323846;
constexpr double kTol = 1e-6;

constexpr std::uint32_t kSeed = 20260927u;
constexpr std::uint32_t kOrientDraws = 256u;
constexpr RtFloat kGlassIor = 1.5f;

RtFloat uniform(std::uint32_t index, std::uint32_t dimension) {
  return gman::unitFloat(gman::sampleHash(kSeed, 0, 0, index, dimension));
}

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

// B: the four GGX shaders.
RtFloat const kDefaultRoughness = 0.1f;
GMANColor const kDefaultSpecularColor(1.0f, 1.0f, 1.0f);
GMANColor const kMetalShinyCs(0.9f, 0.6f, 0.3f);
std::string const kCheckerTextureName = "shaderbsdf_checker.tif";

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

double channel(GMANColor const& c, int i) {
  if (i == 0) {
    return c.getRed();
  }
  return i == 1 ? c.getGreen() : c.getBlue();
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

// Clamps each channel to [0, 1], as gmanshaderparams.h's clampAlbedo does.
GMANColor clampToUnit(GMANColor const& c) {
  auto const clampChannel = [](RtFloat v) { return v > 0.0f ? (v < 1.0f ? v : 1.0f) : 0.0f; };
  return GMANColor(clampChannel(c.getRed()), clampChannel(c.getGreen()), clampChannel(c.getBlue()));
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

// The same lobes, weights, alphas and etas exactly equal.
bool sameClosureExactly(gman::BSDF const& a, gman::BSDF const& b) {
  if (a.lobeCount() != b.lobeCount()) {
    return false;
  }
  for (std::size_t i = 0; i < a.lobeCount(); ++i) {
    if (a.lobe(i).kind != b.lobe(i).kind || !colorExactly(a.lobe(i).weight, b.lobe(i).weight) ||
        a.lobe(i).alpha != b.lobe(i).alpha || a.lobe(i).eta != b.lobe(i).eta) {
      return false;
    }
  }
  return true;
}

// One mirror lobe of weight near, within tol.
bool isMirrorOf(gman::BSDF const& closure, GMANColor const& weight, double tol) {
  return closure.lobeCount() == 1 && closure.lobe(0).kind == gman::LobeKind::mirror &&
         colorNear(closure.lobe(0).weight, weight, tol);
}

// One dielectric lobe of weight and eta exactly.
bool isDielectricOf(gman::BSDF const& closure, GMANColor const& weight, RtFloat eta) {
  return closure.lobeCount() == 1 && closure.lobe(0).kind == gman::LobeKind::dielectric &&
         colorExactly(closure.lobe(0).weight, weight) && closure.lobe(0).eta == eta;
}

bool vectorNear(GMANVector const& a, GMANVector const& b, double tol) {
  return std::fabs((double)a.getX() - (double)b.getX()) <= tol &&
         std::fabs((double)a.getY() - (double)b.getY()) <= tol && std::fabs((double)a.getZ() - (double)b.getZ()) <= tol;
}

// The roughness-to-alpha mapping, the test's own copy of the one
// shaders/gmanshaderparams.h defines: 0 for r <= 0 or NaN, which
// BSDF::addGGX raises to kMinGGXAlpha.
RtFloat alphaFromRoughness(RtFloat roughness) {
  if (!(roughness > 0.0f)) {
    return 0.0f;
  }
  double const cosSquaredHalfMax = std::pow(4.0, -(double)roughness);
  return (RtFloat)std::sqrt((1.0 - cosSquaredHalfMax) / (std::sqrt(2.0) - cosSquaredHalfMax));
}

bool lobeNear(gman::Lobe const& lobe, gman::LobeKind kind, GMANColor const& weight, RtFloat alpha, double tol) {
  return lobe.kind == kind && colorNear(lobe.weight, weight, tol) && std::fabs(lobe.alpha - alpha) <= tol;
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

GMANParameterList plasticParams(RtFloat kd = 0.5f, RtFloat ks = 0.5f,
                                GMANColor const& specularcolor = kDefaultSpecularColor,
                                RtFloat roughness = kDefaultRoughness) {
  static GMANDictionary dictionary;
  RtToken tokens[4] = {RI_KD, RI_KS, RI_SPECULARCOLOR, RI_ROUGHNESS};
  RtFloat sc[3] = {specularcolor.getRed(), specularcolor.getGreen(), specularcolor.getBlue()};
  RtPointer parms[4] = {&kd, &ks, sc, &roughness};
  return GMANParameterList(dictionary, 4, tokens, parms);
}

GMANParameterList paintedplasticParams(RtFloat kd, RtFloat ks, GMANColor const& specularcolor, RtFloat roughness,
                                       std::string const& texturename) {
  static GMANDictionary dictionary;
  RtToken tokens[5] = {RI_KD, RI_KS, RI_SPECULARCOLOR, RI_ROUGHNESS, RI_TEXTURENAME};
  RtFloat sc[3] = {specularcolor.getRed(), specularcolor.getGreen(), specularcolor.getBlue()};
  char* textureCStr = const_cast<char*>(texturename.c_str());
  RtPointer parms[5] = {&kd, &ks, sc, &roughness, &textureCStr};
  return GMANParameterList(dictionary, 5, tokens, parms);
}

GMANParameterList metalParams(RtFloat ks = 1.0f, GMANColor const& specularcolor = kDefaultSpecularColor,
                              RtFloat roughness = kDefaultRoughness) {
  static GMANDictionary dictionary;
  RtToken tokens[3] = {RI_KS, RI_SPECULARCOLOR, RI_ROUGHNESS};
  RtFloat sc[3] = {specularcolor.getRed(), specularcolor.getGreen(), specularcolor.getBlue()};
  RtPointer parms[3] = {&ks, sc, &roughness};
  return GMANParameterList(dictionary, 3, tokens, parms);
}

GMANParameterList shinymetalParams(RtFloat ks = 1.0f, RtFloat kr = 1.0f, RtFloat roughness = kDefaultRoughness) {
  static GMANDictionary dictionary;
  RtToken tokens[3] = {RI_KS, RI_KR, RI_ROUGHNESS};
  RtPointer parms[3] = {&ks, &kr, &roughness};
  return GMANParameterList(dictionary, 3, tokens, parms);
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
}

// countingshader stands in for a plugin without a bsdf override. It is the
// only loaded plugin that still inherits the base default, since every
// shipped shader now overrides bsdf; the in-binary Plain subclass proves
// the same default without crossing the shared-object boundary.
void checkLoadedDefault() {
  auto const counting = loadSurface("countingshader", GMANParameterList());
  check(counting != nullptr, "default: countingshader loads through setSurface");
  if (counting != nullptr) {
    check(isLambertOf(counting->bsdf(envWith(kCs, kOpaqueOs)), kCs),
          "default: countingshader at default parameters answers one lambert lobe of weight Cs");
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

// plastic at default parameters, and its alpha at roughness's edges.
void checkPlasticDefaults() {
  auto const plastic = loadSurface("plastic", plasticParams());
  check(plastic != nullptr, "plastic: loads through setSurface");
  if (plastic == nullptr) {
    return;
  }
  GMANSurfaceEnv const se = envWith(kCs, kOpaqueOs);
  gman::BSDF const closure = plastic->bsdf(se);
  GMANColor const expectedLambert(0.25f, 0.2f, 0.15f);
  GMANColor const expectedGgx(0.5f, 0.5f, 0.5f);
  RtFloat const expectedAlpha = alphaFromRoughness(kDefaultRoughness);
  check(closure.lobeCount() == 2 && lobeNear(closure.lobe(0), gman::LobeKind::lambert, expectedLambert, 0.0f, kTol) &&
            lobeNear(closure.lobe(1), gman::LobeKind::ggx, expectedGgx, expectedAlpha, kTol),
        "plastic: at defaults, lobe 0 is lambert of Kd*Cs = (0.25, 0.2, 0.15) and lobe 1 is ggx of (0.5, 0.5, 0.5) "
        "and alpha 0.487961, each within 1e-6");
  check(colorExactly(plastic->albedo(se), closure.lobe(0).weight), "plastic: albedo equals lobe 0's weight exactly");

  auto const atZero = loadSurface("plastic", plasticParams(0.5f, 0.5f, kDefaultSpecularColor, 0.0f));
  auto const atOne = loadSurface("plastic", plasticParams(0.5f, 0.5f, kDefaultSpecularColor, 1.0f));
  auto const atSmall = loadSurface("plastic", plasticParams(0.5f, 0.5f, kDefaultSpecularColor, 0.01f));
  check(atZero != nullptr && atZero->bsdf(se).lobe(1).alpha == gman::BSDF::kMinGGXAlpha,
        "plastic: at roughness 0, lobe 1's alpha is exactly kMinGGXAlpha");
  check(atOne != nullptr && std::fabs(atOne->bsdf(se).lobe(1).alpha - 0.802628) <= kTol,
        "plastic: at roughness 1, lobe 1's alpha is 0.802628 within 1e-6");
  check(atSmall != nullptr && std::fabs(atSmall->bsdf(se).lobe(1).alpha - 0.1793545) <= kTol,
        "plastic: at roughness 0.01, lobe 1's alpha is 0.1793545 within 1e-6");
}

// plastic's headroom: named cases, the Kd = 2 saturation, and a sweep
// proving lobe 0 stays Kd*Cs and the two lobes never exceed 1 combined.
void checkPlasticHeadroom() {
  struct HeadroomCase {
    RtFloat kd;
    GMANColor cs;
    RtFloat ks;
    GMANColor specularcolor;
    GMANColor lambert;
    GMANColor ggx;
  };
  HeadroomCase const cases[] = {
      {0.8f, GMANColor(1.0f, 0.0f, 0.0f), 0.5f, kDefaultSpecularColor, GMANColor(0.8f, 0.0f, 0.0f),
       GMANColor(0.2f, 0.2f, 0.2f)},
      {0.5f, kWhite, 1.0f, GMANColor(1.0f, 0.5f, 0.0f), GMANColor(0.5f, 0.5f, 0.5f), GMANColor(0.5f, 0.25f, 0.0f)},
  };
  bool namedOk = true;
  for (auto const& c : cases) {
    auto const plastic = loadSurface("plastic", plasticParams(c.kd, c.ks, c.specularcolor, kDefaultRoughness));
    if (plastic == nullptr) {
      namedOk = false;
      continue;
    }
    gman::BSDF const closure = plastic->bsdf(envWith(c.cs, kOpaqueOs));
    namedOk =
        namedOk && colorNear(closure.lobe(0).weight, c.lambert, kTol) && colorNear(closure.lobe(1).weight, c.ggx, kTol);
  }
  check(namedOk, "plastic headroom: the two named cases' lambert and ggx weights match within 1e-6");

  auto const saturated = loadSurface("plastic", plasticParams(2.0f, 0.5f, kDefaultSpecularColor, kDefaultRoughness));
  check(saturated != nullptr, "plastic headroom: the Kd = 2 saturation case loads through setSurface");
  if (saturated == nullptr) {
    return;
  }
  gman::BSDF const saturatedClosure = saturated->bsdf(envWith(kHighCs, kOpaqueOs));
  check(colorExactly(saturatedClosure.lobe(0).weight, kWhite) && colorExactly(saturatedClosure.lobe(1).weight, kBlack),
        "plastic headroom: Kd = 2 at Cs = 0.9 clamps lambert to exactly (1, 1, 1) and zeroes ggx exactly");
  check(colorExactly(saturated->albedo(envWith(kHighCs, kOpaqueOs)), kWhite),
        "plastic headroom: Kd = 2 at Cs = 0.9 answers albedo exactly (1, 1, 1)");

  RtFloat const sweepValues[] = {0.0f, 0.5f, 1.0f, 2.0f};
  GMANColor const csValues[] = {kCs, GMANColor(1.0f, 0.0f, 0.0f), kWhite};
  GMANColor const specularValues[] = {kWhite, GMANColor(1.0f, 0.5f, 0.0f)};
  bool sweepOk = true;
  for (RtFloat kd : sweepValues) {
    for (RtFloat ks : sweepValues) {
      for (auto const& cs : csValues) {
        for (auto const& sc : specularValues) {
          auto const shader = loadSurface("plastic", plasticParams(kd, ks, sc, kDefaultRoughness));
          if (shader == nullptr) {
            sweepOk = false;
            continue;
          }
          gman::BSDF const closure = shader->bsdf(envWith(cs, kOpaqueOs));
          GMANColor const expectedLambert = clampToUnit(kdCs(kd, cs));
          sweepOk = sweepOk && colorNear(closure.lobe(0).weight, expectedLambert, kTol);
          for (int c = 0; c < 3; ++c) {
            sweepOk = sweepOk && channel(closure.lobe(0).weight, c) + channel(closure.lobe(1).weight, c) <= 1.0 + kTol;
          }
        }
      }
    }
  }
  check(sweepOk, "plastic headroom: over Kd, Ks, Cs and specularcolor, lobe 0 is Kd*Cs clamped and the two weights "
                 "sum to at most 1 + 1e-6 per channel");
}

// paintedplastic: an empty texturename degrades to plastic, and a real
// texture sets the headroom texel by texel.
void checkPaintedPlasticGGX() {
  auto const paintedAtDefaults = loadSurface(
      "paintedplastic", paintedplasticParams(0.5f, 0.5f, kDefaultSpecularColor, kDefaultRoughness, std::string()));
  auto const plasticAtDefaults = loadSurface("plastic", plasticParams());
  check(paintedAtDefaults != nullptr && plasticAtDefaults != nullptr &&
            sameClosureExactly(paintedAtDefaults->bsdf(envWith(kCs, kOpaqueOs)),
                               plasticAtDefaults->bsdf(envWith(kCs, kOpaqueOs))),
        "paintedplastic: with texturename empty at plastic's default parameters, its closure equals plastic's lobe "
        "for lobe, in kind, weight and alpha");

  RtFloat const kd = 0.8f;
  GMANColor const cs(1.0f, 0.0f, 0.0f);
  RtFloat const ks = 0.5f;
  auto const painted = loadSurface(
      "paintedplastic", paintedplasticParams(kd, ks, kDefaultSpecularColor, kDefaultRoughness, std::string()));
  auto const plastic = loadSurface("plastic", plasticParams(kd, ks, kDefaultSpecularColor, kDefaultRoughness));
  check(painted != nullptr && plastic != nullptr &&
            sameClosureExactly(painted->bsdf(envWith(cs, kOpaqueOs)), plastic->bsdf(envWith(cs, kOpaqueOs))),
        "paintedplastic: with texturename empty, its closure equals plastic's lobe for lobe, in kind, weight and "
        "alpha");

  RtFloat const texturedKd = 0.8f;
  RtFloat const texturedKs = 0.5f;
  GMANColor const texturedCs(1.0f, 0.5f, 1.0f);
  check(writeCheckerTexture(kCheckerTextureName),
        "paintedplastic: " + kCheckerTextureName + " writes into the test's own working directory");

  GMANSurfaceEnv texturedSe = envWith(texturedCs, kOpaqueOs);
  texturedSe.s = 0.75f;
  texturedSe.t = 0.25f;
  auto const textured =
      loadSurface("paintedplastic", paintedplasticParams(texturedKd, texturedKs, kDefaultSpecularColor,
                                                         kDefaultRoughness, kCheckerTextureName));
  check(textured != nullptr, "paintedplastic: textured shader loads through setSurface");
  if (textured != nullptr) {
    gman::BSDF const closure = textured->bsdf(texturedSe);
    check(colorNear(closure.lobe(0).weight, GMANColor(0.0f, 0.4f, 0.0f), kTol) &&
              colorNear(closure.lobe(1).weight, GMANColor(0.5f, 0.5f, 0.5f), kTol),
          "paintedplastic: at the checker's green texel, lambert (0, 0.4, 0) and ggx (0.5, 0.5, 0.5) within 1e-6");
  }

  auto const untextured =
      loadSurface("paintedplastic", paintedplasticParams(texturedKd, texturedKs, kDefaultSpecularColor,
                                                         kDefaultRoughness, std::string()));
  check(untextured != nullptr, "paintedplastic: untextured shader loads through setSurface");
  if (untextured != nullptr) {
    gman::BSDF const closure = untextured->bsdf(texturedSe);
    check(colorNear(closure.lobe(0).weight, GMANColor(0.8f, 0.4f, 0.8f), kTol) &&
              colorNear(closure.lobe(1).weight, GMANColor(0.2f, 0.2f, 0.2f), kTol),
          "paintedplastic: with the same parameters and an empty texturename, lambert (0.8, 0.4, 0.8) and ggx "
          "(0.2, 0.2, 0.2) within 1e-6, so the texel sets the headroom");
  }
}

// metal: one GGX lobe of Ks*specularcolor*Cs, and a black albedo.
void checkMetalGGX() {
  auto const metal = loadSurface("metal", metalParams());
  check(metal != nullptr, "metal: loads through setSurface");
  if (metal == nullptr) {
    return;
  }
  GMANSurfaceEnv const se = envWith(kMetalShinyCs, kOpaqueOs);
  gman::BSDF const closure = metal->bsdf(se);
  RtFloat const expectedAlpha = alphaFromRoughness(kDefaultRoughness);
  check(closure.lobeCount() == 1 && lobeNear(closure.lobe(0), gman::LobeKind::ggx, kMetalShinyCs, expectedAlpha, kTol),
        "metal: at defaults, one ggx lobe of weight Cs and alpha 0.487961, within 1e-6");

  auto const scaled = loadSurface("metal", metalParams(0.5f, GMANColor(1.0f, 0.5f, 1.0f), kDefaultRoughness));
  check(scaled != nullptr && colorNear(scaled->bsdf(se).lobe(0).weight, GMANColor(0.45f, 0.15f, 0.15f), kTol),
        "metal: at Ks = 0.5, specularcolor = (1, 0.5, 1), weight (0.45, 0.15, 0.15) within 1e-6");

  auto const high = loadSurface("metal", metalParams(2.0f, kDefaultSpecularColor, kDefaultRoughness));
  check(high != nullptr && colorNear(high->bsdf(se).lobe(0).weight, GMANColor(1.0f, 1.0f, 0.6f), kTol),
        "metal: at Ks = 2, weight (1, 1, 0.6) within 1e-6");

  check(colorExactly(metal->albedo(se), kBlack), "metal: albedo is black exactly");
}

// shinymetal: one GGX lobe of Ks*Cs, Kr excluded, and a black albedo.
void checkShinyMetalGGX() {
  auto const shiny = loadSurface("shinymetal", shinymetalParams());
  check(shiny != nullptr, "shinymetal: loads through setSurface");
  if (shiny == nullptr) {
    return;
  }
  GMANSurfaceEnv const se = envWith(kMetalShinyCs, kOpaqueOs);
  gman::BSDF const closure = shiny->bsdf(se);
  RtFloat const expectedAlpha = alphaFromRoughness(kDefaultRoughness);
  check(closure.lobeCount() == 1 && lobeNear(closure.lobe(0), gman::LobeKind::ggx, kMetalShinyCs, expectedAlpha, kTol),
        "shinymetal: at defaults, one ggx lobe of weight Cs and alpha 0.487961, within 1e-6");

  auto const krZero = loadSurface("shinymetal", shinymetalParams(1.0f, 0.0f, kDefaultRoughness));
  auto const krOne = loadSurface("shinymetal", shinymetalParams(1.0f, 1.0f, kDefaultRoughness));
  check(krZero != nullptr && krOne != nullptr && sameClosureExactly(krZero->bsdf(se), krOne->bsdf(se)),
        "shinymetal: the closure is identical at Kr = 0 and Kr = 1");

  check(colorExactly(shiny->albedo(se), kBlack), "shinymetal: albedo is black exactly");
}

// mirror: one mirror lobe of Kr*Cs, clamped, and a black albedo.
void checkMirrorBsdf() {
  auto const mirror = loadSurface("mirror", GMANParameterList());
  check(mirror != nullptr, "mirror: loads through setSurface");
  if (mirror == nullptr) {
    return;
  }
  check(isMirrorOf(mirror->bsdf(envWith(kCs, kOpaqueOs)), kCs, kTol),
        "mirror: at Cs = (0.5, 0.4, 0.3) and default Kr, one mirror lobe of weight Cs within 1e-6");

  auto const lowKr = loadSurface("mirror", floatParam(RI_KR, 0.4f));
  check(lowKr != nullptr && isMirrorOf(lowKr->bsdf(envWith(kCs, kOpaqueOs)), GMANColor(0.2f, 0.16f, 0.12f), kTol),
        "mirror: at Kr = 0.4, weight (0.2, 0.16, 0.12) within 1e-6");

  auto const highKr = loadSurface("mirror", floatParam(RI_KR, 2.0f));
  check(highKr != nullptr && isMirrorOf(highKr->bsdf(envWith(GMANColor(0.9f, 0.6f, 0.3f), kOpaqueOs)),
                                        GMANColor(1.0f, 1.0f, 0.6f), kTol),
        "mirror: at Kr = 2 and Cs = (0.9, 0.6, 0.3), weight (1, 1, 0.6) within 1e-6");

  check(colorExactly(mirror->albedo(envWith(kCs, kOpaqueOs)), kBlack), "mirror: albedo is black exactly");

  check(sameClosureExactly(mirror->bsdf(envWith(kCs, kOpaqueOs)), mirror->bsdf(envWith(kCs, kTranslucentOs))),
        "mirror: the closure at Os = (1, 1, 1) matches the one at Os = (0.2, 0.2, 0.2)");
}

// glass: one dielectric lobe of white weight and eta kGlassIor, and a
// black albedo.
void checkGlassBsdf() {
  auto const glass = loadSurface("glass", GMANParameterList());
  check(glass != nullptr, "glass: loads through setSurface");
  if (glass == nullptr) {
    return;
  }
  gman::BSDF const base = glass->bsdf(envWith(kCs, kOpaqueOs));
  check(isDielectricOf(base, kWhite, kGlassIor),
        "glass: at Cs = (0.5, 0.4, 0.3), one dielectric lobe of weight exactly (1, 1, 1) and eta exactly 1.5");

  check(sameClosureExactly(glass->bsdf(envWith(GMANColor(0.2f, 0.9f, 0.4f), kOpaqueOs)), base),
        "glass: the closure at Cs = (0.2, 0.9, 0.4) matches the one at Cs = (0.5, 0.4, 0.3)");
  check(sameClosureExactly(glass->bsdf(envWith(kCs, kTranslucentOs)), base),
        "glass: the closure at Os = (0.2, 0.2, 0.2) matches the one at Os = (1, 1, 1)");

  check(colorExactly(glass->albedo(envWith(kCs, kOpaqueOs)), kBlack), "glass: albedo is black exactly");

  check(colorNear(base.shadowTransmittance(GMANVector(0.0f, 0.0f, 1.0f)), GMANColor(0.96f, 0.96f, 0.96f), kTol),
        "glass: at N = (0, 0, 1), shadowTransmittance((0, 0, 1)) is 0.96 per channel within 1e-6");
}

// glass orients its dielectric lobe's normal by Ng, negating N where N and
// Ng disagree; Ng zero leaves N as it is.
void checkGlassOrientsByNg() {
  auto const glass = loadSurface("glass", GMANParameterList());
  check(glass != nullptr, "glass orientation: loads through setSurface");
  if (glass == nullptr) {
    return;
  }
  GMANVector const wo(0.0f, 0.0f, 1.0f);
  struct NgCase {
    GMANNormal ng;
    double expectedC;
    char const* label;
  };
  NgCase const cases[] = {
      {GMANNormal(0.0f, 0.0f, 1.0f), 0.4266667, "Ng up"},
      {GMANNormal(0.0f, 0.0f, -1.0f), 2.16, "Ng down"},
      {GMANNormal(), 0.4266667, "Ng zero"},
  };
  for (std::size_t idx = 0; idx < 3; ++idx) {
    NgCase const& c = cases[idx];
    GMANSurfaceEnv se = envWith(kCs, kOpaqueOs);
    se.N = kUpNormal;
    se.Ng = c.ng;
    gman::BSDF const closure = glass->bsdf(se);

    std::uint32_t const dim = 2u * static_cast<std::uint32_t>(idx);
    bool transmitOccurred = false, transmitDirOk = true, transmitCOk = true, reflectPdfOk = true;
    for (std::uint32_t i = 0; i < kOrientDraws; ++i) {
      gman::BSDFSample const s = closure.sample(wo, uniform(i, dim), uniform(i, dim + 1u));
      if (!(s.pdf > 0.0f)) {
        continue;
      }
      if (s.wi.getZ() < 0.0f) {
        transmitOccurred = true;
        transmitDirOk = transmitDirOk && vectorNear(s.wi, GMANVector(0.0f, 0.0f, -1.0f), 1e-5);
        double const cVal = (double)s.f.getRed() * std::fabs((double)s.wi.getZ());
        transmitCOk = transmitCOk && std::fabs(cVal - c.expectedC) <= 1e-5 * c.expectedC;
      } else {
        reflectPdfOk = reflectPdfOk && std::fabs((double)s.pdf - 0.04) <= 1e-5;
      }
    }
    check(transmitOccurred, std::string("glass orientation, ") + c.label + ": a transmitted draw occurs");
    check(transmitDirOk,
          std::string("glass orientation, ") + c.label + ": every transmitted draw has wi = (0, 0, -1) within 1e-5");
    check(transmitCOk,
          std::string("glass orientation, ") + c.label + ": every transmitted draw's c matches within 1e-5 relative");
    check(reflectPdfOk,
          std::string("glass orientation, ") + c.label + ": every reflected draw reports pdf 0.04 within 1e-5");
  }
}

// bsdf and albedo read only se.N, se.Cs (and, for glass, se.Ng) and, for
// paintedplastic, se.s/se.t through se.texture -- never se.I, se.Os, se.P,
// se.lights, the occluder or the tracer. glass's own se.Ng, (1, 0, 0), is
// perpendicular to N in both envs, so the orientation cannot differ
// between them; checkGlassOrientsByNg pins the orientation itself.
void checkInputsOnly() {
  GMANColor const cs(0.5f, 0.4f, 0.3f);
  GMANLight const light(GMAN_LIGHT_POINT, kWhite, GMANPoint(0.0f, 0.0f, 5.0f), GMANVector());

  struct ShaderCase {
    char const* name;
    GMANParameterList params;
  };
  std::vector<ShaderCase> const cases = [] {
    std::vector<ShaderCase> v;
    v.push_back({"plastic", plasticParams()});
    v.push_back(
        {"paintedplastic", paintedplasticParams(0.5f, 0.5f, kDefaultSpecularColor, kDefaultRoughness, std::string())});
    v.push_back({"metal", metalParams()});
    v.push_back({"shinymetal", shinymetalParams()});
    v.push_back({"mirror", GMANParameterList()});
    v.push_back({"glass", GMANParameterList()});
    return v;
  }();

  for (auto const& c : cases) {
    auto const shader = loadSurface(c.name, c.params);
    if (shader == nullptr) {
      check(false, std::string(c.name) + ": inputs only: loads through setSurface");
      continue;
    }
    GMANSurfaceEnv minimal;
    minimal.N = kUpNormal;
    minimal.Cs = cs;

    GMANSurfaceEnv full;
    full.N = kUpNormal;
    full.Cs = cs;
    full.I = GMANVector(0.2f, 0.1f, -1.0f);
    full.Ng = GMANNormal(1.0f, 0.0f, 0.0f);
    full.Os = GMANColor(0.2f, 0.2f, 0.2f);
    full.P = GMANPoint(1.0f, 2.0f, 3.0f);
    full.lights = {&light};

    check(sameClosureExactly(shader->bsdf(minimal), shader->bsdf(full)),
          std::string(c.name) + ": inputs only: bsdf matches lobe count, kind, weight, alpha and eta across both "
                                "envs");
    check(colorExactly(shader->albedo(minimal), shader->albedo(full)),
          std::string(c.name) + ": inputs only: albedo matches across both envs");
  }
}

} // namespace

int main() {
  checkDefault();
  checkAlbedoFollowsBsdf();
  checkMatte();
  checkMatteNormal();
  checkPlasticDefaults();
  checkPlasticHeadroom();
  checkPaintedPlasticGGX();
  checkMetalGGX();
  checkShinyMetalGGX();
  checkMirrorBsdf();
  checkGlassBsdf();
  checkGlassOrientsByNg();
  checkInputsOnly();
  checkLoadedDefault();

  return checkSummary("a surface's BSDF: the Lambert-of-Cs default, albedo from rhoD, matte's Kd * Cs, the four GGX "
                      "shaders' closures, and mirror's and glass's delta closures");
}
