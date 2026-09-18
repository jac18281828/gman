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

/*
 * RenderMan API GMANOutputTIFF
 *
 */

// default constructor
GMANOutputTIFF::GMANOutputTIFF(const char* path, int width, int height)
    : GMANOutput(path, width, height, DefaultBGColor) {
  // damn the torpedoes and the patents
  compression = LZW;
};

// default destructor
GMANOutputTIFF::~GMANOutputTIFF() {};

RtVoid GMANOutputTIFF::save(GMANOutput::DisplayMode /*mode*/, RtFloat gain, RtFloat gamma) {
  const RtInt samplesperpixel = 4; // RGBA

  GMANTIFFWriter writer(outputName, (uint32_t)xres, (uint32_t)yres, (uint16_t)samplesperpixel, compression);
  if (!writer.isOpen()) {
    std::string errorMsg("Unable to open output file: ");
    errorMsg.append(outputName);
    throw(GMANError(RIE_SYSTEM, RIE_SEVERE, errorMsg.c_str()));
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

  // copy frameBuffer to jpeg sample array
  for (int y = 0; y < yres; y++) {
    int colOff = 0, rowOff = y;
    for (int x = 0; x < xres; x++) {
      GMANColorRGB color;
      color = gman::gammaCorrected(getPixel(x, y), gain, gamma);

      if (quantizer)
        quantizer->doColor(color);

      // default, (no reduction) is 32bit

      // write r, g, b, a byte
      buf[colOff++] = color.getRed();
      buf[colOff++] = color.getGreen();
      buf[colOff++] = color.getBlue();

      // FIXME FIXME FIXME
      // FIX Alpha support

      buf[colOff++] = 255;
    }
    // now write a scanline into the image
    if (!writer.writeScanline(buf.data(), rowOff)) {
      // FIXME
      // throw an error here
      break;
    }
  }

  // now isn't that just easy.
}

// get/set the TIFF compression type
RtVoid GMANOutputTIFF::setCompression(Compression c) { compression = c; }

GMANOutputTIFF::Compression GMANOutputTIFF::getCompression(void) const { return compression; }
