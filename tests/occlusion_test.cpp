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
 * R7 proof, check 1: GMANSurfaceEnv's occlusion hook. diffuse() and
 * specular() scale each non-ambient light's colour by
 * occluder->transmission(), per channel -- not one channel applied to all
 * three; ambient() never consults it, having no direction to occlude. A
 * point light reports its own distance and a unit towardLight; a distant
 * light reports RI_INFINITY, its sample()'s l having no useful length.
 */

#include <cmath>
#include <string>

#include "check.h"
#include "gmancolor.h"
#include "gmanlightsourcemgr.h"
#include "gmanocclude.h"
#include "gmanpoint.h"
#include "gmanshaderenvironment.h"
#include "gmanvector.h"
#include "ri.h"

namespace {

constexpr RtFloat kTol = 1e-5f;

bool colorNear(GMANColor const& a, GMANColor const& b, RtFloat tol) {
  return std::fabs(a.getRed() - b.getRed()) <= tol && std::fabs(a.getGreen() - b.getGreen()) <= tol &&
         std::fabs(a.getBlue() - b.getBlue()) <= tol;
}

GMANColor scaledPerChannel(GMANColor const& c, GMANColor const& by) {
  return GMANColor(c.getRed() * by.getRed(), c.getGreen() * by.getGreen(), c.getBlue() * by.getBlue());
}

// Returns a fixed transmission for every light. diffuse() and specular()
// each call transmission() once per non-ambient light per env method
// call; setPhase before each call routes what it reports into that
// phase's own Record rather than the other's, so a caller can inspect
// diffuse()'s own point/distant-light arguments after specular() has since
// run too.
class RecordingOccluder : public gman::Occluder {
public:
  enum class Phase { kDiffuse, kSpecular };

  explicit RecordingOccluder(GMANColor transmission) : transmission_(transmission) {}

  void setPhase(Phase phase) { phase_ = phase; }

  struct Record {
    GMANVector pointTowardLight;
    RtFloat pointDistance = -1.0f;
    RtFloat distantDistance = -1.0f;
  };
  Record const& diffuseRecord() const { return diffuseRecord_; }
  Record const& specularRecord() const { return specularRecord_; }

  GMANColor transmission(GMANLight const& light, GMANPoint const& /*P*/, GMANVector const& towardLight,
                         RtFloat distance) const override {
    Record& record = (phase_ == Phase::kDiffuse) ? diffuseRecord_ : specularRecord_;
    if (light.getType() == GMAN_LIGHT_POINT) {
      record.pointTowardLight = towardLight;
      record.pointDistance = distance;
    } else if (light.getType() == GMAN_LIGHT_DISTANT) {
      record.distantDistance = distance;
    }
    return transmission_;
  }

private:
  GMANColor transmission_;
  Phase phase_ = Phase::kDiffuse;
  mutable Record diffuseRecord_;
  mutable Record specularRecord_;
};

// diffuse() and specular() at transmission, checked against expected --
// baseline scaled per channel by transmission -- and ambient() checked
// unchanged. Returns the occluder so the caller can inspect its recorded
// per-light arguments afterward.
RecordingOccluder checkScaledByTransmission(GMANSurfaceEnv& env, GMANVector const& n, GMANVector const& v,
                                            RtFloat roughness, GMANColor const& ambientBaseline,
                                            GMANColor const& diffuseBaseline, GMANColor const& specularBaseline,
                                            GMANColor const& transmission, std::string const& label) {
  RecordingOccluder occluder(transmission);
  env.occluder = &occluder;

  check(colorNear(env.ambient(), ambientBaseline, kTol), label + ": ambient() is unchanged");

  occluder.setPhase(RecordingOccluder::Phase::kDiffuse);
  GMANColor const diffuseResult = env.diffuse(n);
  check(colorNear(diffuseResult, scaledPerChannel(diffuseBaseline, transmission), kTol),
        label + ": diffuse() scales the baseline per channel");

  occluder.setPhase(RecordingOccluder::Phase::kSpecular);
  GMANColor const specularResult = env.specular(n, v, roughness);
  check(colorNear(specularResult, scaledPerChannel(specularBaseline, transmission), kTol),
        label + ": specular() scales the baseline per channel");

  return occluder;
}

} // namespace

