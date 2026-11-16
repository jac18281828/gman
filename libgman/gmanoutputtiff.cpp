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
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <type_traits>
#include <vector>

#include "gmandefaults.h"
#include "gmanerror.h"
#include "gmanoutput.h"
#include "gmanoutputtiff.h"
#include "gmantiff.h"
#include "ri.h"

namespace gman {

namespace {

constexpr int kFloatBitsPerSample = 32;

// Stores sample as bitsPerSample bits (8 or 16) of an integer sample and
// returns the bytes written.
std::size_t storeSample(unsigned char* out, std::uint16_t sample, int bitsPerSample) {
  if (bitsPerSample == 16) {
    std::memcpy(out, &sample, sizeof(sample));
    return sizeof(sample);
  }
  *out = static_cast<unsigned char>(sample);
  return 1;
}

std::size_t storeSample(unsigned char* out, float sample, int /*bitsPerSample*/) {
  std::memcpy(out, &sample, sizeof(sample));
  return sizeof(sample);
}

// Writes samples, 4 per pixel, as a TIFF of xres by yres pixels with 3 or 4
// of them kept by mode. A float Sample writes 32-bit IEEE float samples;
// otherwise bitsPerSample is the integer sample's width.
template <typename Sample>
void writeTIFF(std::string const& outputName, OutputTIFF::Compression compression, int xres, int yres,
               GMANOutput::DisplayMode mode, int bitsPerSample, std::vector<Sample> const& samples) {
  const RtInt samplesperpixel = (mode == GMANOutput::RGB) ? 3 : 4;
  int const bits = std::is_same_v<Sample, float> ? kFloatBitsPerSample : bitsPerSample;

  TIFFWriter writer(outputName, (uint32_t)xres, (uint32_t)yres, (uint16_t)samplesperpixel, (uint16_t)bits, compression);
  if (!writer.isOpen()) {
    std::string errorMsg("Unable to open output file: ");
    errorMsg.append(outputName);
    throw(GMANError(RIE_SYSTEM, RIE_SEVERE, errorMsg.c_str()));
  }

  if (samplesperpixel == 4) {
    writer.tagAlphaAssociated();
  }
  if (bits == kFloatBitsPerSample) {
    writer.tagIEEEFloat();
  }

  writer.setImageDescription("GMAN Generated TIFF Image.\n"
                             "Copyright (c) 2002 John Cairns <john@2ad.com>\n"
                             "Licensed under the GNU Lesser General Public License v2.1 or later.\n");

  // length in memory of one row of pixels in the image
  const std::size_t bytesPerSample = (std::size_t)bits / 8;
  const std::size_t linebytes = (std::size_t)samplesperpixel * (std::size_t)xres * bytesPerSample;
  std::vector<unsigned char> buf;
  if (writer.scanlineSize() == linebytes) {
    buf.assign(linebytes, 0);
  } else {
    buf.assign(writer.scanlineSize(), 0);
  }

  // We set the strip size of the file to be size of one row of pixels
  writer.setRowsPerStrip(writer.defaultStripSize((uint32_t)(xres * samplesperpixel)));

  // Alpha comes from samples already: save resolved it alongside colour, so
  // this driver narrows nothing.
  for (int y = 0; y < yres; y++) {
    std::size_t byteOff = 0;
    int rowOff = y;
    for (int x = 0; x < xres; x++) {
      const std::size_t idx = 4 * ((std::size_t)y * (std::size_t)xres + (std::size_t)x);
      for (int channel = 0; channel < samplesperpixel; ++channel) {
        byteOff += storeSample(&buf[byteOff], samples[idx + (std::size_t)channel], bitsPerSample);
      }
    }
    // now write a scanline into the image
    if (!writer.writeScanline(buf.data(), rowOff)) {
      std::string errorMsg("Unable to write TIFF scanline ");
      errorMsg.append(std::to_string(rowOff));
      errorMsg.append(": ");
      errorMsg.append(outputName);
      throw(GMANError(RIE_SYSTEM, RIE_SEVERE, errorMsg.c_str()));
    }
  }
}

} // namespace

/*
 * RenderMan API gman::OutputTIFF
 *
 */

// default constructor
OutputTIFF::OutputTIFF(char const* path, int width, int height) : GMANOutput(path, width, height, DefaultBGColor) {
  // damn the torpedoes and the patents
  compression = LZW;
};

// default destructor
OutputTIFF::~OutputTIFF() {};

int OutputTIFF::maxBitsPerSample() const { return 16; }

bool OutputTIFF::writesFloat() const { return true; }

RtVoid OutputTIFF::writeImage(GMANOutput::DisplayMode mode, std::vector<std::uint16_t> const& samples,
                              int bitsPerSample, RtFloat /*gamma*/) {
  writeTIFF(outputName, compression, xres, yres, mode, bitsPerSample, samples);
}

RtVoid OutputTIFF::writeFloatImage(GMANOutput::DisplayMode mode, std::vector<float> const& samples, RtFloat /*gamma*/) {
  writeTIFF(outputName, compression, xres, yres, mode, kFloatBitsPerSample, samples);
}

// get/set the TIFF compression type
RtVoid OutputTIFF::setCompression(Compression c) { compression = c; }

OutputTIFF::Compression OutputTIFF::getCompression(void) const { return compression; }

} // namespace gman
