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

#include <cstdint>
#include <vector>

#include "gmandefaults.h"
#include "gmanlog.h"
#include "gmanoutput.h"
#include "gmanoutputnarrow.h"
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

RtVoid GMANOutput::save(DisplayMode mode, RtFloat gain, RtFloat gamma, GMANQuantize const& quantize) {
  const RtInt widest = (1 << maxBitsPerSample()) - 1;
  const bool honoured = quantize.one > 0 && quantize.min >= 0 && quantize.min <= quantize.max && quantize.max <= widest;

  GMANQuantize resolved = quantize;
  if (!honoured) {
    warning("Quantize: \"{}\" cannot honour {} {} {}; quantizing with 255 0 255 {} instead.", outputName, quantize.one,
            quantize.min, quantize.max, quantize.ditheramplitude);
    resolved = GMANQuantize{255, 0, 255, quantize.ditheramplitude};
  }
  const int bitsPerSample = resolved.max > 255 ? 16 : 8;

  std::vector<std::uint16_t> samples(4 * static_cast<std::size_t>(xres) * static_cast<std::size_t>(yres));
  for (int y = 0; y < yres; y++) {
    for (int x = 0; x < xres; x++) {
      GMANColor const color = gman::gammaCorrected(getPixel(x, y), gain, gamma);
      GMANAlpha const& alpha = getAlpha(x, y);
      GMANColor::ColorSampleType const coverage =
          (alpha.getRed() + alpha.getGreen() + alpha.getBlue()) / static_cast<GMANColor::ColorSampleType>(3.0);

      // One draw per pixel serves R, G, B and alpha: the rule is monotone
      // in v for a fixed draw, so equal channels in stay equal out.
      RtFloat const xi = 2.0f * gman::unitFloat(gman::sampleHash(kDitherSeed, x, y, 0, 0)) - 1.0f;

      std::size_t const idx =
          4 * (static_cast<std::size_t>(y) * static_cast<std::size_t>(xres) + static_cast<std::size_t>(x));
      samples[idx + 0] = gman::quantizedSample(color.getRed(), resolved, xi);
      samples[idx + 1] = gman::quantizedSample(color.getGreen(), resolved, xi);
      samples[idx + 2] = gman::quantizedSample(color.getBlue(), resolved, xi);
      samples[idx + 3] = gman::quantizedSample(coverage, resolved, xi);
    }
  }
  writeImage(mode, samples, bitsPerSample, gamma);
}
