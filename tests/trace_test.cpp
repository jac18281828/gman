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
 * R8 proof, §8 A: the tracer hook. A null tracer degrades trace() to
 * black, a bound one is called with this env's own P/Ng and the caller's
 * R unchanged, and gman::shade forwards its own tracer argument through to
 * the shader it runs.
 */

#include <cmath>

#include "check.h"
#include "gmanlightsourcemgr.h"
#include "gmanocclude.h"
#include "gmanshaderenvironment.h"
#include "gmanshading.h"
#include "gmansurfaceshader.h"
#include "gmantrace.h"
#include "gmanvector.h"
#include "ri.h"

namespace {

constexpr RtFloat kTol = (RtFloat)1.0e-6;

bool vectorNear(GMANVector const& a, GMANVector const& b, RtFloat tol) {
  return std::fabs(a.getX() - b.getX()) <= tol && std::fabs(a.getY() - b.getY()) <= tol &&
         std::fabs(a.getZ() - b.getZ()) <= tol;
}

// Records its own P/R/Ng arguments and always returns a fixed colour, so
// a caller can confirm both that it ran and what it was asked.
class RecordingTracer : public gman::Tracer {
public:
  explicit RecordingTracer(GMANColor answer) : answer_(answer) {}

  GMANColor trace(GMANPoint const& P, GMANVector const& R, GMANVector const& Ng,
                  RtFloat surfaceMagnitude) const override {
    ++callCount_;
    lastP_ = P;
    lastR_ = R;
    lastNg_ = Ng;
    lastSurfaceMagnitude_ = surfaceMagnitude;
    return answer_;
  }

  int callCount() const { return callCount_; }
  GMANPoint const& lastP() const { return lastP_; }
  GMANVector const& lastR() const { return lastR_; }
  GMANVector const& lastNg() const { return lastNg_; }
  RtFloat lastSurfaceMagnitude() const { return lastSurfaceMagnitude_; }

private:
  GMANColor answer_;
  mutable int callCount_ = 0;
  mutable GMANPoint lastP_;
  mutable GMANVector lastR_;
  mutable GMANVector lastNg_;
  mutable RtFloat lastSurfaceMagnitude_ = 0.0;
};

// Calls se.trace() once, unconditionally, with a fixed direction, and
// returns whatever it answers -- the minimum shader that lets a test
// observe trace()'s own plumbing through gman::shade.
class TraceProbeShader : public GMANSurfaceShader {
public:
  const GMANColor& computeCi(GMANSurfaceEnv& se) override {
    ci_ = se.trace(GMANVector(0.0f, 0.0f, -1.0f));
    return ci_;
  }
  const GMANColor& computeOi(GMANSurfaceEnv& se) override {
    oi_ = se.Os;
    return oi_;
  }

private:
  GMANColor ci_;
  GMANColor oi_;
};

// Records its own last surfaceMagnitude argument, white transmission
// otherwise -- the minimum Occluder that lets a test observe
// occludedContribution's own forwarding of env.surfaceMagnitude.
class RecordingOccluder : public gman::Occluder {
public:
  GMANColor transmission(GMANLight const& /*light*/, GMANPoint const& /*P*/, GMANVector const& /*towardLight*/,
                         GMANVector const& /*Ng*/, RtFloat /*distance*/, RtFloat surfaceMagnitude) const override {
    ++callCount_;
    lastSurfaceMagnitude_ = surfaceMagnitude;
    return GMANColor(1.0f, 1.0f, 1.0f);
  }