int main() {
  GMANLight ambient(GMAN_LIGHT_AMBIENT, GMANColor(0.2f, 0.2f, 0.2f), GMANPoint(), GMANVector());
  // direction is light -> scene; (0,0,-1) makes toward-light (0,0,1), the
  // same side as the point light below, so both pass diffuse's N.L and
  // specular's N.H gate against n = (0,0,1).
  GMANLight distant(GMAN_LIGHT_DISTANT, GMANColor(1.0f, 1.0f, 1.0f), GMANPoint(), GMANVector(0.0f, 0.0f, -1.0f));
  GMANPoint const pointLightPos(0.0f, 0.0f, 5.0f);
  GMANLight point(GMAN_LIGHT_POINT, GMANColor(1.0f, 1.0f, 1.0f), pointLightPos, GMANVector());

  GMANSurfaceEnv env;
  env.P = GMANPoint(0.0f, 0.0f, 0.0f);
  env.lights = {&ambient, &distant, &point};

  GMANVector const n(0.0f, 0.0f, 1.0f);
  GMANVector const v(0.0f, 0.0f, 1.0f);
  RtFloat const roughness = 0.5f;

  GMANColor const ambientBaseline = env.ambient();
  GMANColor const diffuseBaseline = env.diffuse(n);
  GMANColor const specularBaseline = env.specular(n, v, roughness);

  check(diffuseBaseline.getRed() > 0.0f, "setup: the null-occluder diffuse baseline is lit");
  check(specularBaseline.getRed() > 0.0f, "setup: the null-occluder specular baseline is lit");

  checkScaledByTransmission(env, n, v, roughness, ambientBaseline, diffuseBaseline, specularBaseline,
                            GMANColor(1.0f, 1.0f, 1.0f), "white transmission");
  checkScaledByTransmission(env, n, v, roughness, ambientBaseline, diffuseBaseline, specularBaseline,
                            GMANColor(0.0f, 0.0f, 0.0f), "black transmission");
  checkScaledByTransmission(env, n, v, roughness, ambientBaseline, diffuseBaseline, specularBaseline,
                            GMANColor(0.5f, 0.5f, 0.5f), "half-grey transmission");
  // Non-grey: a per-channel bug that applies one channel's own value to
  // every channel (e.g. red's alone) fails this where a grey transmission
  // could not tell the difference.
  RecordingOccluder const nonGrey =
      checkScaledByTransmission(env, n, v, roughness, ambientBaseline, diffuseBaseline, specularBaseline,
                                GMANColor(1.0f, 0.5f, 0.0f), "non-grey transmission");

  // ---- per-light distance and direction, from diffuse()'s own record --
  // specular() ran after it above and would previously have overwritten a
  // single shared record with its own last call. A diffuse() that passed
  // RI_INFINITY for every light, point included, fails the check below.
  RecordingOccluder::Record const& diffuseRecord = nonGrey.diffuseRecord();
  check(diffuseRecord.distantDistance == RI_INFINITY,
        "distant light: diffuse()'s transmission() call receives RI_INFINITY");
  check(std::fabs(diffuseRecord.pointDistance - 5.0f) <= kTol,
        "point light: diffuse()'s transmission() call receives its own distance (5.0)");
  GMANVector const expectedTowardLight(0.0f, 0.0f, 1.0f);
  check(std::fabs(diffuseRecord.pointTowardLight.getX() - expectedTowardLight.getX()) <= kTol &&
            std::fabs(diffuseRecord.pointTowardLight.getY() - expectedTowardLight.getY()) <= kTol &&
            std::fabs(diffuseRecord.pointTowardLight.getZ() - expectedTowardLight.getZ()) <= kTol,
        "point light: diffuse()'s transmission() call receives a unit towardLight");

  return checkSummary(
      "GMANSurfaceEnv's occlusion hook: diffuse()/specular() consult it per channel, ambient() does not");
}
