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

#include <algorithm>
#include <cmath>
#include <numbers>
#include <optional>
#include <string>
#include <vector>

#include <tiffio.h>

#include "check.h"
#include "gmanbsdf.h"
#include "gmancolor.h"
#include "gmandictionary.h"
#include "gmanlightsourcemgr.h"
#include "gmanloadableshader.h"
#include "gmanparameterlist.h"
#include "gmanshaderenvironment.h"
#include "gmansurfaceshader.h"
#include "gmanvector.h"
#include "ri.h"

namespace {

constexpr double kTol = 1.0e-4;

struct Texel {
  unsigned char r;
  unsigned char g;
  unsigned char b;
};

struct Frame {
  GMANVector dPdu = GMANVector((RtFloat)1.0, (RtFloat)0.0, (RtFloat)0.0);
  GMANVector dPdv = GMANVector((RtFloat)0.0, (RtFloat)1.0, (RtFloat)0.0);
};

struct Setup {
  std::string normalname;
  std::optional<RtFloat> normalgreendown;
  Frame frame;
  GMANVector toLight;
  RtFloat kd = (RtFloat)1.0;
  RtFloat ks = (RtFloat)0.0;
};

GMANVector vec(double x, double y, double z) { return GMANVector((RtFloat)x, (RtFloat)y, (RtFloat)z); }

double dot(GMANVector const& a, GMANVector const& b) {
  return (double)a.getX() * b.getX() + (double)a.getY() * b.getY() + (double)a.getZ() * b.getZ();
}

bool writeMap(std::string const& path, Texel texel) {
  TIFF* tif = TIFFOpen(path.c_str(), "w");
  if (tif == nullptr) {
    return false;
  }
  TIFFSetField(tif, TIFFTAG_IMAGEWIDTH, (uint32_t)1);
  TIFFSetField(tif, TIFFTAG_IMAGELENGTH, (uint32_t)1);
  TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, 8);
  TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, 3);
  TIFFSetField(tif, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT);
  TIFFSetField(tif, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB);
  TIFFSetField(tif, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
  TIFFSetField(tif, TIFFTAG_ROWSPERSTRIP, 1);
  unsigned char row[3] = {texel.r, texel.g, texel.b};
  bool const ok = TIFFWriteScanline(tif, row, 0, 0) >= 0;
  TIFFClose(tif);
  return ok;
}

double decode(unsigned char channel) { return 2.0 * (channel / 255.0) - 1.0; }

struct Direction {
  double x;
  double y;
  double z;
};

Direction bentNormal(Texel texel, GMANVector const& tangent, GMANVector const& greenAxis, GMANVector const& normal) {
  double const x = decode(texel.r);
  double const y = decode(texel.g);
  double const z = decode(texel.b);
  double const nx = x * tangent.getX() + y * greenAxis.getX() + z * normal.getX();
  double const ny = x * tangent.getY() + y * greenAxis.getY() + z * normal.getY();
  double const nz = x * tangent.getZ() + y * greenAxis.getZ() + z * normal.getZ();
  double const length = std::sqrt(nx * nx + ny * ny + nz * nz);
  return {nx / length, ny / length, nz / length};
}

double dot(Direction const& a, GMANVector const& b) { return a.x * b.getX() + a.y * b.getY() + a.z * b.getZ(); }

double bumpTerm(GMANVector const& base, Direction const& bent, GMANVector const& toLight) {
  double const baseDotLight = dot(Direction{base.getX(), base.getY(), base.getZ()}, toLight);
  double const bentDotLight = dot(bent, toLight);
  double const baseDotBent = dot(bent, base);
  double const g = baseDotLight / (bentDotLight * baseDotBent);
  if (g <= 0.0) {
    return 0.0;
  }
  if (g >= 1.0) {
    return 1.0;
  }
  return -g * g * g + g * g + g;
}

