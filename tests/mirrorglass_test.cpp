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
 * R8 proof, §8 D: mirror and glass, loaded exactly as defaultSurfaceShader()
 * loads matte -- a real .so, not gmanmirror.cpp/gmanglass.cpp compiled
 * directly into this binary, since both define extern "C"
 * GMANGetLoadableInfo/GMANLoadShader at global scope and linking both would
 * be a duplicate-symbol error.
 *
 * Every hand-computed reflected/refracted direction below is worked out
 * independently -- the reflect/refract formulas re-derived inline, not by
 * calling GMANReflect/GMANRefract/GMANSurfaceEnv::reflect/refract again --
 * so a shared sign bug in both the shader and this test cannot cancel out
 * (see the "swap reflect's sign" mutation below).
 */

#include <cmath>
#include <memory>
#include <string>

#include "check.h"
#include "gmandictionary.h"
#include "gmanloadable.h"
#include "gmanloadableshader.h"
#include "gmanparameterlist.h"
#include "gmanshaderenvironment.h"
#include "gmanshading.h"
#include "gmansurfaceshader.h"
#include "gmantrace.h"
#include "gmanvector.h"
#include "ri.h"

namespace {

constexpr RtFloat kTol = (RtFloat)1.0e-4;
constexpr RtFloat kDegToRad = (RtFloat)(3.14159265358979323846 / 180.0);

bool near(RtFloat a, RtFloat b, RtFloat tol) { return std::fabs(a - b) <= tol; }

bool vectorNear(GMANVector const& a, GMANVector const& b, RtFloat tol) {
  return near(a.getX(), b.getX(), tol) && near(a.getY(), b.getY(), tol) && near(a.getZ(), b.getZ(), tol);
}

bool colorNear(GMANColor const& a, GMANColor const& b, RtFloat tol) {
  return near(a.getRed(), b.getRed(), tol) && near(a.getGreen(), b.getGreen(), tol) &&
         near(a.getBlue(), b.getBlue(), tol);
}

bool colorExactly(GMANColor const& a, GMANColor const& b) {
  return a.getRed() == b.getRed() && a.getGreen() == b.getGreen() && a.getBlue() == b.getBlue();
}

GMANVector obliqueDirection(RtFloat degrees) {
  RtFloat const radians = degrees * kDegToRad;
  return GMANVector(std::sin(radians), (RtFloat)0.0, -std::cos(radians));
}

// The reflect/refract formulas, re-derived here rather than reached
// through GMANReflect/GMANRefract -- see this file's own header comment.
GMANVector handReflect(GMANVector const& i, GMANVector const& n) {
  RtFloat const c = i.dot(n);
  return GMANVector(i.getX() - n.getX() * 2.0f * c, i.getY() - n.getY() * 2.0f * c, i.getZ() - n.getZ() * 2.0f * c);
}

GMANVector handRefract(GMANVector const& i, GMANVector const& n, RtFloat eta) {
  RtFloat const c = i.dot(n);
  RtFloat const k = 1.0f - eta * eta * (1.0f - c * c);
  if (k < 0.0f) {
    return GMANVector(0.0f, 0.0f, 0.0f);
  }
  RtFloat const factor = eta * c + std::sqrt(k);
  return GMANVector(i.getX() * eta - n.getX() * factor, i.getY() * eta - n.getY() * factor,
                    i.getZ() * eta - n.getZ() * factor);
}

// The faceforward formula, re-derived independently for the same reason.
GMANVector handFaceforward(GMANVector const& n, GMANVector const& i, GMANVector const& nr) {
  return (nr.dot(i) < 0.0f) ? n : GMANVector(-n.getX(), -n.getY(), -n.getZ());
}

GMANParameterList krParams(RtFloat kr) {
  static GMANDictionary dictionary;
  RtToken tokens[1] = {RI_KR};
  RtPointer parms[1] = {&kr};
  return GMANParameterList(dictionary, 1, tokens, parms);
}

// Records its own last P/R/Ng and always returns a fixed colour.
class FixedAnswerTracer : public gman::Tracer {
public:
  explicit FixedAnswerTracer(GMANColor answer) : answer_(answer) {}
  GMANColor trace(GMANPoint const& P, GMANVector const& R, GMANVector const& Ng,
                  RtFloat /*surfaceMagnitude*/) const override {
    lastP_ = P;
    lastR_ = R;
    lastNg_ = Ng;
    ++callCount_;
    return answer_;
  }
  int callCount() const { return callCount_; }
  GMANVector const& lastR() const { return lastR_; }

private:
  GMANColor answer_;
  mutable int callCount_ = 0;
  mutable GMANPoint lastP_;
  mutable GMANVector lastR_;
  mutable GMANVector lastNg_;
};

// Records two distinct calls' own P/R/Ng and answers a distinct fixed
// colour for each, by call order -- glass's own trace(Rr) then trace(Rt).
class TwoAnswerTracer : public gman::Tracer {
public:
  struct Record {
    GMANPoint P;
    GMANVector R;
    GMANVector Ng;
  };

