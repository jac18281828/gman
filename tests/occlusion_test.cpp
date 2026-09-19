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
 * occluder->transmission(); ambient() never consults it, having no
 * direction to occlude. A point light reports its own distance and a unit
 * towardLight; a distant light reports RI_INFINITY, its sample()'s l
 * having no useful length.
 */

#include <cmath>

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

// Returns a fixed transmission for every light and records what the point
// and distant lights reported, so the same pass through diffuse()/
// specular() proves both the scaling and the per-light distance/direction
// contract in one env setup.
class RecordingOccluder : public gman::Occluder {
public:
  explicit RecordingOccluder(GMANColor transmission) : transmission_(transmission) {}

  GMANColor transmission(GMANLight const& light, GMANPoint const& /*P*/, GMANVector const& towardLight,
                         RtFloat distance) const override {
    if (light.getType() == GMAN_LIGHT_POINT) {
      pointTowardLight = towardLight;
      pointDistance = distance;
    } else if (light.getType() == GMAN_LIGHT_DISTANT) {
      distantDistance = distance;
    }
    return transmission_;
  }

  mutable GMANVector pointTowardLight;
  mutable RtFloat pointDistance = -1.0f;
  mutable RtFloat distantDistance = -1.0f;

private:
  GMANColor transmission_;
};

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

  // ---- white: unblocked leaves every result exactly as the null occluder
  // does ----
  RecordingOccluder white(GMANColor(1.0f, 1.0f, 1.0f));
  env.occluder = &white;
  check(colorNear(env.ambient(), ambientBaseline, kTol), "white transmission: ambient() is unchanged");
  check(colorNear(env.diffuse(n), diffuseBaseline, kTol), "white transmission: diffuse() matches the null occluder");
  check(colorNear(env.specular(n, v, roughness), specularBaseline, kTol),
        "white transmission: specular() matches the null occluder");

  // ---- black: fully blocked zeroes diffuse and specular, not ambient ----
  RecordingOccluder black(GMANColor(0.0f, 0.0f, 0.0f));
  env.occluder = &black;
  check(colorNear(env.ambient(), ambientBaseline, kTol), "black transmission: ambient() is unchanged");
  check(colorNear(env.diffuse(n), GMANColor(0.0f, 0.0f, 0.0f), kTol), "black transmission: diffuse() is zeroed");
  check(colorNear(env.specular(n, v, roughness), GMANColor(0.0f, 0.0f, 0.0f), kTol),
        "black transmission: specular() is zeroed");

  // ---- half grey: half transmission halves both ----
  RecordingOccluder halfGrey(GMANColor(0.5f, 0.5f, 0.5f));
  env.occluder = &halfGrey;
  check(colorNear(env.ambient(), ambientBaseline, kTol), "half-grey transmission: ambient() is unchanged");
  check(colorNear(env.diffuse(n),
                  GMANColor(diffuseBaseline.getRed() * 0.5f, diffuseBaseline.getGreen() * 0.5f,
                            diffuseBaseline.getBlue() * 0.5f),
                  kTol),
        "half-grey transmission: diffuse() is halved");
  check(colorNear(env.specular(n, v, roughness),
                  GMANColor(specularBaseline.getRed() * 0.5f, specularBaseline.getGreen() * 0.5f,
                            specularBaseline.getBlue() * 0.5f),
                  kTol),
        "half-grey transmission: specular() is halved");

  // ---- per-light distance and direction, recorded while computing
  // halfGrey's diffuse() above ----
  check(halfGrey.distantDistance == RI_INFINITY, "distant light: transmission() receives RI_INFINITY");
  check(std::fabs(halfGrey.pointDistance - 5.0f) <= kTol,
        "point light: transmission() receives its own distance (5.0)");
  GMANVector expectedTowardLight(0.0f, 0.0f, 1.0f);
  check(std::fabs(halfGrey.pointTowardLight.getX() - expectedTowardLight.getX()) <= kTol &&
            std::fabs(halfGrey.pointTowardLight.getY() - expectedTowardLight.getY()) <= kTol &&
            std::fabs(halfGrey.pointTowardLight.getZ() - expectedTowardLight.getZ()) <= kTol,
        "point light: transmission() receives a unit towardLight");

  return checkSummary("GMANSurfaceEnv's occlusion hook: diffuse()/specular() consult it, ambient() does not");
}