double predictedRed(Texel texel, GMANVector const& tangent, GMANVector const& greenAxis, GMANVector const& normal,
                    GMANVector const& toLight) {
  Direction const bent = bentNormal(texel, tangent, greenAxis, normal);
  return std::max(0.0, dot(bent, toLight)) * bumpTerm(normal, bent, toLight);
}

GMANParameterList paintedParams(Setup const& setup) {
  static GMANDictionary dictionary;
  RtFloat ka = (RtFloat)0.0;
  RtFloat kd = setup.kd;
  RtFloat ks = setup.ks;
  char emptyName[1] = {'\0'};
  char* texturename = emptyName;
  char* normalname = const_cast<char*>(setup.normalname.c_str());
  RtFloat greenDown = setup.normalgreendown.value_or((RtFloat)0.0);

  std::vector<RtToken> tokens = {RI_KA, RI_KD, RI_KS, RI_TEXTURENAME};
  std::vector<RtPointer> values = {&ka, &kd, &ks, &texturename};
  if (!setup.normalname.empty()) {
    tokens.push_back("string normalname");
    values.push_back(&normalname);
  }
  if (setup.normalgreendown) {
    tokens.push_back("float normalgreendown");
    values.push_back(&greenDown);
  }
  return GMANParameterList(dictionary, (RtInt)tokens.size(), tokens.data(), values.data());
}

class Card {
public:
  explicit Card(Setup const& setup)
      : params(paintedParams(setup)), loader("libpaintedplastic.so", params),
        light(GMAN_LIGHT_DISTANT, GMANColor((RtFloat)1.0, (RtFloat)1.0, (RtFloat)1.0), GMANPoint(), -setup.toLight) {
    env.P = GMANPoint((RtFloat)0.0, (RtFloat)0.0, (RtFloat)0.0);
    env.N = GMANNormal((RtFloat)0.0, (RtFloat)0.0, (RtFloat)1.0);
    env.Ng = GMANNormal((RtFloat)0.0, (RtFloat)0.0, (RtFloat)1.0);
    env.I = vec(0.0, 0.0, -1.0);
    env.s = (RtFloat)0.5;
    env.t = (RtFloat)0.5;
    env.Cs = GMANColor((RtFloat)1.0, (RtFloat)1.0, (RtFloat)1.0);
    env.Os = GMANColor((RtFloat)1.0, (RtFloat)1.0, (RtFloat)1.0);
    env.dPdu = setup.frame.dPdu;
    env.dPdv = setup.frame.dPdv;
    env.lights = {&light};
  }

  GMANColor ci() { return loader.getSurface()->computeCi(env); }
  gman::BSDF closure() { return loader.getSurface()->bsdf(env); }

private:
  GMANParameterList params;
  GMANLoadableShader loader;
  GMANLight light;
  GMANSurfaceEnv env;
};

void checkNear(double got, double want, std::string const& what) {
  check(std::fabs(got - want) <= kTol, what + ": " + std::to_string(got) + " vs " + std::to_string(want));
}

Texel const kRedTilt{191, 128, 238};
Texel const kGreenTilt{128, 191, 238};
Texel const kFlat{128, 128, 255};

GMANVector normalAxis() { return vec(0.0, 0.0, 1.0); }
GMANVector plusX() { return vec(1.0, 0.0, 0.0); }
GMANVector minusX() { return vec(-1.0, 0.0, 0.0); }
GMANVector plusY() { return vec(0.0, 1.0, 0.0); }
GMANVector minusY() { return vec(0.0, -1.0, 0.0); }
GMANVector alongX() { return vec(1.0, 0.0, 1.0) * (RtFloat)(std::numbers::sqrt2 / 2.0); }
GMANVector alongY() { return vec(0.0, 1.0, 1.0) * (RtFloat)(std::numbers::sqrt2 / 2.0); }
GMANVector towardDegrees(double degrees) {
  double const radians = degrees * std::numbers::pi / 180.0;
  return vec(std::sin(radians), 0.0, std::cos(radians));
}

bool sameColor(GMANColor const& a, GMANColor const& b) {
  return a.getRed() == b.getRed() && a.getGreen() == b.getGreen() && a.getBlue() == b.getBlue();
}

