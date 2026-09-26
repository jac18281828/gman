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
 * The diffuse-albedo query: the base default (Cs, clamped), each built-in
 * shader's own answer, and its exclusions (Os, specular). Every shader
 * loads through GMANLoadableShader, exactly as mirrorglass_test.cpp loads
 * mirror and glass -- a real .so, never a shaders/ source file linked
 * directly into this binary, since two plugins defining
 * GMANGetLoadableInfo/GMANLoadShader at global scope in one binary is a
 * duplicate-symbol error.
 */

#include <cmath>
#include <string>

#include "check.h"
#include "checkertexture.h"
#include "gmandictionary.h"
#include "gmanloadableshader.h"
#include "gmanparameterlist.h"
#include "gmanshaderenvironment.h"
#include "gmansurfaceshader.h"
#include "ri.h"

namespace {

constexpr RtFloat kTol = (RtFloat)1.0e-6;

bool colorNear(GMANColor const& a, GMANColor const& b, RtFloat tol) {
  return std::fabs(a.getRed() - b.getRed()) <= tol && std::fabs(a.getGreen() - b.getGreen()) <= tol &&
         std::fabs(a.getBlue() - b.getBlue()) <= tol;
}

bool colorExactly(GMANColor const& a, GMANColor const& b) {
  return a.getRed() == b.getRed() && a.getGreen() == b.getGreen() && a.getBlue() == b.getBlue();
}

GMANParameterList floatParam(RtToken token, RtFloat value) {
  static GMANDictionary dictionary;
  RtToken tokens[1] = {token};
  RtPointer parms[1] = {&value};
  return GMANParameterList(dictionary, 1, tokens, parms);
}

GMANParameterList kdAndTextureParam(RtFloat kd, std::string const& texturename) {
  static GMANDictionary dictionary;
  RtToken tokens[2] = {RI_KD, RI_TEXTURENAME};
  char* textureCStr = const_cast<char*>(texturename.c_str());
  RtPointer parms[2] = {&kd, &textureCStr};
  return GMANParameterList(dictionary, 2, tokens, parms);
}

GMANParameterList kdAndKsParam(RtFloat kd, RtFloat ks) {
  static GMANDictionary dictionary;
  RtToken tokens[2] = {RI_KD, RI_KS};
  RtPointer parms[2] = {&kd, &ks};
  return GMANParameterList(dictionary, 2, tokens, parms);
}

// Loads path with params, and answers its albedo for env se.
GMANColor loadAlbedo(std::string const& path, GMANParameterList const& params, GMANSurfaceEnv const& se) {
  GMANLoadableShader loader(path.c_str(), params);
  GMANSurfaceShader const* shader = loader.getSurface();
  return shader->albedo(se);
}

// Kd*Cs, the diffuse product every clamp and exclusion check compares
// against.
GMANColor kdCs(RtFloat kd, GMANColor const& cs) {
  return GMANColor(kd * cs.getRed(), kd * cs.getGreen(), kd * cs.getBlue());
}

// A minimal, in-file GMANSurfaceShader that never overrides albedo, so
// its own answer comes straight from the base default.
class BaseDefaultShader : public GMANSurfaceShader {
public:
  GMANColor computeCi(GMANSurfaceEnv const&) const override {
    return GMANColor((RtFloat)0.0, (RtFloat)0.0, (RtFloat)0.0);
  }
  GMANColor computeOi(GMANSurfaceEnv const&) const override {
    return GMANColor((RtFloat)0.0, (RtFloat)0.0, (RtFloat)0.0);
  }
};

// The base default: a Cs inside [0, 1] passes through exactly; a Cs with a
// channel above 1 and a channel below 0 clamps to exactly [0, 1].
void checkBaseDefault() {
  BaseDefaultShader const shader;

  GMANColor const csInRange((RtFloat)0.5, (RtFloat)0.4, (RtFloat)0.3);
  GMANSurfaceEnv seInRange;
  seInRange.Cs = csInRange;
  check(colorExactly(shader.albedo(seInRange), csInRange), "A: base default albedo equals Cs exactly inside [0, 1]");

  GMANColor const csOutOfRange((RtFloat)1.4, (RtFloat)0.2, (RtFloat)-0.3);
  GMANColor const wantClamped((RtFloat)1.0, (RtFloat)0.2, (RtFloat)0.0);
  GMANSurfaceEnv seOutOfRange;
  seOutOfRange.Cs = csOutOfRange;
  check(colorExactly(shader.albedo(seOutOfRange), wantClamped), "A: base default albedo clamps Cs to [0, 1]");

  RtFloat const nan = std::nanf("");
  GMANColor const csWithNaN(nan, (RtFloat)0.4, (RtFloat)0.3);
  GMANColor const wantNaNMapped((RtFloat)0.0, (RtFloat)0.4, (RtFloat)0.3);
  GMANSurfaceEnv seWithNaN;
  seWithNaN.Cs = csWithNaN;
  check(colorExactly(shader.albedo(seWithNaN), wantNaNMapped),
        "A: base default albedo maps a NaN Cs channel to exactly 0");
}

// matte, whose albedo comes from its bsdf override: a NaN Cs channel maps
// to exactly 0, matching the base default.
void checkMatteNaN() {
  RtFloat const nan = std::nanf("");
  GMANColor const cs(nan, (RtFloat)0.5, (RtFloat)0.4);
  GMANColor const want((RtFloat)0.0, (RtFloat)0.5, (RtFloat)0.4);
  GMANSurfaceEnv se;
  se.Cs = cs;
  check(colorExactly(loadAlbedo("libmatte.so", GMANParameterList(), se), want),
        "matte: albedo maps a NaN Cs channel to exactly 0");
}

// Shared between matte and plastic: a test Kd other than the shader's own
// spec default, Cs common to every case here, the two clamp bounds, and
// Os-invariance.
void checkMatteOrPlastic(std::string const& path, std::string const& name, RtFloat testKd) {
  GMANColor const cs((RtFloat)0.5, (RtFloat)0.4, (RtFloat)0.3);
  GMANColor const opaqueOs((RtFloat)1.0, (RtFloat)1.0, (RtFloat)1.0);

  {
    GMANSurfaceEnv se;
    se.Cs = cs;
    se.Os = opaqueOs;
    check(colorNear(loadAlbedo(path, floatParam(RI_KD, testKd), se), kdCs(testKd, cs), kTol),
          name + ": B albedo equals Kd*Cs within tolerance");
  }

  {
    RtFloat const highKd = (RtFloat)2.0;
    GMANColor const highCs((RtFloat)0.9, (RtFloat)0.9, (RtFloat)0.9);
    GMANColor const want((RtFloat)1.0, (RtFloat)1.0, (RtFloat)1.0);
    GMANSurfaceEnv se;
    se.Cs = highCs;
    se.Os = opaqueOs;
    check(colorExactly(loadAlbedo(path, floatParam(RI_KD, highKd), se), want),
          name + ": B albedo clamps a raw product above 1 to exactly 1");
  }

  {
    RtFloat const negativeKd = (RtFloat)-0.3;
    GMANColor const want((RtFloat)0.0, (RtFloat)0.0, (RtFloat)0.0);
    GMANSurfaceEnv se;
    se.Cs = cs;
    se.Os = opaqueOs;
    check(colorExactly(loadAlbedo(path, floatParam(RI_KD, negativeKd), se), want),
          name + ": B albedo clamps a raw negative product to exactly 0");
  }

  {
    GMANColor const transparentOs((RtFloat)0.2, (RtFloat)0.2, (RtFloat)0.2);
    GMANColor const want = kdCs(testKd, cs);

    GMANSurfaceEnv seOpaque;
    seOpaque.Cs = cs;
    seOpaque.Os = opaqueOs;
    GMANSurfaceEnv seTransparent;
    seTransparent.Cs = cs;
    seTransparent.Os = transparentOs;

    check(colorNear(loadAlbedo(path, floatParam(RI_KD, testKd), seOpaque), want, kTol),
          name + ": B albedo is unaffected by Os at Os = (1, 1, 1)");
    check(colorNear(loadAlbedo(path, floatParam(RI_KD, testKd), seTransparent), want, kTol),
          name + ": B albedo is unaffected by Os at Os = (0.2, 0.2, 0.2)");
  }
}

// Plastic only: at Kd/Cs where every channel of Kd*Cs is below 1, so a
// leaked specular term is not hidden by the clamp, a lit env whose
// specular(...) is independently confirmed nonzero, and a named nonzero
// Ks, still answers Kd*Cs exactly: no Ks/specularcolor leakage.
void checkPlasticKsExcluded() {
  RtFloat const kd = (RtFloat)0.7;
  RtFloat const ks = (RtFloat)0.8;
  GMANColor const cs((RtFloat)0.5, (RtFloat)0.4, (RtFloat)0.3);
  RtFloat const plasticDefaultRoughness = (RtFloat)0.1;
  GMANPoint const lightPosition((RtFloat)0.0, (RtFloat)0.0, (RtFloat)5.0);
  GMANColor const lightColor((RtFloat)1.0, (RtFloat)1.0, (RtFloat)1.0);

  GMANSurfaceEnv se;
  se.Cs = cs;
  se.Os = GMANColor((RtFloat)1.0, (RtFloat)1.0, (RtFloat)1.0);
  se.P = GMANPoint((RtFloat)0.0, (RtFloat)0.0, (RtFloat)0.0);
  se.N = GMANNormal((RtFloat)0.0, (RtFloat)0.0, (RtFloat)1.0);
  se.Ng = GMANNormal((RtFloat)0.0, (RtFloat)0.0, (RtFloat)1.0);
  se.I = GMANVector((RtFloat)0.0, (RtFloat)0.0, (RtFloat)-1.0);

  GMANLight const light(GMAN_LIGHT_POINT, lightColor, lightPosition, GMANVector());
  se.lights = {&light};

  // N.L and N.H both positive: the light sits straight above the surface,
  // on the same side as the eye.
  GMANVector const nf = se.faceforward(se.N, se.I, se.Ng);
  GMANVector const vf((RtFloat)-se.I.getX(), (RtFloat)-se.I.getY(), (RtFloat)-se.I.getZ());
  GMANColor const specularTerm = se.specular(nf, vf, plasticDefaultRoughness);
  RtFloat const specularMagnitude = specularTerm.getRed() + specularTerm.getGreen() + specularTerm.getBlue();
  check(specularMagnitude > (RtFloat)0.0, "B Ks-excluded: se.specular(...) at this env is nonzero");

  check(colorExactly(loadAlbedo("libplastic.so", kdAndKsParam(kd, ks), se), kdCs(kd, cs)),
        "B Ks-excluded: plastic's albedo excludes the nonzero specular term at Ks = 0.8");
}

// paintedplastic: an empty texturename matches the white-texture default; a
// real texture multiplies in the sampled texel at an asymmetric (s, t)
// whose swap would read a different texel; and its own clamp bounds, as
// matte and plastic have.
void checkPaintedPlastic() {
  RtFloat const kd = (RtFloat)0.6;
  GMANColor const cs((RtFloat)0.5, (RtFloat)0.4, (RtFloat)0.3);

  {
    GMANSurfaceEnv se;
    se.Cs = cs;
    check(colorNear(loadAlbedo("libpaintedplastic.so", kdAndTextureParam(kd, std::string()), se), kdCs(kd, cs), kTol),
          "C: paintedplastic albedo with an empty texturename equals Kd*Cs, matching B's matte/plastic case");
  }

  {
    std::string const textureName = "shaderalbedo_checker.tif";
    RtFloat const s = (RtFloat)0.75; // the checker's top-right texel centre
    RtFloat const t = (RtFloat)0.25;
    GMANColor const whiteTexel((RtFloat)1.0, (RtFloat)1.0, (RtFloat)1.0);
    GMANColor const greenTexel((RtFloat)0.0, (RtFloat)1.0, (RtFloat)0.0); // checkertexture.h's top-right

    check(writeCheckerTexture(textureName), textureName + " writes into the test's own working directory");

    GMANSurfaceEnv se;
    se.Cs = cs;
    se.s = s;
    se.t = t;

    GMANColor const tex = se.texture(textureName, s, t);
    check(!colorExactly(tex, whiteTexel), "C: the sampled texel differs from white in at least one channel");
    check(colorExactly(tex, greenTexel), "C: the sampled texel equals the checker's own top-right texel");

    GMANColor const want(kd * cs.getRed() * tex.getRed(), kd * cs.getGreen() * tex.getGreen(),
                         kd * cs.getBlue() * tex.getBlue());
    check(colorNear(loadAlbedo("libpaintedplastic.so", kdAndTextureParam(kd, textureName), se), want, kTol),
          "C: paintedplastic albedo equals Kd*Cs*tex within tolerance, at (s, t) whose swap reads a different "
          "texel");
  }

  {
    RtFloat const highKd = (RtFloat)2.0;
    GMANColor const highCs((RtFloat)0.9, (RtFloat)0.9, (RtFloat)0.9);
    GMANColor const want((RtFloat)1.0, (RtFloat)1.0, (RtFloat)1.0);
    GMANSurfaceEnv se;
    se.Cs = highCs;
    check(colorExactly(loadAlbedo("libpaintedplastic.so", kdAndTextureParam(highKd, std::string()), se), want),
          "paintedplastic: C albedo clamps a raw product above 1 to exactly 1");
  }

  {
    RtFloat const negativeKd = (RtFloat)-0.3;
    GMANColor const want((RtFloat)0.0, (RtFloat)0.0, (RtFloat)0.0);
    GMANSurfaceEnv se;
    se.Cs = cs;
    check(colorExactly(loadAlbedo("libpaintedplastic.so", kdAndTextureParam(negativeKd, std::string()), se), want),
          "paintedplastic: C albedo clamps a raw negative product to exactly 0");
  }
}

// metal and shinymetal build a GGX-only closure with no Lambert lobe, so
// the base default's bsdf(se).rhoD() is exactly black; mirror and glass
// still override albedo directly to the same effect. None has a diffuse
// term, at Cs = (1, 1, 1) with every other parameter left at default, no
// light and no tracer bound.
void checkBlackShader(std::string const& path, std::string const& name) {
  GMANColor const cs((RtFloat)1.0, (RtFloat)1.0, (RtFloat)1.0);
  GMANColor const black((RtFloat)0.0, (RtFloat)0.0, (RtFloat)0.0);

  GMANSurfaceEnv se;
  se.Cs = cs;
  check(colorExactly(loadAlbedo(path, GMANParameterList(), se), black), name + ": D albedo is exactly black");
}

} // namespace

int main() {
  checkBaseDefault();

  checkMatteOrPlastic("libmatte.so", "matte", (RtFloat)0.6);
  checkMatteNaN();
  checkMatteOrPlastic("libplastic.so", "plastic", (RtFloat)0.7);
  checkPlasticKsExcluded();

  checkPaintedPlastic();

  checkBlackShader("libmetal.so", "metal");
  checkBlackShader("libshinymetal.so", "shinymetal");
  checkBlackShader("libmirror.so", "mirror");
  checkBlackShader("libglass.so", "glass");

  return checkSummary("albedo: the base default, each built-in shader's own diffuse reflectance, its clamp and its "
                      "exclusions");
}
