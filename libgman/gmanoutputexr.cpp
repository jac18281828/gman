/* SPDX-License-Identifier: LGPL-2.1-or-later */

/* This is part of GMAN, a RenderMan-compatible renderer.
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
#include <cstdint>
#include <exception>
#include <string>
#include <vector>

#include <OpenEXR/ImfChannelList.h>
#include <OpenEXR/ImfFrameBuffer.h>
#include <OpenEXR/ImfHeader.h>
#include <OpenEXR/ImfOutputFile.h>
#include <OpenEXR/ImfPixelType.h>
#include <OpenEXR/ImfThreading.h>

#include "gmandefaults.h"
#include "gmanerror.h"
#include "gmanoutput.h"
#include "gmanoutputexr.h"
#include "ri.h"

namespace gman {

namespace {

constexpr std::array<char const*, 4> kChannelNames = {"R", "G", "B", "A"};

struct ChannelRange {
  std::size_t first;
  std::size_t count;
};

ChannelRange channelRange(GMANOutput::DisplayMode mode) {
  switch (mode) {
  case GMANOutput::RGB:
    return {0, 3};
  case GMANOutput::A:
    return {3, 1};
  default:
    return {0, 4};
  }
}

std::vector<float> interleaved(std::vector<float> const& samples, ChannelRange range, std::size_t pixels) {
  std::vector<float> kept(range.count * pixels);
  for (std::size_t pixel = 0; pixel < pixels; ++pixel) {
    for (std::size_t channel = 0; channel < range.count; ++channel) {
      kept[range.count * pixel + channel] = samples[4 * pixel + range.first + channel];
    }
  }
  return kept;
}

void writeEXR(std::string const& outputName, int xres, int yres, GMANOutput::DisplayMode mode,
              std::vector<float> const& samples) {
  ChannelRange const range = channelRange(mode);
  std::size_t const width = static_cast<std::size_t>(xres);
  std::vector<float> kept = interleaved(samples, range, width * static_cast<std::size_t>(yres));

  try {
    Imf::setGlobalThreadCount(0);

    Imf::Header header(xres, yres);
    Imf::FrameBuffer frameBuffer;
    std::size_t const xStride = range.count * sizeof(float);
    std::size_t const yStride = xStride * width;
    for (std::size_t channel = 0; channel < range.count; ++channel) {
      char const* const name = kChannelNames[range.first + channel];
      header.channels().insert(name, Imf::Channel(Imf::FLOAT));
      frameBuffer.insert(name,
                         Imf::Slice(Imf::FLOAT, reinterpret_cast<char*>(kept.data() + channel), xStride, yStride));
    }

    Imf::OutputFile file(outputName.c_str(), header);
    file.setFrameBuffer(frameBuffer);
    file.writePixels(yres);
  } catch (std::exception const& error) {
    std::string errorMsg("Unable to write output file: ");
    errorMsg.append(outputName);
    errorMsg.append(": ");
    errorMsg.append(error.what());
    throw GMANError(RIE_SYSTEM, RIE_SEVERE, errorMsg.c_str());
  }
}

} // namespace

OutputEXR::OutputEXR(char const* path, int width, int height) : GMANOutput(path, width, height, DefaultBGColor) {};

OutputEXR::~OutputEXR() {};

bool OutputEXR::writesFloat() const { return true; }

RtVoid OutputEXR::writeFloatImage(GMANOutput::DisplayMode mode, std::vector<float> const& samples, RtFloat /*gamma*/) {
  writeEXR(outputName, xres, yres, mode, samples);
}

RtVoid OutputEXR::writeImage(GMANOutput::DisplayMode /*mode*/, std::vector<std::uint16_t> const& /*samples*/,
                             int /*bitsPerSample*/, RtFloat /*gamma*/) {
  throw GMANError(RIE_BUG, RIE_SEVERE, "OpenEXR received integer samples");
}

} // namespace gman
