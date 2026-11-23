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
#include <cstdio>
#include <string>
#include <vector>

#include "check.h"
#include "gmanascii.h"
#include "gmanattributes.h"
#include "gmancolor.h"
#include "gmandefaults.h"
#include "gmanerror.h"
#include "gmanoptions.h"
#include "gmanrenderman.h"
#include "gmanrendermanimpl.h"
#include "gmanribparse.h"
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
  std::string message;
};

template <typename Call> Thrown thrownBy(Call&& call) {
  try {
    call();
  } catch (GMANError const& error) {
    return {true, error.getCode(), error.getSeverity(), error.getMessage()};
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

void setColorSamples(GMANRenderManImpl& renderMan, RtInt count) {
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

struct ColorSamplesCall {
  RtInt n = 0;
  std::vector<RtFloat> nRGB;
  std::vector<RtFloat> RGBn;
};

ColorSamplesCall colorSamplesCall(RtInt n, RtFloat const* nRGB, RtFloat const* RGBn) {
  const auto length = 3 * static_cast<std::size_t>(n);
  return {n, {nRGB, nRGB + length}, {RGBn, RGBn + length}};
}

class RecordingRenderMan : public GMANRenderManImpl {
public:
  std::vector<ColorSamplesCall> colorSamples;
  std::vector<std::vector<RtFloat>> colors;
  std::vector<std::vector<RtFloat>> opacities;

  RtVoid RiColorSamples(RtInt n, RtFloat nRGB[], RtFloat RGBn[]) override {
    colorSamples.push_back(colorSamplesCall(n, nRGB, RGBn));
    GMANRenderManImpl::RiColorSamples(n, nRGB, RGBn);
  }

  RtVoid RiColor(RtColor color) override { colors.emplace_back(color, color + colorSampleCount()); }
  RtVoid RiOpacity(RtColor opacity) override { opacities.emplace_back(opacity, opacity + colorSampleCount()); }
};

class FallbackRenderMan : public GMANASCII {
public:
  std::vector<RtFloat> color;
  std::vector<RtFloat> opacity;
  int spheres = 0;

  RtVoid RiBegin(RtToken) override {}
  RtVoid RiEnd() override {}
  RtVoid RiColor(RtColor value) override { color.assign(value, value + DefaultColorSamples); }
  RtVoid RiOpacity(RtColor value) override { opacity.assign(value, value + DefaultColorSamples); }
  RtVoid RiSphereV(RtFloat, RtFloat, RtFloat, RtFloat, RtInt, RtToken[], RtPointer[]) override { ++spheres; }
};

std::vector<RtFloat> run(RtInt count, int firstStep) {
  std::vector<RtFloat> values;
  for (RtInt i = 0; i < count; ++i) {
    values.push_back(0.125f * static_cast<RtFloat>(firstStep + i + 1));
  }
  return values;
}

bool parses(GMANRIBParse& parser) {
  try {
    parser.parse();
  } catch (GMANError const&) {
    return false;
  }
  return true;
}

void testParserReadsAtColorSampleCount(std::string const& ribDir, RtInt count) {
  const std::string label = "ColorSamples " + std::to_string(count) + ": ";
  const std::string rib = ribDir + "/colorsamples_" + std::to_string(count) + ".rib";
  RecordingRenderMan renderMan;
  bool parsed = false;
  {
    GMANRIBParse parser(renderMan, rib.c_str());
    setColorSamples(renderMan, count);
    parsed = parses(parser);
  }

  check(parsed, label + "the fixture parses to its end");
  check(renderMan.colors.size() == 2, label + "two Color requests arrive");
  check(renderMan.opacities.size() == 2, label + "two Opacity requests arrive");
  if (renderMan.colors.size() != 2 || renderMan.opacities.size() != 2) {
    return;
  }
  check(renderMan.colors[0] == run(count, 0), label + "bare Color reads its samples in order");
  check(renderMan.colors[1] == run(count, count), label + "bracketed Color reads its samples in order");
  check(renderMan.opacities[0] == run(count, 2 * count), label + "bare Opacity reads its samples in order");
  check(renderMan.opacities[1] == run(count, 3 * count), label + "bracketed Opacity reads its samples in order");
}

void testRendererWithoutColorSamplesReadsThree(std::string const& ribDir) {
  const std::string rib = ribDir + "/colorsamples_fallback.rib";
  FallbackRenderMan renderMan;
  bool parsed = false;
  {
    GMANRIBParse parser(renderMan, rib.c_str());
    parsed = parses(parser);
  }

  check(parsed, "fallback: the fixture parses to its end");
  check(renderMan.color == std::vector<RtFloat>({0.25f, 0.5f, 0.75f}), "fallback: Color reads three samples");
  check(renderMan.opacity == std::vector<RtFloat>({1.0f, 1.0f, 1.0f}), "fallback: Opacity reads three samples");
  check(renderMan.spheres == 2, "fallback: each request is followed by its Sphere");
}

class WritebackReader : public GMANASCII {
public:
  std::vector<ColorSamplesCall> colorSamples;
  int spheres = 0;

  RtVoid RiBegin(RtToken) override {}
  RtVoid RiEnd() override {}
  RtVoid RiColorSamples(RtInt n, RtFloat nRGB[], RtFloat RGBn[]) override {
    colorSamples.push_back(colorSamplesCall(n, nRGB, RGBn));
  }
  RtVoid RiSphereV(RtFloat, RtFloat, RtFloat, RtFloat, RtInt, RtToken[], RtPointer[]) override { ++spheres; }
};

Thrown parseThrown(GMANRenderMan& renderMan, std::string const& rib) {
  GMANRIBParse parser(renderMan, rib.c_str());
  return thrownBy([&] { parser.parse(); });
}

void testRequestReachesTheRenderer(std::string const& ribDir) {
  RecordingRenderMan renderMan;
  const Thrown thrown = parseThrown(renderMan, ribDir + "/colorsamples_request.rib");

  check(!thrown.threw, "request: the fixture parses to its end");
  check(renderMan.colorSamples.size() == 1, "request: one ColorSamples call arrives");
  if (renderMan.colorSamples.size() == 1) {
    ColorSamplesCall const& call = renderMan.colorSamples[0];
    check(call.n == 2, "request: the count is derived from the array lengths");
    check(call.nRGB == run(6, 0), "request: nRGB arrives intact and in order");
    check(call.RGBn == run(6, 6), "request: RGBn arrives intact and in order");
  }
  check(renderMan.colors.size() == 1, "request: one Color arrives");
  if (renderMan.colors.size() == 1) {
    check(renderMan.colors[0] == run(2, 12), "request: Color reads two samples after the request");
  }
}

void testRequestAtOneSampleStopsAtColor(std::string const& ribDir) {
  ColorSamplesRenderMan renderMan;
  const Thrown thrown = parseThrown(renderMan, ribDir + "/colorsamples_refused.rib");

  check(thrown.threw, "refused: the fixture stops the file");
  check(thrown.code == RIE_UNIMPLEMENT, "refused: the stop is RIE_UNIMPLEMENT");
  check(thrown.severity == RIE_ERROR, "refused: the stop is at RIE_ERROR");
}

void checkPairRejected(std::string const& ribDir, std::string const& name, RtInt code) {
  const std::string label = "malformed " + name + ": ";
  RecordingRenderMan renderMan;
  const Thrown thrown = parseThrown(renderMan, ribDir + "/malformed/colorsamples_" + name + ".rib");

  check(thrown.threw, label + "the file stops");
  check(thrown.code == code, label + "the stop carries the expected class");
  check(thrown.severity == RIE_ERROR, label + "the stop is at RIE_ERROR");
  check(renderMan.colorSamples.empty(), label + "no ColorSamples call arrives");
}

void testMalformedPairsAreRejected(std::string const& ribDir) {
  checkPairRejected(ribDir, "unequal", RIE_CONSISTENCY);
  checkPairRejected(ribDir, "oneempty", RIE_CONSISTENCY);
  checkPairRejected(ribDir, "notriple", RIE_CONSISTENCY);
  checkPairRejected(ribDir, "empty", RIE_RANGE);
  checkPairRejected(ribDir, "nan", RIE_RANGE);
  checkPairRejected(ribDir, "infinite", RIE_RANGE);
}

void checkBracketRejected(std::string const& ribDir, std::string const& name, std::string const& message) {
  const std::string label = "malformed " + name + ": ";
  RecordingRenderMan renderMan;
  const Thrown thrown = parseThrown(renderMan, ribDir + "/malformed/" + name + ".rib");

  check(thrown.threw, label + "the file stops");
  check(thrown.code == RIE_CONSISTENCY, label + "the stop is RIE_CONSISTENCY");
  check(thrown.severity == RIE_ERROR, label + "the stop is at RIE_ERROR");
  check(thrown.message == message, label + "the message names the request, the length and the count");
  check(renderMan.colors.empty(), label + "no Color call arrives");
  check(renderMan.opacities.empty(), label + "no Opacity call arrives");
}

void testBracketedArrayMustHoldTheSampleCount(std::string const& ribDir) {
  checkBracketRejected(ribDir, "color_bracket_short", "Color: array length 2 does not match the color sample count 3.");
  checkBracketRejected(ribDir, "color_bracket_samplecount",
                       "Color: array length 3 does not match the color sample count 2.");
  checkBracketRejected(ribDir, "opacity_bracket_short",
                       "Opacity: array length 2 does not match the color sample count 3.");
  checkBracketRejected(ribDir, "opacity_bracket_long",
                       "Opacity: array length 4 does not match the color sample count 3.");
}

void testWrittenColorSamplesReadBack() {
  const std::string rib = "colorsamples_writeback.rib";
  constexpr RtInt count = 4;
  std::vector<RtFloat> nRGB = run(3 * count, 0);
  std::vector<RtFloat> RGBn = run(3 * count, 3 * count);

  {
    GMANASCII writer;
    writer.RiBegin(rib.c_str());
    writer.RiColorSamples(count, nRGB.data(), RGBn.data());
    writer.RiWorldBegin();
    writer.RiSphereV(1, -1, 1, 360, 0, nullptr, nullptr);
    writer.RiWorldEnd();
    writer.RiEnd();
  }

  WritebackReader reader;
  const Thrown thrown = parseThrown(reader, rib);

  check(!thrown.threw, "write-back: the written file parses to its end");
  check(reader.colorSamples.size() == 1, "write-back: one ColorSamples call arrives");
  if (reader.colorSamples.size() == 1) {
    ColorSamplesCall const& call = reader.colorSamples[0];
    check(call.n == count, "write-back: the count survives");
    check(call.nRGB == nRGB, "write-back: nRGB survives in order");
    check(call.RGBn == RGBn, "write-back: RGBn survives in order");
  }
  check(reader.spheres == 1, "write-back: the Sphere after the request is read");
}

} // namespace

int main(int argc, char* argv[]) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: %s <tests/rib dir>\n", argv[0]);
    return 2;
  }
  const std::string ribDir = argv[1];

  testCountBelowOneIsRejected();
  testColorAndOpacityNeedThreeSamples();
  for (const RtInt count : {1, 2, 5}) {
    testParserReadsAtColorSampleCount(ribDir, count);
  }
  testRendererWithoutColorSamplesReadsThree(ribDir);
  testRequestReachesTheRenderer(ribDir);
  testRequestAtOneSampleStopsAtColor(ribDir);
  testMalformedPairsAreRejected(ribDir);
  testBracketedArrayMustHoldTheSampleCount(ribDir);
  testWrittenColorSamplesReadBack();

  return checkSummary("ColorSamples is honored by the parser and refused by the renderer where it cannot shade");
}
