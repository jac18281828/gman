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
 * gman::bsdf, the shading API's free function: it fills a GMANSurfaceEnv
 * the same way gman::shade and gman::albedo do and returns the shader's
 * own bsdf(env) -- plastic's closure matching one built by hand, matte's
 * closure built at the shading normal regardless of the geometric one, a
 * null shader's one Lambert lobe of Cs, and glass's dielectric lobe.
 * Shaders load through GMANAttributes::setSurface, as shaderbsdf_test.cpp
 * does.
 */

#include <cmath>
#include <memory>

#include "check.h"
#include "gmanattributes.h"
#include "gmanbsdf.h"
#include "gmancolor.h"
#include "gmandictionary.h"
#include "gmannormal.h"
#include "gmanparameterlist.h"
#include "gmanshaderenvironment.h"
#include "gmanshading.h"
#include "gmansurfaceshader.h"
#include "gmanvector.h"
#include "ri.h"

namespace {

constexpr double kPiD = 3.14159265358979323846;
constexpr double kTol = 1e-6;
constexpr RtFloat kGlassIor = 1.5f;

GMANMatrix4 const kIdentity;

GMANColor const kPlasticCs(0.5f, 0.4f, 0.3f);
RtFloat const kPlasticKd = 0.6f;
RtFloat const kPlasticKs = 0.4f;
RtFloat const kPlasticRoughness = 0.1f;

GMANColor const kMatteCs(0.5f, 0.4f, 0.3f);
RtFloat const kMatteKd = 0.6f;
GMANNormal const kMatteN(0.0f, 0.0f, 1.0f);
GMANNormal const kMatteNg(1.0f, 0.0f, 0.0f);
GMANVector const kWoUnnormalized(-0.5f, 0.0f, 1.0f);
GMANVector const kWiUnnormalized(0.5f, 0.3f, 1.0f);

GMANColor const kFreeCs(1.4f, 0.2f, -0.3f);
GMANColor const kFreeClamped(1.0f, 0.2f, 0.0f);

GMANColor const kOpaqueOs(1.0f, 1.0f, 1.0f);

std::shared_ptr<GMANSurfaceShader const> loadSurface(std::string const& name, GMANParameterList const& params) {
  GMANAttributes attributes;
  attributes.setSurface(name, params);
  return attributes.getSurface(0.0);
}

GMANParameterList plasticParams() {
  static GMANDictionary dictionary;
  RtToken tokens[4] = {RI_KD, RI_KS, RI_SPECULARCOLOR, RI_ROUGHNESS};
  RtFloat kd = kPlasticKd;
  RtFloat ks = kPlasticKs;
  RtFloat sc[3] = {1.0f, 1.0f, 1.0f};
  RtFloat roughness = kPlasticRoughness;
  RtPointer parms[4] = {&kd, &ks, sc, &roughness};
  return GMANParameterList(dictionary, 4, tokens, parms);
}

GMANParameterList matteParams(RtFloat kd) {
  static GMANDictionary dictionary;
  RtToken tokens[1] = {RI_KD};
  RtPointer parms[1] = {&kd};
  return GMANParameterList(dictionary, 1, tokens, parms);
}

GMANVector normalized(GMANVector v) {
  v.normalize();
  return v;
}

bool colorExactly(GMANColor const& a, GMANColor const& b) {
  return a.getRed() == b.getRed() && a.getGreen() == b.getGreen() && a.getBlue() == b.getBlue();
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
  for (int i = 0; i < 3; ++i) {
    if (!nearRel(channel(a, i), channel(b, i), rel)) {
      return false;
    }
  }
  return true;
}

// A closure "matches" another when lobe count and each lobe's kind,
// weight, alpha and eta are equal exactly.
bool closuresMatch(gman::BSDF const& a, gman::BSDF const& b) {
  if (a.lobeCount() != b.lobeCount()) {
    return false;
  }
  for (std::size_t i = 0; i < a.lobeCount(); ++i) {
    gman::Lobe const& la = a.lobe(i);
    gman::Lobe const& lb = b.lobe(i);
    if (la.kind != lb.kind || !colorExactly(la.weight, lb.weight) || la.alpha != lb.alpha || la.eta != lb.eta) {
      return false;
    }
  }
  return true;
}

gman::Appearance appearanceFor(std::shared_ptr<GMANSurfaceShader const> const& shader, GMANColor const& cs) {
  gman::Appearance appearance;
  appearance.shader = shader;
  appearance.Cs = cs;
  appearance.Os = kOpaqueOs;
  return appearance;
}

gman::SurfacePoint pointAt(GMANNormal const& n, GMANNormal const& ng) {
  gman::SurfacePoint point;
  point.N = n;
  point.Ng = ng;
  return point;
}

// ---- check 1: plastic matches a closure built by hand from the same env
// ----
void checkPlasticMatchesHandBuilt() {
  auto const plastic = loadSurface("plastic", plasticParams());
  check(plastic != nullptr, "plastic: loads through setSurface");
  if (plastic == nullptr) {
    return;
  }

  gman::Appearance const appearance = appearanceFor(plastic, kPlasticCs);
  gman::SurfacePoint const point = pointAt(GMANNormal(0.0f, 0.0f, 1.0f), GMANNormal(0.0f, 0.0f, 1.0f));
  gman::BSDF const viaFreeFunction = gman::bsdf(appearance, point, kIdentity);

  GMANSurfaceEnv handEnv;
  handEnv.Cs = kPlasticCs;
  handEnv.Os = kOpaqueOs;
  handEnv.N = point.N;
  handEnv.Ng = point.Ng;
  gman::BSDF const handBuilt = plastic->bsdf(handEnv);

  check(closuresMatch(viaFreeFunction, handBuilt),
        "plastic: gman::bsdf matches shader->bsdf(env) on an env filled by hand, lobe for lobe");
}

// ---- check 2: matte builds at the shading normal, never the geometric one
// ----
void checkMatteBuildsAtShadingNormal() {
  auto const matte = loadSurface(RI_MATTE, matteParams(kMatteKd));
  check(matte != nullptr, "matte: loads through setSurface");
  if (matte == nullptr) {
    return;
  }

  gman::Appearance const appearance = appearanceFor(matte, kMatteCs);
  gman::SurfacePoint const point = pointAt(kMatteN, kMatteNg);
  gman::BSDF const closure = gman::bsdf(appearance, point, kIdentity);

  GMANVector const wo = normalized(kWoUnnormalized);
  GMANVector const wi = normalized(kWiUnnormalized);

  GMANColor expectedEval(kMatteKd * kMatteCs.getRed(), kMatteKd * kMatteCs.getGreen(), kMatteKd * kMatteCs.getBlue());
  expectedEval.scale(static_cast<RtFloat>(1.0 / kPiD));

  check(colorNearRel(closure.eval(wo, wi), expectedEval, kTol),
        "matte: eval at N = (0, 0, 1), Ng = (1, 0, 0) is Kd * Cs / pi within 1e-6 relative, on one side of N and "
        "opposite sides of Ng");
}

// ---- check 3: an appearance with no shader answers one Lambert lobe of
// Cs ----
void checkNullShaderAnswersLambertOfCs() {
  gman::Appearance appearance;
  appearance.Cs = kFreeCs;
  appearance.Os = kOpaqueOs;

  gman::SurfacePoint const point = pointAt(GMANNormal(0.0f, 0.0f, 1.0f), GMANNormal(0.0f, 0.0f, 1.0f));
  gman::BSDF const closure = gman::bsdf(appearance, point, kIdentity);

  check(closure.lobeCount() == 1 && closure.lobe(0).kind == gman::LobeKind::lambert &&
            colorExactly(closure.lobe(0).weight, kFreeClamped),
        "null shader: gman::bsdf at Cs = (1.4, 0.2, -0.3) answers one lambert lobe of weight exactly (1, 0.2, 0)");
}

// ---- check 4: glass answers one dielectric lobe ----
void checkGlassAnswersDielectric() {
  auto const glass = loadSurface("glass", GMANParameterList());
  check(glass != nullptr, "glass: loads through setSurface");
  if (glass == nullptr) {
    return;
  }

  gman::Appearance const appearance = appearanceFor(glass, kPlasticCs);
  gman::SurfacePoint const point = pointAt(GMANNormal(0.0f, 0.0f, 1.0f), GMANNormal(0.0f, 0.0f, 1.0f));
  gman::BSDF const closure = gman::bsdf(appearance, point, kIdentity);

  check(closure.lobeCount() == 1 && closure.lobe(0).kind == gman::LobeKind::dielectric &&
            colorExactly(closure.lobe(0).weight, GMANColor(1.0f, 1.0f, 1.0f)) && closure.lobe(0).eta == kGlassIor,
        "glass: gman::bsdf answers one dielectric lobe of weight (1, 1, 1) and eta 1.5, exactly");
}

} // namespace

int main() {
  checkPlasticMatchesHandBuilt();
  checkMatteBuildsAtShadingNormal();
  checkNullShaderAnswersLambertOfCs();
  checkGlassAnswersDielectric();

  return checkSummary("gman::bsdf: plastic matches a hand-built env, matte builds at the shading normal, a null "
                      "shader answers Cs as one Lambert lobe, and glass answers its dielectric lobe");
}