  int callCount() const { return callCount_; }
  RtFloat lastSurfaceMagnitude() const { return lastSurfaceMagnitude_; }

private:
  mutable int callCount_ = 0;
  mutable RtFloat lastSurfaceMagnitude_ = 0.0;
};

// Calls both se.trace() (reaching a bound Tracer) and se.diffuse()
// (reaching a bound Occluder, one light active and N.L > 0) -- the
// minimum shader that lets a test observe env.surfaceMagnitude's own
// forwarding to both hooks through one gman::shade call.
class MagnitudeProbeShader : public GMANSurfaceShader {
public:
  const GMANColor& computeCi(GMANSurfaceEnv& se) override {
    GMANVector const n(se.N.getX(), se.N.getY(), se.N.getZ());
    GMANColor const traced = se.trace(GMANVector(0.0f, 0.0f, -1.0f));
    GMANColor const lit = se.diffuse(n);
    ci_ =
        GMANColor(traced.getRed() + lit.getRed(), traced.getGreen() + lit.getGreen(), traced.getBlue() + lit.getBlue());
    return ci_;
  }
  const GMANColor& computeOi(GMANSurfaceEnv& se) override {
    oi_ = se.Os;
    return oi_;
  }

private:
  GMANColor ci_;
  GMANColor oi_;
};

void checkDefaultTraceIsBlack() {
  GMANSurfaceEnv env;
  GMANColor const black(0.0f, 0.0f, 0.0f);
  GMANVector const directions[3] = {
      GMANVector(1.0f, 0.0f, 0.0f),
      GMANVector(0.0f, 1.0f, 0.0f),
      GMANVector(0.3f, -0.6f, 0.8f),
  };
  for (GMANVector const& r : directions) {
    GMANColor const got = env.trace(r);
    check(got.getRed() == black.getRed() && got.getGreen() == black.getGreen() && got.getBlue() == black.getBlue(),
          "a default-constructed GMANSurfaceEnv's trace() returns black");
  }
}

void checkRecordingTracerReceivesEnvArguments() {
  GMANColor const answer(0.25f, 0.5f, 0.75f);
  RecordingTracer tracer(answer);

  GMANSurfaceEnv env;
  env.P = GMANPoint(1.0f, 2.0f, 3.0f);
  env.Ng = GMANNormal(0.0f, 0.0f, 1.0f);
  env.tracer = &tracer;

  GMANVector const r(0.5f, -0.5f, 0.7071f);
  GMANColor const got = env.trace(r);

  check(tracer.callCount() == 1, "env.trace() calls a bound tracer exactly once");
  check(got.getRed() == answer.getRed() && got.getGreen() == answer.getGreen() && got.getBlue() == answer.getBlue(),
        "env.trace() returns the tracer's own answer");

  GMANVector const p(env.P.getX(), env.P.getY(), env.P.getZ());
  GMANVector const lastP(tracer.lastP().getX(), tracer.lastP().getY(), tracer.lastP().getZ());
  check(vectorNear(lastP, p, kTol), "the tracer receives env.P unchanged");
  check(vectorNear(tracer.lastR(), r, kTol), "the tracer receives the given R unchanged");
  GMANVector const ng(env.Ng.getX(), env.Ng.getY(), env.Ng.getZ());
  check(vectorNear(tracer.lastNg(), ng, kTol), "the tracer receives env.Ng unchanged");
}

void checkShadeForwardsItsOwnTracer() {
  GMANColor const answer(0.1f, 0.2f, 0.3f);
  RecordingTracer tracer(answer);

  TraceProbeShader shader;
  gman::Appearance appearance;
  appearance.shader = &shader;
  appearance.Cs = GMANColor(1.0f, 1.0f, 1.0f);
  appearance.Os = GMANColor(1.0f, 1.0f, 1.0f);

  gman::SurfacePoint point;
  point.P = GMANPoint(0.0f, 0.0f, 0.0f);
  point.N = GMANNormal(0.0f, 0.0f, 1.0f);
  point.Ng = GMANNormal(0.0f, 0.0f, 1.0f);
  point.I = GMANVector(0.0f, 0.0f, -1.0f);
  point.E = GMANPoint(0.0f, 0.0f, 5.0f);

  GMANMatrix4 const cameraToWorld;
  gman::Shading const shading = gman::shade(appearance, point, cameraToWorld, /*occluder=*/nullptr, &tracer);

  check(tracer.callCount() == 1, "gman::shade reaches the shader's own se.trace() call exactly once");
  check(shading.Ci.getRed() == answer.getRed() && shading.Ci.getGreen() == answer.getGreen() &&
            shading.Ci.getBlue() == answer.getBlue(),
        "the shader's Ci is what the bound tracer answered");
}

// A SurfacePoint with a non-zero surfaceMagnitude reaches both a recording
// Tracer and a recording Occluder unchanged, through one gman::shade call
// -- checkShadeForwardsItsOwnTracer's own model, since a check on a
// hand-built env never passes through gman::shade at all.
void checkShadeForwardsSurfaceMagnitude() {
  RtFloat const magnitude = 42.5f;

  RecordingTracer tracer(GMANColor(0.1f, 0.1f, 0.1f));
  RecordingOccluder occluder;

  MagnitudeProbeShader shader;
  gman::Appearance appearance;
  appearance.shader = &shader;
  appearance.Cs = GMANColor(1.0f, 1.0f, 1.0f);
  appearance.Os = GMANColor(1.0f, 1.0f, 1.0f);

  GMANLight const light(GMAN_LIGHT_DISTANT, GMANColor(1.0f, 1.0f, 1.0f), GMANPoint(), GMANVector(0.0f, 0.0f, -1.0f));
  appearance.lights = {&light};

  gman::SurfacePoint point;
  point.P = GMANPoint(0.0f, 0.0f, 0.0f);
  point.N = GMANNormal(0.0f, 0.0f, 1.0f);
  point.Ng = GMANNormal(0.0f, 0.0f, 1.0f);
  point.I = GMANVector(0.0f, 0.0f, -1.0f);
  point.E = GMANPoint(0.0f, 0.0f, 5.0f);
  point.surfaceMagnitude = magnitude;

  GMANMatrix4 const cameraToWorld;
  gman::shade(appearance, point, cameraToWorld, &occluder, &tracer);

  check(tracer.callCount() == 1, "commit 2 forwarding: the shader's own trace() call reached the recording tracer");
  check(tracer.lastSurfaceMagnitude() == magnitude,
        "commit 2 forwarding: the recording tracer receives the point's own surfaceMagnitude unchanged");
  check(occluder.callCount() == 1,
        "commit 2 forwarding: the shader's own diffuse() call reached the recording occluder");
  check(occluder.lastSurfaceMagnitude() == magnitude,
        "commit 2 forwarding: the recording occluder receives the point's own surfaceMagnitude unchanged");
}

} // namespace

int main() {
  checkDefaultTraceIsBlack();
  checkRecordingTracerReceivesEnvArguments();
  checkShadeForwardsItsOwnTracer();
  checkShadeForwardsSurfaceMagnitude();

  return checkSummary("gman::Tracer: a null tracer degrades to black, a bound one sees env's own P/Ng and the "
                      "caller's R, and gman::shade forwards it to the shader");
}
