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

#include <stdio.h>
extern "C" {
#include <jpeglib.h>
}

#include "gmandefaults.h"
#include "gmanerror.h"
#include "gmanoutput.h"
#include "gmanoutputjpeg.h"
#include "gmanoutputnarrow.h"
#include "ri.h"

namespace gman {

/*
 * RenderMan API gman::OutputJPEG
 *
 */

// default constructor
//
// quality was never initialized here, so save()'s jpeg_set_quality(&cinfo,
// getQuality(), ...) read garbage -- reachable only once this class was
// ever actually instantiated via RIB, which happened for the first time
// once RiWorldBegin gained its jpg/jpeg dispatch branch. 75 matches
// libjpeg's own conventional default quality.
OutputJPEG::OutputJPEG(const char* path, int width, int height)
    : GMANOutput(path, width, height, DefaultBGColor), quality(75) {};

// default destructor
OutputJPEG::~OutputJPEG() {};

RtVoid OutputJPEG::writeImage(GMANOutput::DisplayMode /*mode*/, std::vector<GMANColor> const& image,
                              RtFloat /*gamma*/) {
  FILE* jpegFile = fopen(outputName.c_str(), "w");
  if (jpegFile) {
    struct jpeg_compress_struct cinfo; // jpeg compression params
    struct jpeg_error_mgr jerr;        // error handler

    /* 3 color samples per pixel */
    JSAMPLE* row = new JSAMPLE[xres * 3];
    if (row) {
      /* pointer to JSAMPLE row */
      JSAMPROW row_pointer[1] = {row};

      // allocate jpeg compression object
      cinfo.err = jpeg_std_error(&jerr);
      // compress object
      jpeg_create_compress(&cinfo);

      // specify output file
      jpeg_stdio_dest(&cinfo, jpegFile);

      // setup compression params

      cinfo.image_width = xres;
      cinfo.image_height = yres;

      cinfo.input_components = 3;     // number of color components per pixel
      cinfo.in_color_space = JCS_RGB; // color space of image

      // set defaults
      jpeg_set_defaults(&cinfo);

      // set image quality
      jpeg_set_quality(&cinfo, getQuality(), TRUE /* limit to baseline-JPEG values */);

      // start compress job
      jpeg_start_compress(&cinfo, TRUE);

      // copy frameBuffer to jpeg sample array
      for (int y = 0; y < yres; y++) {
        int colOff = 0;
        for (int x = 0; x < xres; x++) {
          GMANColor const& color = image[(std::size_t)y * (std::size_t)xres + (std::size_t)x];

          // default, (no reduction) is 24bit

          // write r, g, and b

          // use x*3 + [0,1,2] .. aRtVoid a multiply by summing.
          row[colOff++] = gman::narrowedByte(color.getRed());
          row[colOff++] = gman::narrowedByte(color.getGreen());
          row[colOff++] = gman::narrowedByte(color.getBlue());
        }
        // write jpeg scanline
        jpeg_write_scanlines(&cinfo, row_pointer, 1);
      }

      jpeg_finish_compress(&cinfo);

      // we are done
      jpeg_destroy_compress(&cinfo);
      delete[] row;
    }

    fclose(jpegFile);
  } else {
    std::string errorMsg("Unable to open output file: ");
    errorMsg.append(outputName);
    throw(GMANError(RIE_SYSTEM, RIE_SEVERE, errorMsg.c_str()));
  }
}

RtVoid OutputJPEG::setQuality(int q) { quality = q; }

int OutputJPEG::getQuality(RtVoid) { return quality; }

} // namespace gman
