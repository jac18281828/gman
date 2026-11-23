/* SPDX-License-Identifier: LGPL-2.1-or-later */

/* This is part of GMAN, a RenderMan-compatible renderer.
 *
 * Copyright (c) 2001, 2000, 1999  John Cairns
 *
 * Author: John Cairns <john@2ad.com>
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
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <vector>

#include "gmanbitmap.h"
#include "gmancolor.h"
#include "gmandefaults.h"
#include "gmanerror.h"
#include "gmanfiledrivers.h"
#include "gmanframebuffer.h"
#include "gmangamma.h"
#include "gmanoutput.h"
#include "gmanoutputnarrow.h"
#include "gmanoutputpnm.h"
#include "gmanquantize.h"
#include "gmansampling.h"
#include "ri.h"

/*
 * RenderMan API GMANOutput
 *
 */

namespace {

// The dither draw's hash seed: distinct from every seed a renderer's own
// integrator uses, so a pixel's quantization draw never correlates with
// its shading draws.
constexpr std::uint32_t kDitherSeed = 0x5eed;

// What a refusal rule may ask of the output: the name an error gives it and
// the widest sample and the float it writes.
struct QuantizeTraits {
  std::string_view displayName;
  int bits;
  bool writesFloat;
};

struct QuantizeRefusal {
  bool (*applies)(QuantizeTraits const&, GMANQuantize const&);
  RtInt code;
  std::string (*message)(QuantizeTraits const&, GMANQuantize const&);
};

bool ditherUnusable(RtFloat dither) { return !(std::isfinite(dither) && dither >= 0); }

// Evaluated in order; the first row that applies stops the request.
auto const kQuantizeRefusals = std::to_array<QuantizeRefusal>({
    {[](QuantizeTraits const&, GMANQuantize const& q) { return q.one < 0; }, RIE_RANGE,
     [](QuantizeTraits const&, GMANQuantize const& q) {
       return std::format("Quantize one {} must not be negative", q.one);
     }},
    {[](QuantizeTraits const& t, GMANQuantize const& q) { return q.one == 0 && !t.writesFloat; }, RIE_INCAPABLE,
     [](QuantizeTraits const& t, GMANQuantize const&) {
       return std::format("{} writes integer samples; Quantize one 0 cannot be honoured", t.displayName);
     }},
    {[](QuantizeTraits const&, GMANQuantize const& q) { return q.one > 0 && q.min < 0; }, RIE_RANGE,
     [](QuantizeTraits const&, GMANQuantize const& q) {
       return std::format("Quantize min {} must not be negative", q.min);
     }},
    {[](QuantizeTraits const&, GMANQuantize const& q) { return q.one > 0 && q.min > q.max; }, RIE_RANGE,
     [](QuantizeTraits const&, GMANQuantize const& q) {
       return std::format("Quantize min {} exceeds max {}", q.min, q.max);
     }},
    {[](QuantizeTraits const&, GMANQuantize const& q) { return q.one > 0 && ditherUnusable(q.ditheramplitude); },
     RIE_RANGE,
     [](QuantizeTraits const&, GMANQuantize const& q) {
       return std::format("Quantize dither amplitude {} must be a finite number of 0 or greater", q.ditheramplitude);
     }},
    {[](QuantizeTraits const& t, GMANQuantize const& q) {
       return q.one > 0 && static_cast<std::int64_t>(q.max) > (std::int64_t{1} << t.bits) - 1;
     },
     RIE_INCAPABLE,
     [](QuantizeTraits const& t, GMANQuantize const& q) {
       return std::format("{} writes {}-bit samples; Quantize max {} cannot be honoured", t.displayName, t.bits, q.max);
     }},
});

float floatSample(GMANColor::ColorSampleType v) { return std::isnan(v) ? 0.0f : static_cast<float>(v); }

std::vector<float> floatSamples(GMANBitmap const& image, RtFloat gain, RtFloat gamma) {
  std::size_t const width = static_cast<std::size_t>(image.getWidth());
  std::size_t const height = static_cast<std::size_t>(image.getHeight());
  std::vector<float> floats(4 * width * height);
  for (std::size_t y = 0; y < height; y++) {
    for (std::size_t x = 0; x < width; x++) {
      GMANColor const color =
          gman::gammaCorrected(image.getPixel(static_cast<int>(x), static_cast<int>(y)), gain, gamma);
      GMANAlpha const& alpha = image.getAlpha(static_cast<int>(x), static_cast<int>(y));
      GMANColor::ColorSampleType const coverage =
          (alpha.getRed() + alpha.getGreen() + alpha.getBlue()) / static_cast<GMANColor::ColorSampleType>(3.0);
      std::size_t const idx = 4 * (y * width + x);
      floats[idx + 0] = floatSample(color.getRed());
      floats[idx + 1] = floatSample(color.getGreen());
      floats[idx + 2] = floatSample(color.getBlue());
      floats[idx + 3] = floatSample(coverage);
    }
  }
  return floats;
}

} // namespace