  TwoAnswerTracer(GMANColor first, GMANColor second) : first_(first), second_(second) {}

  GMANColor trace(GMANPoint const& P, GMANVector const& R, GMANVector const& Ng,
                  RtFloat /*surfaceMagnitude*/) const override {
    Record& record = (callCount_ == 0) ? recordFirst_ : recordSecond_;
    record.P = P;
    record.R = R;
    record.Ng = Ng;
    GMANColor const answer = (callCount_ == 0) ? first_ : second_;
    ++callCount_;
    return answer;
  }

  int callCount() const { return callCount_; }
  Record const& recordFirst() const { return recordFirst_; }
  Record const& recordSecond() const { return recordSecond_; }

private:
  GMANColor first_;
  GMANColor second_;
  mutable int callCount_ = 0;
  mutable Record recordFirst_;
  mutable Record recordSecond_;
};

// Bound only to the outer gman::shade call in checkMirrorReentrant: its
// one trace() call recurses once more through gman::shade with the inner
// Appearance, terminating at terminatingTracer -- a second, non-recursing
// Tracer.
class ReenteringMirrorTracer : public gman::Tracer {
public:
  ReenteringMirrorTracer(gman::Appearance const& innerAppearance, gman::SurfacePoint const& innerPoint,
                         GMANMatrix4 const& cameraToWorld, gman::Tracer const& terminatingTracer)
      : innerAppearance_(innerAppearance), innerPoint_(innerPoint), cameraToWorld_(cameraToWorld),
        terminatingTracer_(terminatingTracer) {}

  GMANColor trace(GMANPoint const& /*P*/, GMANVector const& /*R*/, GMANVector const& /*Ng*/,
                  RtFloat /*surfaceMagnitude*/) const override {
    gman::Shading const inner =
        gman::shade(innerAppearance_, innerPoint_, cameraToWorld_, nullptr, &terminatingTracer_);
    return inner.Ci;
  }

private:
  gman::Appearance const& innerAppearance_;
  gman::SurfacePoint const& innerPoint_;
  GMANMatrix4 const& cameraToWorld_;
  gman::Tracer const& terminatingTracer_;
};

// Bound only to the outer gman::shade call in checkGlassReentrant: both of
// its two trace() calls (Rr then Rt) recurse into their own inner
// SurfacePoint and their own terminating Tracer.
class ReenteringGlassTracer : public gman::Tracer {
public:
  ReenteringGlassTracer(gman::Appearance const& innerAppearance, gman::SurfacePoint const& innerPointA,
                        gman::Tracer const& terminatingA, gman::SurfacePoint const& innerPointB,
                        gman::Tracer const& terminatingB, GMANMatrix4 const& cameraToWorld)
      : innerAppearance_(innerAppearance), innerPointA_(innerPointA), terminatingA_(terminatingA),
        innerPointB_(innerPointB), terminatingB_(terminatingB), cameraToWorld_(cameraToWorld) {}

  GMANColor trace(GMANPoint const& /*P*/, GMANVector const& /*R*/, GMANVector const& /*Ng*/,
                  RtFloat /*surfaceMagnitude*/) const override {
    if (callCount_ == 0) {
      ++callCount_;
      return gman::shade(innerAppearance_, innerPointA_, cameraToWorld_, nullptr, &terminatingA_).Ci;
    }
    ++callCount_;
    return gman::shade(innerAppearance_, innerPointB_, cameraToWorld_, nullptr, &terminatingB_).Ci;
  }

