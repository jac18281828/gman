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
 * One shader instance per Surface call. Every check loads a plugin through
 * GMANAttributes::setSurface, the path RiSurfaceV takes, and shades through
 * gman::appearanceOf and gman::shade.
 */

#include <cmath>
#include <string>
#include <vector>

#include <dlfcn.h>

#include "check.h"
#include "gmanattributes.h"
#include "gmandictionary.h"
#include "gmanerror.h"
#include "gmanloadableshader.h"
#include "gmanparameterlist.h"
#include "gmanshaderenvironment.h"
#include "gmanshading.h"
#include "gmansurfaceshader.h"
#include "gmantrace.h"
#include "ri.h"

namespace {

constexpr RtFloat kTol = (RtFloat)1.0e-6;

bool colorNear(GMANColor const& a, GMANColor const& b, RtFloat tol) {
  return std::fabs(a.getRed() - b.getRed()) <= tol && std::fabs(a.getGreen() - b.getGreen()) <= tol &&
         std::fabs(a.getBlue() - b.getBlue()) <= tol;
}

GMANParameterList floatParam(RtToken token, RtFloat value) {
  static GMANDictionary dictionary;
  RtToken tokens[1] = {token};
  RtPointer parms[1] = {&value};
  return GMANParameterList(dictionary, 1, tokens, parms);
}

// countingshader is a plugin, dlopened at runtime by GMANAttributes::
// setSurface, never linked into this binary, so its own live-count
// function is resolved the same way: dlopen (idempotent -- the plugin may
// already be loaded) then dlsym, once, memoized here.
int countingShaderLiveCount() {
  using LiveCountFn = int (*)();
  static LiveCountFn const fn = []() -> LiveCountFn {
    void* const handle = dlopen("libcountingshader.so", RTLD_LAZY);
    if (handle == nullptr) {
      return nullptr;
    }
    return reinterpret_cast<LiveCountFn>(dlsym(handle, "GMANCountingShaderLiveCount"));
  }();
  return fn ? fn() : -1;
}

// Always returns the same colour, regardless of what it is asked to trace
// -- mirror's Kr is what this check varies, not what trace() finds.
class FixedAnswerTracer : public gman::Tracer {
public:
  explicit FixedAnswerTracer(GMANColor answer) : answer_(answer) {}
  GMANColor trace(GMANPoint const&, GMANVector const&, GMANVector const&, RtFloat) const override { return answer_; }

private:
  GMANColor answer_;
};

// A1: for each of the seven shipped plugins, two setSurface calls on two
// GMANAttributes yield distinct, non-null shader instances.
void checkDistinctInstances() {
  std::vector<std::string> const plugins = {RI_MATTE,      RI_PLASTIC, RI_METAL, RI_PAINTEDPLASTIC,
                                            RI_SHINYMETAL, "mirror",   "glass"};
  GMANParameterList const emptyPl;
  for (std::string const& name : plugins) {
    GMANAttributes attrA;
    GMANAttributes attrB;
    attrA.setSurface(name, emptyPl);
    attrB.setSurface(name, emptyPl);
    auto const shaderA = attrA.getSurface(0.0);
    auto const shaderB = attrB.getSurface(0.0);
    check(shaderA != nullptr && shaderB != nullptr, name + ": two setSurface calls each yield a non-null shader");
    check(shaderA != shaderB, name + ": two setSurface calls on two GMANAttributes yield distinct instances");
  }
}

// A2: two mirror instances differing in Kr, and two matte instances
// differing in Ka, each shaded A, B, A, B once all four exist -- a shared
// singleton or a rebound parameter list would leak one instance's own
// value into the other's Ci.
void checkInterleavedShading() {
  GMANMatrix4 const cameraToWorld;
  gman::SurfacePoint point;
  point.P = GMANPoint(0.0f, 0.0f, 0.0f);
  point.N = GMANNormal(0.0f, 0.0f, 1.0f);
  point.Ng = GMANNormal(0.0f, 0.0f, 1.0f);
  point.I = GMANVector(0.0f, 0.0f, -1.0f);
  point.E = GMANPoint(0.0f, 0.0f, 5.0f);

  // ---- mirror pair: differ in Kr ----
  GMANColor const mirrorCsA(0.5f, 0.4f, 0.3f);
  GMANColor const mirrorCsB(0.2f, 0.6f, 0.9f);
  RtFloat const krA = 0.3f;
  RtFloat const krB = 0.7f;

  GMANAttributes mirrorAttrA;
  GMANAttributes mirrorAttrB;
  RtColor mirrorColorA = {mirrorCsA.getRed(), mirrorCsA.getGreen(), mirrorCsA.getBlue()};
  RtColor mirrorColorB = {mirrorCsB.getRed(), mirrorCsB.getGreen(), mirrorCsB.getBlue()};
  mirrorAttrA.setColor(mirrorColorA);
  mirrorAttrB.setColor(mirrorColorB);
  mirrorAttrA.setSurface("mirror", floatParam(RI_KR, krA));
  mirrorAttrB.setSurface("mirror", floatParam(RI_KR, krB));

  gman::Appearance const mirrorAppA = gman::appearanceOf(mirrorAttrA);
  gman::Appearance const mirrorAppB = gman::appearanceOf(mirrorAttrB);

  // ---- matte pair: differ in Ka, lit by one ambient light ----
  GMANColor const matteCsA(0.6f, 0.2f, 0.8f);
  GMANColor const matteCsB(0.3f, 0.5f, 0.1f);
  RtFloat const kaA = 0.4f;
  RtFloat const kaB = 0.9f;

  GMANAttributes matteAttrA;
  GMANAttributes matteAttrB;
  RtColor matteColorA = {matteCsA.getRed(), matteCsA.getGreen(), matteCsA.getBlue()};
  RtColor matteColorB = {matteCsB.getRed(), matteCsB.getGreen(), matteCsB.getBlue()};
  matteAttrA.setColor(matteColorA);
  matteAttrB.setColor(matteColorB);
  matteAttrA.setSurface(RI_MATTE, floatParam(RI_KA, kaA));
  matteAttrB.setSurface(RI_MATTE, floatParam(RI_KA, kaB));

  gman::Appearance matteAppA = gman::appearanceOf(matteAttrA);
  gman::Appearance matteAppB = gman::appearanceOf(matteAttrB);
  GMANLight const ambient(GMAN_LIGHT_AMBIENT, GMANColor(1.0f, 1.0f, 1.0f), GMANPoint(), GMANVector());
  matteAppA.lights = {&ambient};
  matteAppB.lights = {&ambient};

  // Only once all four instances above exist does any shading run.

  GMANColor const tracerAnswer(0.5f, 0.25f, 0.75f);
  FixedAnswerTracer tracer(tracerAnswer);
  GMANColor const wantMirrorA(mirrorCsA.getRed() * krA * tracerAnswer.getRed(),
                              mirrorCsA.getGreen() * krA * tracerAnswer.getGreen(),
                              mirrorCsA.getBlue() * krA * tracerAnswer.getBlue());
  GMANColor const wantMirrorB(mirrorCsB.getRed() * krB * tracerAnswer.getRed(),
                              mirrorCsB.getGreen() * krB * tracerAnswer.getGreen(),
                              mirrorCsB.getBlue() * krB * tracerAnswer.getBlue());

  gman::Shading const mirrorA1 = gman::shade(mirrorAppA, point, cameraToWorld, nullptr, &tracer);
  gman::Shading const mirrorB1 = gman::shade(mirrorAppB, point, cameraToWorld, nullptr, &tracer);
  gman::Shading const mirrorA2 = gman::shade(mirrorAppA, point, cameraToWorld, nullptr, &tracer);
  gman::Shading const mirrorB2 = gman::shade(mirrorAppB, point, cameraToWorld, nullptr, &tracer);
  check(colorNear(mirrorA1.Ci, wantMirrorA, kTol), "A.2 mirror: A's first interleaved Ci matches its own Kr");
  check(colorNear(mirrorB1.Ci, wantMirrorB, kTol), "A.2 mirror: B's first interleaved Ci matches its own Kr");
  check(colorNear(mirrorA2.Ci, wantMirrorA, kTol), "A.2 mirror: A's second interleaved Ci still matches its own Kr");
  check(colorNear(mirrorB2.Ci, wantMirrorB, kTol), "A.2 mirror: B's second interleaved Ci still matches its own Kr");

  GMANColor const wantMatteA(matteCsA.getRed() * kaA, matteCsA.getGreen() * kaA, matteCsA.getBlue() * kaA);
  GMANColor const wantMatteB(matteCsB.getRed() * kaB, matteCsB.getGreen() * kaB, matteCsB.getBlue() * kaB);

  gman::Shading const matteA1 = gman::shade(matteAppA, point, cameraToWorld);
  gman::Shading const matteB1 = gman::shade(matteAppB, point, cameraToWorld);
  gman::Shading const matteA2 = gman::shade(matteAppA, point, cameraToWorld);
  gman::Shading const matteB2 = gman::shade(matteAppB, point, cameraToWorld);
  check(colorNear(matteA1.Ci, wantMatteA, kTol), "A.2 matte: A's first interleaved Ci matches its own Ka");
  check(colorNear(matteB1.Ci, wantMatteB, kTol), "A.2 matte: B's first interleaved Ci matches its own Ka");
  check(colorNear(matteA2.Ci, wantMatteA, kTol), "A.2 matte: A's second interleaved Ci still matches its own Ka");
  check(colorNear(matteB2.Ci, wantMatteB, kTol), "A.2 matte: B's second interleaved Ci still matches its own Ka");
}

// A3: countingshader's live count, read through the plugin's own
// GMANCountingShaderLiveCount, tracks GMANLoadableShader's own ownership.
void checkLifetimeAndDestruction() {
  check(countingShaderLiveCount() == 0, "A.3: countingshader's live count starts at 0");

  GMANColor const csA(0.2f, 0.4f, 0.6f);
  GMANColor const csB(0.8f, 0.1f, 0.3f);

  gman::Appearance appearanceA;
  gman::Appearance appearanceB;
  {
    GMANAttributes attrA;
    GMANAttributes attrB;
    RtColor colorA = {csA.getRed(), csA.getGreen(), csA.getBlue()};
    RtColor colorB = {csB.getRed(), csB.getGreen(), csB.getBlue()};
    attrA.setColor(colorA);
    attrB.setColor(colorB);

    GMANParameterList const emptyPl;
    attrA.setSurface("countingshader", emptyPl);
    attrB.setSurface("countingshader", emptyPl);
    check(countingShaderLiveCount() == 2, "A.3: the count is 2 after setSurface on two GMANAttributes");

    GMANAttributes const copyOfAttrA(attrA); // AttributeBegin's own copy
    check(countingShaderLiveCount() == 2, "A.3: copying a GMANAttributes leaves the count at 2");

    appearanceA = gman::appearanceOf(attrA);
    appearanceB = gman::appearanceOf(attrB);
  }
  check(countingShaderLiveCount() == 2,
        "A.3: the count stays 2 once every GMANAttributes holding the shader is destroyed");

  gman::SurfacePoint point;
  point.P = GMANPoint(0.0f, 0.0f, 0.0f);
  point.N = GMANNormal(0.0f, 0.0f, 1.0f);
  point.Ng = GMANNormal(0.0f, 0.0f, 1.0f);
  point.I = GMANVector(0.0f, 0.0f, -1.0f);
  point.E = GMANPoint(0.0f, 0.0f, 5.0f);
  GMANMatrix4 const cameraToWorld;

  gman::Shading const shadingA = gman::shade(appearanceA, point, cameraToWorld);
  gman::Shading const shadingB = gman::shade(appearanceB, point, cameraToWorld);
  check(colorNear(shadingA.Ci, csA, kTol), "A.3: shading through the first Appearance returns its expected colour");
  check(colorNear(shadingB.Ci, csB, kTol), "A.3: shading through the second Appearance returns its expected colour");

  appearanceA.shader.reset();
  appearanceB.shader.reset();
  check(countingShaderLiveCount() == 0, "A.3: the count returns to 0 once every Appearance releases the shader");
}

// A4, in process: nodestroyshader defines GMANLoadShader but not
// GMANDestroyShader, standing in for a plugin built before this change.
void checkRefusalInProcess() {
  bool threw = false;
  RtInt code = 0;
  std::string message;
  try {
    GMANLoadableShader const loader("libnodestroyshader.so", GMANParameterList());
  } catch (GMANError const& error) {
    threw = true;
    code = error.getCode();
    message = error.getMessage();
  }
  check(threw, "A.4 in-process: constructing GMANLoadableShader on nodestroyshader throws");
  check(code == RIE_NOSHADER, "A.4 in-process: the thrown error carries RIE_NOSHADER");
  check(message.find("GMANDestroyShader") != std::string::npos,
        "A.4 in-process: the thrown error names GMANDestroyShader");

  GMANAttributes attr;
  GMANParameterList const emptyPl;
  attr.setSurface("nodestroyshader", emptyPl); // must throw nothing
  check(attr.getSurface(0.0) == nullptr, "A.4 in-process: setSurface leaves getSurface() null");

  gman::Appearance const appearance = gman::appearanceOf(attr);
  check(appearance.shader != nullptr, "A.4 in-process: appearanceOf falls back to the default surface");
}

} // namespace

int main() {
  checkDistinctInstances();
  checkInterleavedShading();
  checkLifetimeAndDestruction();
  checkRefusalInProcess();

  return checkSummary("one shader instance per Surface call: distinct, correctly shaded, correctly destroyed, and a "
                      "plugin missing GMANDestroyShader is refused");
}
