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
 * gman::shade reads a NaN or infinite channel of a shader's Ci as 0 and
 * leaves a finite channel as it is; Oi passes through for each renderer to
 * read as coverage. gman::albedo reads a shader's albedo the same way.
 */

#include <cmath>
#include <limits>
#include <memory>
#include <string>

#include "check.h"
#include "gmancolor.h"
#include "gmanmatrix4.h"
#include "gmanshaderenvironment.h"
#include "gmanshading.h"
#include "gmansurfaceshader.h"
#include "ri.h"

namespace {

constexpr RtFloat kNaN = std::numeric_limits<RtFloat>::quiet_NaN();
constexpr RtFloat kInfinity = std::numeric_limits<RtFloat>::infinity();

class FixedAnswerShader : public GMANSurfaceShader {
public:
  FixedAnswerShader(GMANColor ci, GMANColor oi, GMANColor albedoAnswer) : ci(ci), oi(oi), albedoAnswer(albedoAnswer) {}

  GMANColor computeCi(GMANSurfaceEnv const&) const override { return ci; }
  GMANColor computeOi(GMANSurfaceEnv const&) const override { return oi; }
  GMANColor albedo(GMANSurfaceEnv const&) const override { return albedoAnswer; }

private:
  GMANColor ci;
  GMANColor oi;
  GMANColor albedoAnswer;
};

gman::Appearance appearanceOf(FixedAnswerShader const& shader) {
  gman::Appearance appearance;
  appearance.shader = std::shared_ptr<GMANSurfaceShader const>(&shader, [](GMANSurfaceShader const*) {});
  return appearance;
}

bool channelsEqual(GMANColor const& c, RtFloat r, RtFloat g, RtFloat b) {
  return c.getRed() == r && c.getGreen() == g && c.getBlue() == b;
}

std::string describe(GMANColor const& c) {
  return "(" + std::to_string(c.getRed()) + ", " + std::to_string(c.getGreen()) + ", " + std::to_string(c.getBlue()) +
         ")";
}

void testShadeReadsNonFiniteCiAsZero() {
  GMANColor const mixed(kNaN, 0.25f, kInfinity);
  GMANColor const negative(-kInfinity, kNaN, 0.5f);
  GMANColor const oi(1.0f, 1.0f, 1.0f);
  FixedAnswerShader const mixedShader(mixed, oi, oi);
  FixedAnswerShader const negativeShader(negative, oi, oi);
  gman::SurfacePoint const point;
  GMANMatrix4 const cameraToWorld;

  GMANColor const ci = gman::shade(appearanceOf(mixedShader), point, cameraToWorld).Ci;
  check(channelsEqual(ci, 0.0f, 0.25f, 0.0f), "shade: Ci (NaN, 0.25, inf) reads (0, 0.25, 0), got " + describe(ci));
  GMANColor const negativeCi = gman::shade(appearanceOf(negativeShader), point, cameraToWorld).Ci;
  check(channelsEqual(negativeCi, 0.0f, 0.0f, 0.5f),
        "shade: Ci (-inf, NaN, 0.5) reads (0, 0, 0.5), got " + describe(negativeCi));
}

void testShadeLeavesFiniteCiAndOiAlone() {
  GMANColor const ci(0.2f, 1.7f, -0.4f);
  GMANColor const oi(kNaN, 2.0f, kInfinity);
  FixedAnswerShader const shader(ci, oi, ci);
  gman::Shading const shading = gman::shade(appearanceOf(shader), gman::SurfacePoint(), GMANMatrix4());
  check(channelsEqual(shading.Ci, 0.2f, 1.7f, -0.4f), "shade: a finite Ci passes through unchanged");
  check(std::isnan(shading.Oi.getRed()) && shading.Oi.getGreen() == 2.0f && std::isinf(shading.Oi.getBlue()),
        "shade: Oi passes through unchanged for the renderer to read as coverage");
}

void testAlbedoReadsNonFiniteAsZero() {
  GMANColor const oi(1.0f, 1.0f, 1.0f);
  GMANColor const albedoAnswer(0.5f, kNaN, -kInfinity);
  FixedAnswerShader const shader(oi, oi, albedoAnswer);
  GMANColor const answer = gman::albedo(appearanceOf(shader), gman::SurfacePoint(), GMANMatrix4());
  check(channelsEqual(answer, 0.5f, 0.0f, 0.0f), "albedo: (0.5, NaN, -inf) reads (0.5, 0, 0), got " + describe(answer));
}

} // namespace

int main() {
  testShadeReadsNonFiniteCiAsZero();
  testShadeLeavesFiniteCiAndOiAlone();
  testAlbedoReadsNonFiniteAsZero();
  return checkSummary("shadingnonfinite: all checks passed");
}
