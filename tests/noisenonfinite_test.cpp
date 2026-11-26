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
 * gman::Noise answers NaN for a non-finite coordinate and for a periodic
 * noise's zero, infinite or NaN period, in every dimension of noise,
 * periodic and cellnoise. A finite coordinate beyond RtInt's range answers
 * the lattice's own periodic value instead of converting out of range,
 * which a debug build's UBSan traps.
 */

#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <string>
#include <vector>

#include "check.h"
#include "gmannoise.h"
#include "gmanpoint.h"

namespace {

using Coordinates = std::array<RtFloat, 4>;
using Periods = std::array<RtFloat, 4>;

constexpr RtFloat kNaN = std::numeric_limits<RtFloat>::quiet_NaN();
constexpr RtFloat kInfinity = std::numeric_limits<RtFloat>::infinity();
constexpr RtFloat kBeyondRange = 3.5e9f;
constexpr RtFloat kSamePeriodInRange = -794967296.0f;
constexpr RtFloat kSamePeriodForCells = 768.0f;
constexpr RtFloat kWidePeriod = 1e30f;

struct Form {
  std::string name;
  int dimensions;
  std::function<RtFloat(gman::Noise&, Coordinates const&, Periods const&)> scalar;
  std::function<void(gman::Noise&, Coordinates const&, Periods const&, RtFloat&, RtFloat&, RtFloat&)> vector;
};

GMANPoint pointOf(Coordinates const& c) { return GMANPoint(c[0], c[1], c[2]); }

std::vector<Form> noiseForms() {
  return {
      {"noise 1D", 1, [](gman::Noise& n, Coordinates const& c, Periods const&) { return n.noise(c[0]); },
       [](gman::Noise& n, Coordinates const& c, Periods const&, RtFloat& a, RtFloat& b, RtFloat& d) {
         n.noise(c[0], a, b, d);
       }},
      {"noise 2D", 2, [](gman::Noise& n, Coordinates const& c, Periods const&) { return n.noise(c[0], c[1]); },
       [](gman::Noise& n, Coordinates const& c, Periods const&, RtFloat& a, RtFloat& b, RtFloat& d) {
         n.noise(c[0], c[1], a, b, d);
       }},
      {"noise 3D", 3, [](gman::Noise& n, Coordinates const& c, Periods const&) { return n.noise(pointOf(c)); },
       [](gman::Noise& n, Coordinates const& c, Periods const&, RtFloat& a, RtFloat& b, RtFloat& d) {
         n.noise(pointOf(c), a, b, d);
       }},
      {"noise 4D", 4, [](gman::Noise& n, Coordinates const& c, Periods const&) { return n.noise(pointOf(c), c[3]); },
       [](gman::Noise& n, Coordinates const& c, Periods const&, RtFloat& a, RtFloat& b, RtFloat& d) {
         n.noise(pointOf(c), c[3], a, b, d);
       }},
      {"cellnoise 1D", 1, [](gman::Noise& n, Coordinates const& c, Periods const&) { return n.cellnoise(c[0]); },
       [](gman::Noise& n, Coordinates const& c, Periods const&, RtFloat& a, RtFloat& b, RtFloat& d) {
         n.cellnoise(c[0], a, b, d);
       }},
      {"cellnoise 2D", 2, [](gman::Noise& n, Coordinates const& c, Periods const&) { return n.cellnoise(c[0], c[1]); },
       [](gman::Noise& n, Coordinates const& c, Periods const&, RtFloat& a, RtFloat& b, RtFloat& d) {
         n.cellnoise(c[0], c[1], a, b, d);
       }},
      {"cellnoise 3D", 3, [](gman::Noise& n, Coordinates const& c, Periods const&) { return n.cellnoise(pointOf(c)); },
       [](gman::Noise& n, Coordinates const& c, Periods const&, RtFloat& a, RtFloat& b, RtFloat& d) {
         n.cellnoise(pointOf(c), a, b, d);
       }},
      {"cellnoise 4D", 4,
       [](gman::Noise& n, Coordinates const& c, Periods const&) { return n.cellnoise(pointOf(c), c[3]); },
       [](gman::Noise& n, Coordinates const& c, Periods const&, RtFloat& a, RtFloat& b, RtFloat& d) {
         n.cellnoise(pointOf(c), c[3], a, b, d);
       }},
  };
}

std::vector<Form> periodicForms() {
  return {
      {"periodic 1D", 1, [](gman::Noise& n, Coordinates const& c, Periods const& p) { return n.periodic(c[0], p[0]); },
       [](gman::Noise& n, Coordinates const& c, Periods const& p, RtFloat& a, RtFloat& b, RtFloat& d) {
         n.periodic(c[0], p[0], a, b, d);
       }},
      {"periodic 2D", 2,
       [](gman::Noise& n, Coordinates const& c, Periods const& p) { return n.periodic(c[0], c[1], p[0], p[1]); },
       [](gman::Noise& n, Coordinates const& c, Periods const& p, RtFloat& a, RtFloat& b, RtFloat& d) {
         n.periodic(c[0], c[1], p[0], p[1], a, b, d);
       }},
      {"periodic 3D", 3,
       [](gman::Noise& n, Coordinates const& c, Periods const& p) { return n.periodic(pointOf(c), pointOf(p)); },
       [](gman::Noise& n, Coordinates const& c, Periods const& p, RtFloat& a, RtFloat& b, RtFloat& d) {
         n.periodic(pointOf(c), pointOf(p), a, b, d);
       }},
      {"periodic 4D", 4,
       [](gman::Noise& n, Coordinates const& c, Periods const& p) {
         return n.periodic(pointOf(c), c[3], pointOf(p), p[3]);
       },
       [](gman::Noise& n, Coordinates const& c, Periods const& p, RtFloat& a, RtFloat& b, RtFloat& d) {
         n.periodic(pointOf(c), c[3], pointOf(p), p[3], a, b, d);
       }},
  };
}

std::vector<Form> allForms() {
  std::vector<Form> forms = noiseForms();
  for (Form const& form : periodicForms()) {
    forms.push_back(form);
  }
  return forms;
}

constexpr Coordinates kOrdinary = {0.25f, 0.5f, 0.75f, 0.125f};
constexpr Periods kOrdinaryPeriods = {4.0f, 4.0f, 4.0f, 4.0f};

void testNonFiniteCoordinate() {
  gman::Noise noise;
  for (Form const& form : allForms()) {
    check(std::isfinite(form.scalar(noise, kOrdinary, kOrdinaryPeriods)),
          form.name + ": an ordinary coordinate is finite");
    for (int position = 0; position < form.dimensions; ++position) {
      for (RtFloat const bad : {kNaN, kInfinity, -kInfinity}) {
        Coordinates c = kOrdinary;
        c[static_cast<std::size_t>(position)] = bad;
        check(std::isnan(form.scalar(noise, c, kOrdinaryPeriods)), form.name + ": non-finite coordinate " +
                                                                       std::to_string(position) + " " +
                                                                       std::to_string(bad) + " answers NaN");
        RtFloat a = 0, b = 0, d = 0;
        form.vector(noise, c, kOrdinaryPeriods, a, b, d);
        check(std::isnan(a) && std::isnan(b) && std::isnan(d),
              form.name + ": vector form of coordinate " + std::to_string(position) + " " + std::to_string(bad) +
                  " answers NaN in every component");
      }
    }
  }
}

void testUnusablePeriod() {
  gman::Noise noise;
  for (Form const& form : periodicForms()) {
    for (int position = 0; position < form.dimensions; ++position) {
      for (RtFloat const bad : {0.0f, kNaN, kInfinity, -kInfinity}) {
        Periods p = kOrdinaryPeriods;
        p[static_cast<std::size_t>(position)] = bad;
        check(std::isnan(form.scalar(noise, kOrdinary, p)),
              form.name + ": period " + std::to_string(position) + " of " + std::to_string(bad) + " answers NaN");
      }
    }
  }
}

void testCoordinateBeyondRange() {
  gman::Noise noise;
  for (Form const& form : noiseForms()) {
    for (RtFloat const sign : {1.0f, -1.0f}) {
      Coordinates beyond;
      beyond.fill(sign * kBeyondRange);
      RtFloat const beyondAnswer = form.scalar(noise, beyond, kOrdinaryPeriods);
      check(std::isfinite(beyondAnswer), form.name + ": a coordinate of " + std::to_string(beyond[0]) + " is finite");
      Coordinates origin;
      origin.fill(form.name.starts_with("cellnoise") ? sign * kSamePeriodForCells : 0.0f);
      check(beyondAnswer == form.scalar(noise, origin, kOrdinaryPeriods),
            form.name + ": a coordinate of " + std::to_string(beyond[0]) + " answers its lattice period's value");
    }
    Coordinates inRange;
    inRange.fill(kSamePeriodInRange);
    Coordinates beyond;
    beyond.fill(kBeyondRange);
    check(form.scalar(noise, beyond, kOrdinaryPeriods) == form.scalar(noise, inRange, kOrdinaryPeriods),
          form.name + ": a coordinate beyond RtInt matches the in-range coordinate one 2^32 below");
    Coordinates widest;
    widest.fill(std::numeric_limits<RtFloat>::max());
    check(std::isfinite(form.scalar(noise, widest, kOrdinaryPeriods)),
          form.name + ": the largest finite coordinate is finite");
  }
}

void testPeriodicBeyondRange() {
  gman::Noise noise;
  Periods wide;
  wide.fill(kWidePeriod);
  for (Form const& form : periodicForms()) {
    Coordinates beyond;
    beyond.fill(kBeyondRange);
    Coordinates origin;
    origin.fill(0.0f);
    RtFloat const beyondAnswer = form.scalar(noise, beyond, wide);
    check(std::isfinite(beyondAnswer), form.name + ": a coordinate beyond RtInt under a wide period is finite");
    check(beyondAnswer == form.scalar(noise, origin, wide),
          form.name + ": a coordinate beyond RtInt under a wide period answers its lattice period's value");
    Coordinates widest;
    widest.fill(std::numeric_limits<RtFloat>::max());
    check(std::isfinite(form.scalar(noise, widest, wide)),
          form.name + ": the largest finite coordinate under a wide period is finite");
  }
}

} // namespace

int main() {
  testNonFiniteCoordinate();
  testUnusablePeriod();
  testCoordinateBeyondRange();
  testPeriodicBeyondRange();
  return checkSummary("noisenonfinite: all checks passed");
}
