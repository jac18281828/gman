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
 * Regression against the delta-light integrator this unit's emitter wiring
 * replaced, one light at a time: a point, a spot and a distant light each
 * still reproduce their own previously pinned residual check, unmoved by
 * choosing and sampling every light -- delta or area -- through the shared
 * gman::Emitter interface. Split from pathtracerarealightregression_test.cpp,
 * whose furnace, two-light and environment checks the three renders here
 * would otherwise crowd past a sanitizer build's own per-test budget.
 */

#include <string>
#include <vector>

#include "check.h"
#include "gmanlightsourcemgr.h"
#include "gmanpoint.h"
#include "gmanvector.h"
#include "pathtracerscene.h"
#include "ri.h"

namespace {

constexpr std::size_t kMinMeasuredPixels = 400;

void testOneLight() {
  struct Case {
    char const* name;
    GMANLight light;
  };
  Case cases[] = {
      {"point", GMANLight(GMAN_LIGHT_POINT, GMANColor(4.0f, 4.0f, 4.0f), GMANPoint(0.0f, -2.0f, 8.0f), GMANVector())},
      {"spot", GMANLight(GMAN_LIGHT_SPOT, GMANColor(4.0f, 4.0f, 4.0f), GMANPoint(0.0f, -2.0f, 8.0f),
                         GMANVector(0.0f, -1.0f, 0.0f), 0.6f, 0.15f, 2.0f)},
      {"distant",
       GMANLight(GMAN_LIGHT_DISTANT, GMANColor(0.8f, 0.8f, 0.8f), GMANPoint(), GMANVector(0.0f, -1.0f, 0.0f))},
  };

  for (auto const& c : cases) {
    std::vector<GMANLight const*> const lights = {&c.light};
    std::vector<double> residuals[3];
    std::vector<double> expected[3];
    std::size_t measured = 0;
    std::size_t dropped = 0;
    renderAndCollectResiduals(lights, residuals, expected, measured, dropped);
    check(dropped == 0, std::string("one light, ") + c.name + ": droppedPathCount() is 0");
    check(measured >= kMinMeasuredPixels, std::string("one light, ") + c.name + ": at least 400 pixels measured");
    checkResiduals(residuals, expected, 1e-4, std::string("one light, ") + c.name);
  }
}

} // namespace

int main() {
  testOneLight();

  return checkSummary("the delta-light integrator's own pinned residual check holds unmoved for a point, a spot "
                      "and a distant light taken one at a time");
}