void checkDegenerate(Frame const& frame, std::string const& what) {
  Card unmapped(Setup{"", std::nullopt, Frame{}, alongX()});
  Card degenerate(Setup{"normalmap_redtilt.tif", std::nullopt, frame, alongX()});
  check(sameColor(degenerate.ci(), unmapped.ci()), what + ": computeCi equals the no-map value channel for channel");
}

void testNoMap() {
  Card card(Setup{"", std::nullopt, Frame{}, alongX()});
  checkNear(card.ci().getRed(), dot(normalAxis(), alongX()), "no map: computeCi is n . L for n = N");
  checkNear(card.ci().getRed(), std::numbers::sqrt2 / 2.0, "no map: computeCi is 0.7071");
}

void testRedTilt() {
  Card card(Setup{"normalmap_redtilt.tif", std::nullopt, Frame{}, alongX()});
  double const want = predictedRed(kRedTilt, plusX(), minusY(), normalAxis(), alongX());
  checkNear(card.ci().getRed(), want, "red tilt: computeCi follows the bent normal");
  check(want > 0.92 && want < 0.925, "red tilt: the prediction is about 0.9225");
}

void testMirroredTangent() {
  Frame frame;
  frame.dPdu = minusX();
  Card card(Setup{"normalmap_redtilt.tif", std::nullopt, frame, alongX()});
  double const want = predictedRed(kRedTilt, minusX(), minusY(), normalAxis(), alongX());
  checkNear(card.ci().getRed(), want, "mirrored tangent: the red channel bends along the mirrored dPdu");
  check(want > 0.25 && want < 0.27, "mirrored tangent: the prediction is about 0.2608");
}

void testGreenDefault() {
  Card card(Setup{"normalmap_greentilt.tif", std::nullopt, Frame{}, alongY()});
  double const want = predictedRed(kGreenTilt, plusX(), minusY(), normalAxis(), alongY());
  checkNear(card.ci().getRed(), want, "green, default: green bends toward -dPdv");
  check(want > 0.25 && want < 0.27, "green, default: the prediction is about 0.2608");
}

void testGreenDirectX() {
  Card card(Setup{"normalmap_greentilt.tif", (RtFloat)1.0, Frame{}, alongY()});
  double const want = predictedRed(kGreenTilt, plusX(), plusY(), normalAxis(), alongY());
  checkNear(card.ci().getRed(), want, "green, DirectX: green bends toward +dPdv");
  check(want > 0.92 && want < 0.925, "green, DirectX: the prediction is about 0.9225");

  Card nonzero(Setup{"normalmap_greentilt.tif", (RtFloat)7.0, Frame{}, alongY()});
  checkNear(nonzero.ci().getRed(), want, "green, DirectX: any nonzero normalgreendown counts as 1");
}

void testMirroredBitangent() {
  Frame frame;
  frame.dPdv = minusY();
  Card card(Setup{"normalmap_greentilt.tif", std::nullopt, frame, alongY()});
  double const want = predictedRed(kGreenTilt, plusX(), plusY(), normalAxis(), alongY());
  checkNear(card.ci().getRed(), want, "mirrored bitangent: green bends toward the mirrored -dPdv");
  check(want > 0.92 && want < 0.925, "mirrored bitangent: the prediction is about 0.9225");
}

void testBeyondTerminatorDiffuse() {
  GMANVector const toLight = towardDegrees(100.0);
  Direction const bent = bentNormal(kRedTilt, plusX(), minusY(), normalAxis());
  check(dot(bent, toLight) > 0.3, "beyond the terminator, diffuse: the bent normal faces the light");
  check(dot(Direction{0.0, 0.0, 1.0}, toLight) < 0.0, "beyond the terminator, diffuse: N faces away from it");

  Card card(Setup{"normalmap_redtilt.tif", std::nullopt, Frame{}, toLight});
  check(card.ci().getRed() == 0.0f,
        "beyond the terminator, diffuse: computeCi is exactly 0, got " + std::to_string(card.ci().getRed()));
}

