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
 * The shading-language spline family: the uniform cubic B-spline's values, and
 * each basis reading only its control values at and beyond the ends of [0, 1],
 * GMANSurfaceEnv::spline's dispatch on the basis name, and the Bezier and Hermite
 * segments starting at their basis's step.
 */

#include <cmath>
#include <cstddef>
#include <limits>
#include <string>
#include <vector>

#include "check.h"
#include "gmanerror.h"
#include "gmanshaderenvironment.h"
#include "gmanslapi.h"

namespace {

constexpr float kTolerance = 1e-5f;

bool near(float a, float b) { return std::fabs(a - b) <= kTolerance; }

void testBsplineConstantCurve() {
  constexpr float k = 3.5f;
  std::vector<RtFloat> constant(6, k);
  for (const float value : {0.0f, 0.25f, 0.5f, 0.75f}) {
    const float result = GMANBsplineSpline<RtFloat>(value, 6, constant.data());
    check(near(result, k), "b-spline of a constant curve is the constant at value " + std::to_string(value));
  }
}

void testBsplineSegmentStart() {
  std::vector<RtFloat> points{0.0f, 1.0f, 0.0f, 0.0f};
  check(near(GMANBsplineSpline<RtFloat>(0.0f, 4, points.data()), 2.0f / 3.0f),
        "b-spline at value 0 is (p0 + 4 p1 + p2) / 6");
}

using SplineFunction = RtFloat (*)(RtFloat, RtInt, RtFloat*);

struct Basis {
  char const* name;
  SplineFunction spline;
  std::vector<RtInt> sizes;
  RtFloat (*endPoint)(std::vector<RtFloat> const&);
};

RtFloat lastButOne(std::vector<RtFloat> const& v) { return v[v.size() - 2]; }

RtFloat lastValue(std::vector<RtFloat> const& v) { return v.back(); }

RtFloat bsplineEnd(std::vector<RtFloat> const& v) {
  const std::size_t n = v.size();
  return (v[n - 3] + 4.0f * v[n - 2] + v[n - 1]) / 6.0f;
}

std::vector<Basis> const& bases() {
  static const std::vector<Basis> table{
      {"catmull-rom", GMANCatmullSpline<RtFloat>, {4, 7}, lastButOne},
      {"b-spline", GMANBsplineSpline<RtFloat>, {4, 7}, bsplineEnd},
      {"linear", GMANLinearSpline<RtFloat>, {4, 7}, lastButOne},
      {"bezier", GMANBezierSpline<RtFloat>, {4}, lastValue},
      {"hermite", GMANHermiteSpline<RtFloat>, {4}, lastButOne},
  };
  return table;
}

std::vector<RtFloat> controlValues(RtInt nvals) {
  const std::vector<RtFloat> source{0.5f, 2.0f, -1.0f, 3.0f, 1.5f, 4.0f, 2.5f};
  return std::vector<RtFloat>(source.begin(), source.begin() + nvals);
}

void testNoReadPastTheArray() {
  for (Basis const& basis : bases()) {
    for (const RtInt nvals : basis.sizes) {
      std::vector<RtFloat> const values = controlValues(nvals);
      const RtFloat atStart = basis.spline(0.0f, nvals, controlValues(nvals).data());
      const RtFloat atEnd = basis.endPoint(values);
      for (const float value : {1.0f, 1.5f, -0.5f}) {
        const RtFloat expected = value < 0.0f ? atStart : atEnd;
        const std::string label =
            std::string(basis.name) + " nvals " + std::to_string(nvals) + " value " + std::to_string(value);

        std::vector<RtFloat> sentinel = values;
        sentinel.push_back(std::numeric_limits<RtFloat>::quiet_NaN());
        const RtFloat withSentinel = basis.spline(value, nvals, sentinel.data());
        check(std::isfinite(withSentinel) && near(withSentinel, expected),
              label + ": the result is the curve's end value and never the sentinel");

        std::vector<RtFloat> exact = values;
        check(near(basis.spline(value, nvals, exact.data()), expected),
              label + ": an exact-size array is read within its bounds");
      }
    }
  }
}

void testBsplineNames() {
  GMANSurfaceEnv env;
  std::vector<RtFloat> points{0.0f, 1.0f, 0.0f, 0.0f};
  for (char const* name : {"b-spline", "bspline"}) {
    check(near(env.spline<RtFloat>(name, 0.0f, 4, points.data()), 2.0f / 3.0f),
          std::string(name) + " selects the uniform cubic B-spline");
  }
  check(near(env.spline<RtFloat>("catmull-rom", 0.0f, 4, points.data()), 1.0f),
        "catmull-rom on the same control values answers 1, so it differs from the B-spline");
}

void testEachNameMatchesItsTemplate() {
  GMANSurfaceEnv env;
  std::vector<RtFloat> six = controlValues(6);
  std::vector<RtFloat> four = controlValues(4);
  const RtFloat catmull = GMANCatmullSpline<RtFloat>(0.5f, 6, six.data());
  const RtFloat bezier = GMANBezierSpline<RtFloat>(0.5f, 4, four.data());
  const RtFloat hermite = GMANHermiteSpline<RtFloat>(0.5f, 4, four.data());
  const RtFloat linear = GMANLinearSpline<RtFloat>(0.5f, 6, six.data());
  const std::vector<RtFloat> results{catmull, bezier, hermite, linear};
  bool distinct = true;
  for (std::size_t i = 0; i < results.size(); ++i) {
    for (std::size_t j = i + 1; j < results.size(); ++j) {
      distinct = distinct && !near(results[i], results[j]);
    }
  }
  check(distinct, "the four templates differ on the control values under test");
  check(near(env.spline<RtFloat>("catmull-rom", 0.5f, 6, six.data()), catmull), "catmull-rom runs GMANCatmullSpline");
  check(near(env.spline<RtFloat>("bezier", 0.5f, 4, four.data()), bezier), "bezier runs GMANBezierSpline");
  check(near(env.spline<RtFloat>("hermite", 0.5f, 4, four.data()), hermite), "hermite runs GMANHermiteSpline");
  check(near(env.spline<RtFloat>("linear", 0.5f, 6, six.data()), linear), "linear runs GMANLinearSpline");
}

void testUnknownBasisThrows() {
  GMANSurfaceEnv env;
  std::vector<RtFloat> points = controlValues(4);
  for (char const* name : {"nonesuch", ""}) {
    bool threw = false;
    bool codeOk = false;
    bool severityOk = false;
    bool namesBasis = false;
    try {
      env.spline<RtFloat>(name, 0.5f, 4, points.data());
    } catch (GMANError const& e) {
      threw = true;
      codeOk = e.getCode() == RIE_BADTOKEN;
      severityOk = e.getSeverity() == RIE_ERROR;
      namesBasis = std::string(e.getMessage()).find(std::string("\"") + name + "\"") != std::string::npos;
    }
    const std::string label = std::string("basis \"") + name + "\"";
    check(threw, label + " throws GMANError");
    check(codeOk, label + " throws code RIE_BADTOKEN");
    check(severityOk, label + " throws severity RIE_ERROR");
    check(namesBasis, label + " is named in the message");
  }
}

void checkSegmentWalk(char const* name, SplineFunction spline, std::vector<RtFloat> const& values,
                      std::vector<RtFloat> const& expected) {
  const std::vector<RtFloat> positions{0.0f, 0.25f, 0.5f, 0.75f, 1.0f};
  for (std::size_t i = 0; i < positions.size(); ++i) {
    std::vector<RtFloat> exact = values;
    check(near(spline(positions[i], static_cast<RtInt>(exact.size()), exact.data()), expected[i]),
          std::string(name) + " at value " + std::to_string(positions[i]) + " is " + std::to_string(expected[i]));
  }
}

void testSegmentsStartAtTheirStep() {
  checkSegmentWalk("bezier", GMANBezierSpline<RtFloat>, {0.0f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f},
                   {0.0f, 1.5f, 3.0f, 4.5f, 6.0f});
  checkSegmentWalk("hermite", GMANHermiteSpline<RtFloat>, {0.0f, 0.0f, 1.0f, 0.0f, 3.0f, 0.0f},
                   {0.0f, 0.5f, 1.0f, 2.0f, 3.0f});
}

} // namespace

int main() {
  testBsplineConstantCurve();
  testBsplineSegmentStart();
  testNoReadPastTheArray();
  testBsplineNames();
  testEachNameMatchesItsTemplate();
  testUnknownBasisThrows();
  testSegmentsStartAtTheirStep();

  return checkSummary("the shading-language spline family evaluates the uniform cubic B-spline and stays within its "
                      "control values at and beyond the ends of [0, 1], and spline() accepts the six basis names and "
                      "throws on any other, and Bezier and Hermite segments start at their step");
}
