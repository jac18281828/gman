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

#include <array>
#include <string>

#include "check.h"
#include "gmanerror.h"
#include "gmanoptions.h"
#include "gmanrendermanimpl.h"
#include "ri.h"

namespace {

class ColorSamplesRenderMan : public GMANRenderManImpl {
public:
  GMANOptions& options() { return getOptions(); }
};

struct Thrown {
  bool threw = false;
  RtInt code = 0;
  RtInt severity = 0;
};

template <typename Call> Thrown thrownBy(Call&& call) {
  try {
    call();
  } catch (GMANError const& error) {
    return {true, error.getCode(), error.getSeverity()};
  }
  return {};
}

void checkRejectedBelowOne(ColorSamplesRenderMan& renderMan, RtInt count) {
  const Thrown thrown = thrownBy([&] { renderMan.RiColorSamples(count, nullptr, nullptr); });
  const std::string label = "ColorSamples " + std::to_string(count);
  check(thrown.threw, label + " throws");
  check(thrown.code == RIE_RANGE, label + " throws RIE_RANGE");
  check(thrown.severity == RIE_ERROR, label + " throws at RIE_ERROR");
  check(renderMan.options().getColorSamples().getNumber() == DefaultColorSamples,
        label + " leaves the stored count as it was");
}

void testCountBelowOneIsRejected() {
  ColorSamplesRenderMan renderMan;
  renderMan.RiBegin("gmanzbuffer");

  checkRejectedBelowOne(renderMan, 0);
  checkRejectedBelowOne(renderMan, -1);

  std::array<RtFloat, 6> nRGB{1, 0, 0, 0, 1, 0};
  std::array<RtFloat, 6> RGBn{1, 0, 0, 0, 1, 0};
  const Thrown accepted = thrownBy([&] { renderMan.RiColorSamples(2, nRGB.data(), RGBn.data()); });
  check(!accepted.threw, "ColorSamples 2 is accepted");
  check(renderMan.options().getColorSamples().getNumber() == 2, "ColorSamples 2 stores its count");

  renderMan.RiEnd();
}

} // namespace

int main() {
  testCountBelowOneIsRejected();

  return checkSummary("ColorSamples is honored by the parser and refused by the renderer where it cannot shade");
}