void testBeyondTerminatorSpecular() {
  GMANVector const toLight = towardDegrees(100.0);
  GMANVector halfway = toLight + vec(0.0, 0.0, 1.0);
  halfway.normalize();
  Direction const bent = bentNormal(kRedTilt, plusX(), minusY(), normalAxis());
  check(dot(bent, halfway) > 0.0, "beyond the terminator, specular: the bent normal faces the half vector");

  Card card(Setup{"normalmap_redtilt.tif", std::nullopt, Frame{}, toLight, (RtFloat)0.0, (RtFloat)1.0});
  check(card.ci().getRed() == 0.0f,
        "beyond the terminator, specular: computeCi is exactly 0, got " + std::to_string(card.ci().getRed()));
}

void testFadeNearTerminator() {
  GMANVector const toLight = towardDegrees(80.0);
  Direction const bent = bentNormal(kRedTilt, plusX(), minusY(), normalAxis());
  double const term = bumpTerm(normalAxis(), bent, toLight);
  check(term > 0.0 && term < 1.0, "fade: the term lies strictly between 0 and 1, got " + std::to_string(term));

  Card card(Setup{"normalmap_redtilt.tif", std::nullopt, Frame{}, toLight});
  checkNear(card.ci().getRed(), dot(bent, toLight) * term, "fade: computeCi is s . L times the term");
  check(card.ci().getRed() < dot(bent, toLight) - kTol, "fade: computeCi sits below the unscaled s . L");
}

void testFlatMap() {
  Card card(Setup{"normalmap_flat.tif", std::nullopt, Frame{}, normalAxis()});
  checkNear(card.ci().getRed(), 1.0, "flat map: computeCi equals the unbent value");
}

void testDegenerateFrame() {
  Frame zero;
  zero.dPdu = vec(0.0, 0.0, 0.0);
  zero.dPdv = vec(0.0, 0.0, 0.0);
  checkDegenerate(zero, "degenerate frame");

  Frame nearlyParallel;
  nearlyParallel.dPdu = vec(1.0e-5, 0.0, 1.0);
  checkDegenerate(nearlyParallel, "near-degenerate frame, dPdu 1e-5 off N");
}

void testClosure() {
  GMANVector const wo = vec(0.0, 0.0, 1.0);
  GMANVector wi = vec(-1.0, 0.0, 0.3);
  wi.normalize();
  Card mapped(Setup{"normalmap_redtilt.tif", std::nullopt, Frame{}, alongX()});
  Card unmapped(Setup{"", std::nullopt, Frame{}, alongX()});
  GMANColor const mappedF = mapped.closure().eval(wo, wi);
  GMANColor const unmappedF = unmapped.closure().eval(wo, wi);
  check(mappedF.getRed() == 0.0f && mappedF.getGreen() == 0.0f && mappedF.getBlue() == 0.0f,
        "closure: wi below the bent normal evaluates to zero");
  check(unmappedF.getRed() > 0.0f, "closure: the same wi above N evaluates nonzero without the map");
  check(dot(mapped.closure().shadingNormal(), plusX()) > 0.4, "closure: its shading normal is the bent one");
}

} // namespace

int main() {
  check(writeMap("normalmap_redtilt.tif", kRedTilt), "the red-tilt map writes");
  check(writeMap("normalmap_greentilt.tif", kGreenTilt), "the green-tilt map writes");
  check(writeMap("normalmap_flat.tif", kFlat), "the flat map writes");

  testNoMap();
  testRedTilt();
  testMirroredTangent();
  testGreenDefault();
  testGreenDirectX();
  testMirroredBitangent();
  testBeyondTerminatorDiffuse();
  testBeyondTerminatorSpecular();
  testFadeNearTerminator();
  testFlatMap();
  testDegenerateFrame();
  testClosure();

  return checkSummary("paintedplastic bends its shading normal by a tangent-space map in computeCi and bsdf");
}
