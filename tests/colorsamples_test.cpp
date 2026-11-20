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
#include <cstddef>
#include <string>
#include <vector>

#include "check.h"
#include "gmanattributes.h"
#include "gmancolor.h"
#include "gmanerror.h"
#include "gmanoptions.h"
#include "gmanrendermanimpl.h"
#include "ri.h"

namespace {

class ColorSamplesRenderMan : public GMANRenderManImpl {
public:
  GMANOptions& options() { return getOptions(); }
  GMANAttributes& attributes() { return getAttributes(); }
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

void setColorSamples(ColorSamplesRenderMan& renderMan, RtInt count) {
  std::vector<RtFloat> matrix(3 * static_cast<std::size_t>(count), 0.0f);
  renderMan.RiColorSamples(count, matrix.data(), matrix.data());
}

bool colorIs(GMANColor const& color, RtColor const& expected) {
  return color.getRed() == expected[0] && color.getGreen() == expected[1] && color.getBlue() == expected[2];
}

void checkColorAndOpacityRefused(ColorSamplesRenderMan& renderMan, RtInt count) {
  const std::string label = "ColorSamples " + std::to_string(count) + ": ";
  RtColor const supplied = {0.9f, 0.8f, 0.7f};
  RtColor const priorColor = {0.25f, 0.5f, 0.75f};
  RtColor const priorOpacity = {0.125f, 0.375f, 0.625f};

  setColorSamples(renderMan, DefaultColorSamples);
  renderMan.RiColor(const_cast<RtFloat*>(priorColor));
  renderMan.RiOpacity(const_cast<RtFloat*>(priorOpacity));
  setColorSamples(renderMan, count);

  const Thrown color = thrownBy([&] { renderMan.RiColor(const_cast<RtFloat*>(supplied)); });
  check(color.threw && color.code == RIE_UNIMPLEMENT && color.severity == RIE_ERROR,
        label + "Color throws RIE_UNIMPLEMENT at RIE_ERROR");
  check(colorIs(renderMan.attributes().getColor(), priorColor), label + "a refused Color keeps the stored color");

  const Thrown opacity = thrownBy([&] { renderMan.RiOpacity(const_cast<RtFloat*>(supplied)); });
  check(opacity.threw && opacity.code == RIE_UNIMPLEMENT && opacity.severity == RIE_ERROR,
        label + "Opacity throws RIE_UNIMPLEMENT at RIE_ERROR");
  check(colorIs(renderMan.attributes().getOpacity(), priorOpacity),
        label + "a refused Opacity keeps the stored opacity");
}

void testColorAndOpacityNeedThreeSamples() {
  ColorSamplesRenderMan renderMan;
  renderMan.RiBegin("gmanzbuffer");

  RtColor const color = {0.9f, 0.8f, 0.7f};
  const Thrown storedColor = thrownBy([&] { renderMan.RiColor(const_cast<RtFloat*>(color)); });
  const Thrown storedOpacity = thrownBy([&] { renderMan.RiOpacity(const_cast<RtFloat*>(color)); });
  check(!storedColor.threw && !storedOpacity.threw, "default count: Color and Opacity are accepted");
  check(colorIs(renderMan.attributes().getColor(), color), "default count: Color stores its values");
  check(colorIs(renderMan.attributes().getOpacity(), color), "default count: Opacity stores its values");

  checkColorAndOpacityRefused(renderMan, 2);
  checkColorAndOpacityRefused(renderMan, 5);

  renderMan.RiEnd();
}

} // namespace

int main() {
  testCountBelowOneIsRejected();
  testColorAndOpacityNeedThreeSamples();

  return checkSummary("ColorSamples is honored by the parser and refused by the renderer where it cannot shade");
}
