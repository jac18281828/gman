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
 * GMANSurfaceEnv::ambient() adds indirect when it is set, and only when
 * it is set: directly against a bare env, and through gman::shade against
 * a matte appearance, where Ka alone gates the term.
 */

#include <cmath>

#include "check.h"
#include "gmanattributes.h"
#include "gmandictionary.h"
#include "gmanlightsourcemgr.h"
#include "gmanparameterlist.h"
#include "gmanshaderenvironment.h"
#include "gmanshading.h"
#include "ri.h"

namespace {

constexpr RtFloat kTol = (RtFloat)1.0e-6;

bool colorNear(GMANColor const& a, GMANColor const& b, RtFloat tol) {
  return std::fabs(a.getRed() - b.getRed()) <= tol && std::fabs(a.getGreen() - b.getGreen()) <= tol &&
         std::fabs(a.getBlue() - b.getBlue()) <= tol;
}

GMANParameterList kaKdParams(RtFloat ka, RtFloat kd) {
  static GMANDictionary dictionary;
  RtToken tokens[2] = {RI_KA, RI_KD};
  RtPointer parms[2] = {&ka, &kd};
  return GMANParameterList(dictionary, 2, tokens, parms);
}

// A bare env: no lights, then one ambient light added, each read with and
// without indirect bound.
void checkBareEnvAddsIndirect() {
  GMANColor const c(0.1f, 0.2f, 0.3f);

  GMANSurfaceEnv env;
  env.indirect = &c;
  check(colorNear(env.ambient(), c, kTol), "no lights, indirect bound: ambient() returns indirect exactly");

  RtFloat const a = 0.4f;
  GMANLight const ambientLight(GMAN_LIGHT_AMBIENT, GMANColor(a, a, a), GMANPoint(), GMANVector());
  env.lights = {&ambientLight};
  check(colorNear(env.ambient(), GMANColor(a + c.getRed(), a + c.getGreen(), a + c.getBlue()), kTol),
        "one ambient light, indirect bound: ambient() returns the light plus indirect");

  env.indirect = nullptr;
  check(colorNear(env.ambient(), GMANColor(a, a, a), kTol),
        "one ambient light, indirect null: ambient() returns the light's own contribution alone");
}

// A matte appearance loaded through GMANAttributes::setSurface, the
// factory path RiSurfaceV takes -- Ka alone gates whether indirect
// reaches Ci, since diffuse() never reads it.
void checkThroughShadeGatedByKa() {
  RtFloat const ka = 0.5f;
  RtFloat const kd = 1.0f;
  GMANColor const c(0.1f, 0.2f, 0.3f);

  GMANAttributes attributes;
  RtColor white = {1.0f, 1.0f, 1.0f};
  attributes.setColor(white);
  attributes.setOpacity(white);
  attributes.setSurface("matte", kaKdParams(ka, kd));

  gman::Appearance appearance = gman::appearanceOf(attributes);
  GMANLight const ambientLight(GMAN_LIGHT_AMBIENT, GMANColor(0.3f, 0.3f, 0.3f), GMANPoint(), GMANVector());
  appearance.lights = {&ambientLight};

  gman::SurfacePoint point;
  point.P = GMANPoint(0.0f, 0.0f, 0.0f);
  point.N = GMANNormal(0.0f, 0.0f, 1.0f);
  point.Ng = GMANNormal(0.0f, 0.0f, 1.0f);
  point.I = GMANVector(0.0f, 0.0f, -1.0f);
  point.E = GMANPoint(0.0f, 0.0f, 5.0f);
  GMANMatrix4 const cameraToWorld;

  gman::Shading const withIndirect = gman::shade(appearance, point, cameraToWorld, nullptr, nullptr, nullptr, &c);
  gman::Shading const withoutIndirect = gman::shade(appearance, point, cameraToWorld);

  GMANColor const delta(withIndirect.Ci.getRed() - withoutIndirect.Ci.getRed(),
                        withIndirect.Ci.getGreen() - withoutIndirect.Ci.getGreen(),
                        withIndirect.Ci.getBlue() - withoutIndirect.Ci.getBlue());
  GMANColor const want(ka * c.getRed(), ka * c.getGreen(), ka * c.getBlue());
  check(colorNear(delta, want, kTol),
        "matte through gman::shade: Ci's own indirect delta is Ka * indirect, entering through ambient() alone");
}

} // namespace

int main() {
  checkBareEnvAddsIndirect();
  checkThroughShadeGatedByKa();

  return checkSummary("GMANSurfaceEnv::ambient() adds indirect when bound, gated by Ka through gman::shade");
}