// default constructor

GMANOutput::GMANOutput() : GMANFrameBuffer(), outputName() {}

GMANOutput::GMANOutput(const char* name, int width, int height)
    : GMANFrameBuffer(width, height, DefaultBGColor), outputName(name) {};

// default constructor
GMANOutput::GMANOutput(const char* name, int width, int height, const GMANColor& background)
    : GMANFrameBuffer(width, height, background), outputName(name) {};

// default destructor
GMANOutput::~GMANOutput() {};

int GMANOutput::maxBitsPerSample() const { return 8; }

bool GMANOutput::writesFloat() const { return false; }

RtVoid GMANOutput::writeFloatImage(DisplayMode /*mode*/, std::vector<float> const& /*samples*/, RtFloat /*gamma*/) {}

namespace gman {

void checkQuantize(GMANOutput const& output, GMANQuantize const& quantize) {
  FileDriverEntry const* const entry = fileDriverEntry(output);
  QuantizeTraits const traits{entry != nullptr ? std::string_view(entry->displayName) : "output",
                              output.maxBitsPerSample(), output.writesFloat()};
  for (QuantizeRefusal const& refusal : kQuantizeRefusals) {
    if (refusal.applies(traits, quantize)) {
      throw GMANError(refusal.code, RIE_ERROR, refusal.message(traits, quantize).c_str());
    }
  }
}

} // namespace gman

RtVoid GMANOutput::save(DisplayMode mode, RtFloat gain, RtFloat gamma, GMANQuantize const& quantize) {
  gman::checkQuantize(*this, quantize);

  if (quantize.one == 0 && writesFloat()) {
    writeFloatImage(mode, floatSamples(*this, gain, gamma), gamma);
    return;
  }

  const int bitsPerSample = quantize.max > 255 ? 16 : 8;

  gman::FileDriverEntry const* const entry = gman::fileDriverEntry(*this);
  bool const straightColor = entry != nullptr && entry->straightAlpha && mode != RGB;

  std::vector<std::uint16_t> samples(4 * static_cast<std::size_t>(xres) * static_cast<std::size_t>(yres));
  for (int y = 0; y < yres; y++) {
    for (int x = 0; x < xres; x++) {
      GMANAlpha const& alpha = getAlpha(x, y);
      GMANColor::ColorSampleType const coverage =
          (alpha.getRed() + alpha.getGreen() + alpha.getBlue()) / static_cast<GMANColor::ColorSampleType>(3.0);
      GMANColor linear = getPixel(x, y);
      if (straightColor && coverage > 0 && coverage < 1) {
        linear /= coverage;
      }
      GMANColor const color = gman::gammaCorrected(linear, gain, gamma);

      // One draw per pixel serves R, G, B and alpha: the rule is monotone
      // in v for a fixed draw, so equal channels in stay equal out.
      RtFloat const xi = 2.0f * gman::unitFloat(gman::sampleHash(kDitherSeed, x, y, 0, 0)) - 1.0f;

      std::size_t const idx =
          4 * (static_cast<std::size_t>(y) * static_cast<std::size_t>(xres) + static_cast<std::size_t>(x));
      samples[idx + 0] = gman::quantizedSample(color.getRed(), quantize, xi);
      samples[idx + 1] = gman::quantizedSample(color.getGreen(), quantize, xi);
      samples[idx + 2] = gman::quantizedSample(color.getBlue(), quantize, xi);
      samples[idx + 3] = gman::quantizedSample(coverage, quantize, xi);
    }
  }
  if (auto* const pnm = dynamic_cast<gman::OutputPNM*>(this)) {
    pnm->setMaxval(quantize.max);
  }
  writeImage(mode, samples, bitsPerSample, gamma);
}
