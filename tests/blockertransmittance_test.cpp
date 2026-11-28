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
 * gman::blockerTransmittance and gman::clampCoverage, against a real
 * primitive hit at normal incidence through a real intersect. A shader's
 * opacity sets the coverage and its dielectric lobe the transmittance, so
 * the factor is (1 - c) + c * w * (1 - F) per channel; a null-shader
 * appearance gives 1 - Os; a NaN opacity reads as opaque and a NaN
 * transmittance as blocked.
 */

#include <cmath>
#include <limits>
#include <memory>
#include <utility>

#include "check.h"
#include "gmanbsdf.h"
#include "gmancolor.h"
#include "gmanmatrix4.h"
#include "gmanowned.h"
#include "gmanparameterlist.h"
#include "gmanpoint.h"
#include "gmanray.h"
#include "gmanraybvh.h"
#include "gmanrayoccluder.h"
#include "gmanrayprimitive.h"
#include "gmanraysphere.h"
#include "gmanshading.h"
#include "gmansurfaceshader.h"
#include "gmanvector.h"
#include "gmanworld.h"
#include "maketransform.h"
#include "ri.h"

namespace {

constexpr RtFloat kIndex = (RtFloat)1.5;
constexpr RtFloat kRelativeTolerance = (RtFloat)1e-5;
constexpr RtFloat kNaN = std::numeric_limits<RtFloat>::quiet_NaN();
constexpr RtFloat kInfinity = std::numeric_limits<RtFloat>::infinity();

RtFloat normalIncidenceReflectance(RtFloat eta) {
  RtFloat const root = (eta - (RtFloat)1.0) / (eta + (RtFloat)1.0);
  return root * root;
}

bool near(RtFloat a, RtFloat b) { return std::fabs(a - b) <= kRelativeTolerance * std::fabs(b); }

bool colorNear(GMANColor const& a, GMANColor const& b) {
  return near(a.getRed(), b.getRed()) && near(a.getGreen(), b.getGreen()) && near(a.getBlue(), b.getBlue());
}

bool colorExactly(GMANColor const& a, GMANColor const& b) {
  return a.getRed() == b.getRed() && a.getGreen() == b.getGreen() && a.getBlue() == b.getBlue();
}

class StubShader : public GMANSurfaceShader {
public:
  StubShader(GMANColor const& oi, GMANColor const& weight, bool nanNormal)
      : oi(oi), weight(weight), nanNormal(nanNormal) {}

  GMANColor computeCi(GMANSurfaceEnv const&) const override { return GMANColor(0.0f, 0.0f, 0.0f); }
  GMANColor computeOi(GMANSurfaceEnv const&) const override { return oi; }
  gman::BSDF bsdf(GMANSurfaceEnv const& se) const override {
    GMANVector normal(se.N.getX(), se.N.getY(), se.N.getZ());
    if (nanNormal) {
      normal = GMANVector(kNaN, kNaN, kNaN);
    }
    gman::BSDF closure(normal);
    closure.addDielectric(weight, kIndex);
    return closure;
  }

private:
  GMANColor oi;
  GMANColor weight;
  bool nanNormal;
};

std::shared_ptr<GMANSurfaceShader const> asAppearanceShader(GMANSurfaceShader const& shader) {
  return std::shared_ptr<GMANSurfaceShader const>(&shader, [](GMANSurfaceShader const*) {});
}

struct Blocker {
  GMANWorld world;
  GMANRayBVH bvh;
  GMANRay const ray{GMANPoint(-5.0f, 0.0f, 0.0f), GMANVector(1.0f, 0.0f, 0.0f)};
  GMANHit hit;
  GMANRayPrimitive const* primitive = nullptr;
  bool struck = false;

  explicit Blocker(gman::Appearance const& appearance) {
    GMANMatrix4 identity;
    auto sphere = gman::make<GMANRaySphere>(1.0f, -1.0f, 1.0f, 360.0f, GMANParameterList(), makeTransform(identity));
    sphere->setAppearance(appearance);
    world.add(std::move(sphere));
    bvh.build(world);
    struck = bvh.nearestHit(ray, hit, primitive);
  }

