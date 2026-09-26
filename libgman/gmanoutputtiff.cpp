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
#include <vector>

#include "gmandefaults.h"
#include "gmanerror.h"
#include "gmanoutput.h"
#include "gmanoutputtiff.h"
#include "gmantiff.h"
#include "ri.h"

namespace gman {

/*
 * RenderMan API gman::OutputTIFF
 *
 */

// default constructor
OutputTIFF::OutputTIFF(const char* path, int width, int height) : GMANOutput(path, width, height, DefaultBGColor) {
  // damn the torpedoes and the patents
  compression = LZW;
};

// default destructor
OutputTIFF::~OutputTIFF() {};

RtVoid OutputTIFF::writeImage(GMANOutput::DisplayMode mode, std::vector<std::uint16_t> const& samples,
                              int /*bitsPerSample*/, RtFloat /*gamma*/) {
  const RtInt samplesperpixel = (mode == GMANOutput::RGB) ? 3 : 4;

  TIFFWriter writer(outputName, (uint32_t)xres, (uint32_t)yres, (uint16_t)samplesperpixel, compression);
  if (!writer.isOpen()) {
    std::string errorMsg("Unable to open output file: ");
    errorMsg.append(outputName);
    throw(GMANError(RIE_SYSTEM, RIE_SEVERE, errorMsg.c_str()));
  }

  if (samplesperpixel == 4) {
    writer.tagAlphaAssociated();
  }

  writer.setImageDescription("GMAN Generated TIFF Image.\n"
                             "Copyright (c) 2002 John Cairns <john@2ad.com>\n"
                             "Licensed under the GNU Lesser General Public License v2.1 or later.\n");

  // length in memory of one row of pixels in the image
  const std::size_t linebytes = (std::size_t)samplesperpixel * (std::size_t)xres;
  std::vector<unsigned char> buf;
  if (writer.scanlineSize() == linebytes) {
    buf.assign(linebytes, 0);
  } else {
    buf.assign(writer.scanlineSize(), 0);
  }

  // We set the strip size of the file to be size of one row of pixels
  writer.setRowsPerStrip(writer.defaultStripSize((uint32_t)(xres * samplesperpixel)));

  // Alpha comes from samples already: save resolved and quantized it
  // alongside colour, one draw per pixel, so this driver narrows nothing.
  for (int y = 0; y < yres; y++) {
    int colOff = 0, rowOff = y;
    for (int x = 0; x < xres; x++) {
      const std::size_t idx = 4 * ((std::size_t)y * (std::size_t)xres + (std::size_t)x);
      for (int channel = 0; channel < samplesperpixel; ++channel) {
        buf[(std::size_t)colOff++] = static_cast<unsigned char>(samples[idx + (std::size_t)channel]);
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

// get/set the TIFF compression type
RtVoid OutputTIFF::setCompression(Compression c) { compression = c; }

OutputTIFF::Compression OutputTIFF::getCompression(void) const { return compression; }

} // namespace gman