  int callCount() const { return callCount_; }

private:
  gman::Appearance const& innerAppearance_;
  gman::SurfacePoint const& innerPointA_;
  gman::Tracer const& terminatingA_;
  gman::SurfacePoint const& innerPointB_;
  gman::Tracer const& terminatingB_;
  GMANMatrix4 const& cameraToWorld_;
  mutable int callCount_ = 0;
};

// Check D.1: mirror's Ci at a non-normal incidence equals Os*Cs*Kr times
// the recording tracer's return, for the hand-computed reflected
// direction, at Kr's default (1) and at a non-default value -- two
// instances, one per Kr, since Kr binds once, at construction.
void checkMirrorCi(std::string const& mirrorPath) {
  GMANSurfaceEnv se;
  se.Os = GMANColor(1.0f, 1.0f, 1.0f);
  se.Cs = GMANColor(0.4f, 0.6f, 0.8f);
  se.P = GMANPoint(0.0f, 0.0f, 0.0f);
  se.N = GMANNormal(0.0f, 0.0f, 1.0f);
  se.Ng = GMANNormal(0.0f, 0.0f, 1.0f);
  se.I = obliqueDirection(25.0f);

  FixedAnswerTracer tracer(GMANColor(0.2f, 0.3f, 0.4f));
  se.tracer = &tracer;

  GMANVector const n(se.N.getX(), se.N.getY(), se.N.getZ());
  GMANVector const ng(se.Ng.getX(), se.Ng.getY(), se.Ng.getZ());
  GMANVector const nf = handFaceforward(n, se.I, ng);
  GMANVector const expectedR = handReflect(se.I, nf);

  GMANLoadableShader defaultLoader(mirrorPath.c_str(), GMANParameterList());
  GMANColor const ciDefault = defaultLoader.getSurface()->computeCi(se);
  GMANColor const wantDefault(se.Os.getRed() * se.Cs.getRed() * 1.0f * 0.2f,
                              se.Os.getGreen() * se.Cs.getGreen() * 1.0f * 0.3f,
                              se.Os.getBlue() * se.Cs.getBlue() * 1.0f * 0.4f);
  check(colorNear(ciDefault, wantDefault, kTol), "D.1: mirror's Ci at default Kr equals Os*Cs*Kr*trace()");
  check(tracer.callCount() == 1, "D.1: mirror calls trace() exactly once");
  check(vectorNear(tracer.lastR(), expectedR, kTol), "D.1: mirror traces the hand-computed reflected direction");

  RtFloat const nonDefaultKr = 0.4f;
  GMANLoadableShader nonDefaultLoader(mirrorPath.c_str(), krParams(nonDefaultKr));
  GMANColor const ciNonDefault = nonDefaultLoader.getSurface()->computeCi(se);
  GMANColor const wantNonDefault(se.Os.getRed() * se.Cs.getRed() * nonDefaultKr * 0.2f,
                                 se.Os.getGreen() * se.Cs.getGreen() * nonDefaultKr * 0.3f,
                                 se.Os.getBlue() * se.Cs.getBlue() * nonDefaultKr * 0.4f);
  check(colorNear(ciNonDefault, wantNonDefault, kTol), "D.1: mirror's Ci at a non-default Kr equals Os*Cs*Kr*trace()");
}

// Check D.2: glass's Ci, entering and exiting, both with hand-computed
// expected directions, both scaled by Os alone (no Cs factor).
void checkGlassCi(std::string const& glassPath) {
  GMANLoadableShader loader(glassPath.c_str(), GMANParameterList()); // glass reads no parameter
  GMANSurfaceShader const* glassShader = loader.getSurface();

  GMANNormal const N(0.0f, 0.0f, 1.0f);
  GMANNormal const Ng(0.0f, 0.0f, 1.0f);
  GMANVector const n(0.0f, 0.0f, 1.0f);
  GMANVector const ng(0.0f, 0.0f, 1.0f);
  RtFloat const ior = 1.5f;

  // ---- entering: Ng . I < 0, eta = 1/ior, Nf == N ----
  {
    GMANSurfaceEnv se;
    se.Os = GMANColor(1.0f, 1.0f, 1.0f);
    se.P = GMANPoint(0.0f, 0.0f, 0.0f);
    se.N = N;
    se.Ng = Ng;
    se.I = obliqueDirection(20.0f); // (sin20, 0, -cos20): Ng.I < 0

    TwoAnswerTracer tracer(GMANColor(0.3f, 0.4f, 0.5f), GMANColor(0.6f, 0.1f, 0.2f));
    se.tracer = &tracer;

    RtFloat const eta = 1.0f / ior;
    GMANVector const nf = handFaceforward(n, se.I, ng);
    GMANVector const rr = handReflect(se.I, nf);
    GMANVector const rt = handRefract(se.I, nf, eta);
    RtFloat kr = 0.0f, kt = 0.0f;
    se.fresnel(se.I, nf, eta, kr, kt);

    GMANColor const ci = glassShader->computeCi(se);
    GMANColor const want(se.Os.getRed() * (kr * 0.3f + kt * 0.6f), se.Os.getGreen() * (kr * 0.4f + kt * 0.1f),
                         se.Os.getBlue() * (kr * 0.5f + kt * 0.2f));
    check(colorNear(ci, want, kTol), "D.2 entering: glass's Ci equals Os*(kr*trace(Rr) + kt*trace(Rt))");
    check(vectorNear(tracer.recordFirst().R, rr, kTol), "D.2 entering: glass traces the hand-computed Rr first");
    check(vectorNear(tracer.recordSecond().R, rt, kTol), "D.2 entering: glass traces the hand-computed Rt second");
  }

  // ---- exiting: Ng . I >= 0, eta = ior, Nf == -N -- catches a shader
  // that always uses raw N, or one that always uses eta = 1/ior ----
  {
    GMANSurfaceEnv se;
    se.Os = GMANColor(1.0f, 1.0f, 1.0f);
    se.P = GMANPoint(0.0f, 0.0f, 0.0f);
    se.N = N;
    se.Ng = Ng;
    RtFloat const radians = 20.0f * kDegToRad;
    se.I = GMANVector(std::sin(radians), 0.0f, std::cos(radians)); // Ng.I >= 0

    TwoAnswerTracer tracer(GMANColor(0.3f, 0.4f, 0.5f), GMANColor(0.6f, 0.1f, 0.2f));
    se.tracer = &tracer;

    RtFloat const eta = ior;
    GMANVector const nf = handFaceforward(n, se.I, ng); // == -n here
    GMANVector const rr = handReflect(se.I, nf);
    GMANVector const rt = handRefract(se.I, nf, eta);
    RtFloat kr = 0.0f, kt = 0.0f;
    se.fresnel(se.I, nf, eta, kr, kt);

    GMANColor const ci = glassShader->computeCi(se);
    GMANColor const want(se.Os.getRed() * (kr * 0.3f + kt * 0.6f), se.Os.getGreen() * (kr * 0.4f + kt * 0.1f),
                         se.Os.getBlue() * (kr * 0.5f + kt * 0.2f));
    check(colorNear(ci, want, kTol), "D.2 exiting: glass's Ci equals Os*(kr*trace(Rr) + kt*trace(Rt))");
    check(vectorNear(tracer.recordFirst().R, rr, kTol), "D.2 exiting: glass traces the hand-computed Rr first");
    check(vectorNear(tracer.recordSecond().R, rt, kTol), "D.2 exiting: glass traces the hand-computed Rt second");
  }
}

// Check D.3: glass at a TIR angle matches the reflection-only prediction
// (kr == 1), with no NaN or Inf anywhere in Ci.
void checkGlassTIR(std::string const& glassPath) {
  GMANLoadableShader loader(glassPath.c_str(), GMANParameterList());
  GMANSurfaceShader const* glassShader = loader.getSurface();

  GMANSurfaceEnv se;
  se.Os = GMANColor(1.0f, 1.0f, 1.0f);
  se.P = GMANPoint(0.0f, 0.0f, 0.0f);
  se.N = GMANNormal(0.0f, 0.0f, 1.0f);
  se.Ng = GMANNormal(0.0f, 0.0f, 1.0f);
  RtFloat const radians = 60.0f * kDegToRad;                     // past asin(1/1.5) =~ 41.81 degrees
  se.I = GMANVector(std::sin(radians), 0.0f, std::cos(radians)); // exiting

  TwoAnswerTracer tracer(GMANColor(0.3f, 0.4f, 0.5f), GMANColor(0.6f, 0.1f, 0.2f));
  se.tracer = &tracer;

  GMANColor const ci = glassShader->computeCi(se);
  GMANColor const want = GMANColor(0.3f, 0.4f, 0.5f); // Os * (1 * traced(Rr) + 0 * traced(Rt))
  check(colorNear(ci, want, kTol), "D.3: glass at TIR matches the reflection-only prediction");
  check(std::isfinite(ci.getRed()) && std::isfinite(ci.getGreen()) && std::isfinite(ci.getBlue()),
        "D.3: glass at TIR produces no NaN or Inf in Ci");
}

// Check D.4: both shaders' computeOi returns Os unchanged.
void checkComputeOiUnchanged(std::string const& mirrorPath, std::string const& glassPath) {
  GMANSurfaceEnv se;
  se.Os = GMANColor(0.3f, 0.6f, 0.9f);

  GMANLoadableShader mirrorLoader(mirrorPath.c_str(), GMANParameterList());
  GMANColor const mirrorOi = mirrorLoader.getSurface()->computeOi(se);
  check(colorExactly(mirrorOi, se.Os), "D.4: mirror's computeOi returns Os unchanged");

  GMANLoadableShader glassLoader(glassPath.c_str(), GMANParameterList());
  GMANColor const glassOi = glassLoader.getSurface()->computeOi(se);
  check(colorExactly(glassOi, se.Os), "D.4: glass's computeOi returns Os unchanged");
}

// Check D.5 (mirror): re-entrant shading through two distinct instances
// stays correct. The outer call's Ci uses the outer Kr (1), not the
// inner's (0.25): each instance's own Kr comes from its own construction,
// so nothing left to rebind can leak between them.
void checkMirrorReentrant(std::string const& mirrorPath) {
  GMANColor const outerCs(0.5f, 0.25f, 0.125f);
  GMANColor const innerCs(0.25f, 0.5f, 0.125f);
  GMANColor const terminatingAnswer(0.5f, 0.25f, 0.5f);

  gman::SurfacePoint point;
  point.P = GMANPoint(0.0f, 0.0f, 0.0f);
  point.N = GMANNormal(0.0f, 0.0f, 1.0f);
  point.Ng = GMANNormal(0.0f, 0.0f, 1.0f);
  point.I = GMANVector(0.0f, 0.0f, -1.0f);
  point.E = GMANPoint(0.0f, 0.0f, 5.0f);

  auto const outerLoader = std::make_shared<GMANLoadableShader>(mirrorPath.c_str(), krParams(1.0f));
  auto const innerLoader = std::make_shared<GMANLoadableShader>(mirrorPath.c_str(), krParams(0.25f));

  gman::Appearance outerAppearance;
  outerAppearance.shader = std::shared_ptr<GMANSurfaceShader const>(outerLoader, outerLoader->getSurface());
  outerAppearance.Cs = outerCs;
  outerAppearance.Os = GMANColor(1.0f, 1.0f, 1.0f);

  gman::Appearance innerAppearance;
  innerAppearance.shader = std::shared_ptr<GMANSurfaceShader const>(innerLoader, innerLoader->getSurface());
  innerAppearance.Cs = innerCs;
  innerAppearance.Os = GMANColor(1.0f, 1.0f, 1.0f);

  FixedAnswerTracer terminatingTracer(terminatingAnswer);
  GMANMatrix4 const cameraToWorld;
  ReenteringMirrorTracer reenteringTracer(innerAppearance, point, cameraToWorld, terminatingTracer);

  gman::Shading const outer = gman::shade(outerAppearance, point, cameraToWorld, nullptr, &reenteringTracer);

  // inner Ci = innerOs * innerCs * innerKr(0.25) * terminatingAnswer
  GMANColor const innerCi(innerCs.getRed() * 0.25f * terminatingAnswer.getRed(),
                          innerCs.getGreen() * 0.25f * terminatingAnswer.getGreen(),
                          innerCs.getBlue() * 0.25f * terminatingAnswer.getBlue());
  // outer Ci = outerOs * outerCs * outerKr(1) * innerCi
  GMANColor const wantOuterCi(outerCs.getRed() * 1.0f * innerCi.getRed(),
                              outerCs.getGreen() * 1.0f * innerCi.getGreen(),
                              outerCs.getBlue() * 1.0f * innerCi.getBlue());

  check(colorExactly(outer.Ci, wantOuterCi),
        "D.5 mirror: the outer call's Ci uses the outer Kr, not the inner's, exactly");
}

// Check D.5 (glass): re-entrant shading through one shared instance stays
// correct, both of the outer call's trace() calls recursing into their
// own inner SurfacePoint and their own terminating colour. glass reads no
// parameter, so inner and outer sharing one instance proves nothing about
// per-instance state the way checkMirrorReentrant's two instances do;
// grazing incidence at the outer call pins kr = 1, kt = 0 exactly, so the
// predicted composite is exactly representable.
//
// This pins only the reflected branch's own contribution to the outer
// Ci: kt == 0 makes branch B's own composite value vanish from it
// regardless of what that value is, so callCount() == 2 is this check's
// only evidence branch B's own recursion ran at all, not that its
// result combined correctly. checkMirrorReentrant and the callCount()
// == 2 assertion below are what stand in for that; a genuinely oblique
// outer angle, with both kr and kt nonzero and a tolerance instead of
// an exact match, would pin it directly.
void checkGlassReentrant(std::string const& glassPath) {
  GMANColor const answerA(0.5f, 0.25f, 0.75f);     // branch A: Rr
  GMANColor const answerB(0.125f, 0.625f, 0.375f); // branch B: Rt

  gman::SurfacePoint innerPointA;
  innerPointA.P = GMANPoint(0.0f, 0.0f, 0.0f);
  innerPointA.N = GMANNormal(0.0f, 0.0f, 1.0f);
  innerPointA.Ng = GMANNormal(0.0f, 0.0f, 1.0f);
  innerPointA.I = GMANVector(0.0f, 0.0f, -1.0f);
  innerPointA.E = GMANPoint(0.0f, 0.0f, 5.0f);

  gman::SurfacePoint innerPointB = innerPointA;

  auto const loader = std::make_shared<GMANLoadableShader>(glassPath.c_str(), GMANParameterList());
  std::shared_ptr<GMANSurfaceShader const> const glassShader(loader, loader->getSurface());

  gman::Appearance innerAppearance;
  innerAppearance.shader = glassShader;
  innerAppearance.Os = GMANColor(1.0f, 1.0f, 1.0f);

  FixedAnswerTracer terminatingA(answerA);
  FixedAnswerTracer terminatingB(answerB);
  GMANMatrix4 const cameraToWorld;
  ReenteringGlassTracer reenteringTracer(innerAppearance, innerPointA, terminatingA, innerPointB, terminatingB,
                                         cameraToWorld);

  gman::Appearance outerAppearance;
  outerAppearance.shader = glassShader;
  outerAppearance.Os = GMANColor(1.0f, 1.0f, 1.0f);

  gman::SurfacePoint outerPoint;
  outerPoint.P = GMANPoint(0.0f, 0.0f, 0.0f);
  outerPoint.N = GMANNormal(0.0f, 0.0f, 1.0f);
  outerPoint.Ng = GMANNormal(0.0f, 0.0f, 1.0f);
  outerPoint.I = GMANVector(1.0f, 0.0f, 0.0f); // grazing: Ng.I == 0
  outerPoint.E = GMANPoint(0.0f, 0.0f, 5.0f);

  gman::Shading const outer = gman::shade(outerAppearance, outerPoint, cameraToWorld, nullptr, &reenteringTracer);

  // Each inner glass call's own kr_inner + kt_inner == 1 exactly, and both
  // of its own trace() calls answer the same fixed colour regardless of
  // direction, so inner Ci == Os_inner * answer exactly, independent of
  // the inner call's own fresnel split. The outer call's own kr == 1,
  // kt == 0 (grazing), so only branch A's answer survives.
  GMANColor const wantOuterCi = answerA;
  check(colorExactly(outer.Ci, wantOuterCi),
        "D.5 glass: the outer call's Ci combines its own kr/kt with both recursive results correctly");
  check(reenteringTracer.callCount() == 2, "D.5 glass: both of the outer call's trace() calls recursed");
}

} // namespace

int main() {
  std::string const mirrorPath = "libmirror.so";
  std::string const glassPath = "libglass.so";

  checkMirrorCi(mirrorPath);
  checkGlassCi(glassPath);
  checkGlassTIR(glassPath);
  checkComputeOiUnchanged(mirrorPath, glassPath);
  checkMirrorReentrant(mirrorPath);
  checkGlassReentrant(glassPath);

  return checkSummary("mirror and glass: Ci matches the hand-computed prediction, computeOi passes Os through, "
                      "and re-entrant shading through their own instances stays correct");
}