  gman::BlockerTransmittance transmittance() const {
    return gman::blockerTransmittance(ray, hit, primitive->getAppearance(), GMANMatrix4(), nullptr);
  }
};

gman::Appearance shadedAppearance(GMANSurfaceShader const& shader) {
  gman::Appearance appearance;
  appearance.Os = GMANColor(1.0f, 1.0f, 1.0f);
  appearance.shader = asAppearanceShader(shader);
  return appearance;
}

void testShaderSetsCoverageAndTransmittance() {
  GMANColor const oi(1.0f, 0.5f, 0.0f);
  GMANColor const weight(1.0f, 0.5f, 0.25f);
  StubShader const shader(oi, weight, false);
  Blocker const blocker(shadedAppearance(shader));
  check(blocker.struck, "a: the ray hits the blocker");
  if (!blocker.struck) {
    return;
  }

  gman::BlockerTransmittance const result = blocker.transmittance();
  check(colorExactly(result.coverage, oi), "a: coverage is the shader's Oi, not Appearance::Os");

  RtFloat const pass = (RtFloat)1.0 - normalIncidenceReflectance(kIndex);
  GMANColor const expected((1.0f - oi.getRed()) + oi.getRed() * weight.getRed() * pass,
                           (1.0f - oi.getGreen()) + oi.getGreen() * weight.getGreen() * pass,
                           (1.0f - oi.getBlue()) + oi.getBlue() * weight.getBlue() * pass);
  check(colorNear(result.transmittance, expected), "a: the factor is (1 - c) + c * w * (1 - F) per channel");
  check(result.transmittance.getRed() != result.transmittance.getGreen() &&
            result.transmittance.getGreen() != result.transmittance.getBlue() &&
            result.transmittance.getRed() != result.transmittance.getBlue(),
        "a: every channel of the factor is distinct");
}

void testNullShaderFactorIsOneMinusOs() {
  gman::Appearance appearance;
  appearance.Os = GMANColor(0.25f, 0.5f, 0.75f);
  Blocker const blocker(appearance);
  check(blocker.struck, "b: the ray hits the blocker");
  if (!blocker.struck) {
    return;
  }

  gman::BlockerTransmittance const result = blocker.transmittance();
  check(colorExactly(result.coverage, appearance.Os), "b: a null shader's coverage is Appearance::Os");
  check(colorExactly(result.transmittance, gman::oneMinus(appearance.Os)), "b: a null shader's factor is 1 - Os");
}

void testNaNAndOutOfRangeOpacityClamp() {
  StubShader const shader(GMANColor(kNaN, 1.5f, -0.5f), GMANColor(1.0f, 1.0f, 1.0f), false);
  Blocker const blocker(shadedAppearance(shader));
  check(blocker.struck, "c: the ray hits the blocker");
  if (!blocker.struck) {
    return;
  }

  check(colorExactly(blocker.transmittance().coverage, GMANColor(1.0f, 1.0f, 0.0f)),
        "c: an Oi of (NaN, 1.5, -0.5) gives coverage (1, 1, 0)");
}

void testClampCoverageMapsInfinities() {
  GMANColor const clamped = gman::clampCoverage(GMANColor(kInfinity, -kInfinity, 0.5f));
  check(colorExactly(clamped, GMANColor(1.0f, 0.0f, 0.5f)), "d: clampCoverage maps +inf to 1, -inf to 0, 0.5 to 0.5");
}

void testNonFiniteTransmittanceBlocks() {
  GMANColor const weight(1.0f, 1.0f, 1.0f);
  gman::BSDF setup(GMANVector(kNaN, kNaN, kNaN));
  setup.addDielectric(weight, kIndex);
  GMANColor const unguarded = setup.shadowTransmittance(GMANVector(1.0f, 0.0f, 0.0f));
  check(!std::isfinite(unguarded.getRed()) && !std::isfinite(unguarded.getGreen()) &&
            !std::isfinite(unguarded.getBlue()),
        "e: setup: a closure built on a NaN normal has a non-finite shadowTransmittance");

  StubShader const shader(GMANColor(1.0f, 1.0f, 1.0f), weight, true);
  Blocker const blocker(shadedAppearance(shader));
  check(blocker.struck, "e: the ray hits the blocker");
  if (!blocker.struck) {
    return;
  }

  check(colorExactly(blocker.transmittance().transmittance, GMANColor(0.0f, 0.0f, 0.0f)),
        "e: a non-finite shadow transmittance gives a factor of 0 in every channel");
}

} // namespace

int main() {
  testShaderSetsCoverageAndTransmittance();
  testNullShaderFactorIsOneMinusOs();
  testNaNAndOutOfRangeOpacityClamp();
  testClampCoverageMapsInfinities();
  testNonFiniteTransmittanceBlocks();

  return checkSummary("gman::blockerTransmittance: shader coverage and dielectric transmittance per blocker");
}
