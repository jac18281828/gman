/* SPDX-License-Identifier: LGPL-2.1-or-later */

/* This is part of GMAN, a RenderMan-compatible renderer.
 *
 * Copyright (c) 2002, 2001, 2000, 1999  John Cairns
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

extern "C" {
#include <png.h>
#include <zlib.h>
}

#include "gmandefaults.h"
#include "gmanerror.h"
#include "gmanoutput.h"
#include "gmanoutputnarrow.h"
#include "gmanoutputpng.h"
#include "ri.h"

namespace gman {

/*
 * RenderMan API gman::OutputPNG
 *
 */

// default constructor
OutputPNG::OutputPNG(const char* path, int width, int height) : GMANOutput(path, width, height, DefaultBGColor) {};

// default destructor
OutputPNG::~OutputPNG() {};

RtVoid OutputPNG::writeImage(GMANOutput::DisplayMode mode, std::vector<GMANColor> const& image, RtFloat gamma) {
  // write a PNG file to 'fileName'

  // open jpeg output file for writing
  FILE* pngFile = fopen(outputName.c_str(), "w");
  if (pngFile == NULL) {
    std::string errorMsg("Unable to open output file: ");
    errorMsg.append(outputName);
    throw(GMANError(RIE_SYSTEM, RIE_SEVERE, errorMsg.c_str()));
  }

  png_structp png_ptr;
  png_infop info_ptr;

  /* initialize png */
  png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);

  if (png_ptr == NULL) {
    // very bad
    fclose(pngFile);
    throw(GMANError(RIE_SYSTEM, RIE_SEVERE, "PNG initialization failure."));
  }

  // allocate the image information data
  info_ptr = png_create_info_struct(png_ptr);
  if (info_ptr == NULL) {
    // ouch!
    fclose(pngFile);
    png_destroy_write_struct(&png_ptr, (png_infopp)NULL);
    throw(GMANError(RIE_SYSTEM, RIE_SEVERE, "PNG initialization failure."));
  }

  // set the error handler
#ifdef PNG_SETJMP_SUPPORTED
  if (setjmp(png_jmpbuf(png_ptr))) {
    // jump here on error
    fclose(pngFile);
    png_destroy_write_struct(&png_ptr, (png_infopp)NULL);
    throw(GMANError(RIE_SYSTEM, RIE_SEVERE, "Internal PNG library error."));
  }
#endif

  /* setup C stream */
  png_init_io(png_ptr, pngFile);

  // set compression level
  png_set_compression_level(png_ptr, Z_BEST_COMPRESSION);

  // png_set_invert_mono(png_ptr); // reverse video black->white

  /* setup image information... */

  // "A" writes coverage alone, one sample, no colour. "RGB" writes colour
  // alone, no alpha. Every other mode -- RGBA and everything this driver
  // has no channel for -- writes RGB plus one coverage sample, the same
  // not-RGB bucket OutputTIFF uses.
  const int colorType = (mode == RGB) ? PNG_COLOR_TYPE_RGB
                        : (mode == A) ? PNG_COLOR_TYPE_GRAY
                                      : PNG_COLOR_TYPE_RGB_ALPHA;

  png_set_IHDR(png_ptr, info_ptr, xres, yres, 8, colorType, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT,
               PNG_FILTER_TYPE_DEFAULT);

  // set time
  png_time modtime;
  png_convert_from_time_t(&modtime, time(NULL));
  png_set_tIME(png_ptr, info_ptr, &modtime);

  // set PNG gamma correction
  png_set_gAMA(png_ptr, info_ptr, gamma);

  // set bgcolor black;
  png_color_16 bgcolor;
  bgcolor.red = 0x0;
  bgcolor.green = 0x0;
  bgcolor.blue = 0x0;
  png_set_bKGD(png_ptr, info_ptr, &bgcolor);

  // write useful comments
  png_text text_ptr[3];
  text_ptr[0].key = const_cast<png_charp>("Title");
  text_ptr[0].text = const_cast<png_charp>("GMAN Generated Image");
  text_ptr[0].compression = PNG_TEXT_COMPRESSION_NONE;
  text_ptr[1].key = const_cast<png_charp>("Copyright");
  text_ptr[1].text = const_cast<png_charp>("Copyright (c) 2002 John Cairns");
  text_ptr[1].compression = PNG_TEXT_COMPRESSION_NONE;
  text_ptr[2].key = const_cast<png_charp>("Author");
  text_ptr[2].text = const_cast<png_charp>("John Cairns <john@2ad.com> ");
  text_ptr[2].compression = PNG_TEXT_COMPRESSION_NONE;
  png_set_text(png_ptr, info_ptr, text_ptr, 2);
  png_write_info(png_ptr, info_ptr);

  // make sure < 8-bit images are packed into pixels as much as possible
  png_set_packing(png_ptr);

  // png_read_update_info is a read-side call (its own doc: "MUST be
  // called before png_read_update_info or png_start_read_image") --
  // png_ptr here came from png_create_write_struct and was never set up
  // for reading, so this corrupted internal state libpng only fills in
  // for a read stream. png_write_info already populated info_ptr's
  // rowbytes from the IHDR set above; no update call is needed on the
  // write side.
  png_uint_32 rowbytes = png_get_rowbytes(png_ptr, info_ptr);

  // One row of rowbytes per scanline -- yres of them, not xres. A square
  // Format hid this: the two are equal there, so the buffer happened to
  // be exactly right.
  png_byte* image_data = new png_byte[rowbytes * yres];
  if (image_data) {
    png_bytep* row_pointers = new png_bytep[yres];
    if (row_pointers) {

      /* set the row_pointers to point at the correct offset */
      png_uint_32 i;
      for (i = 0; i < (png_uint_32)yres; ++i)
        row_pointers[i] = image_data + i * rowbytes;

      // set x,y pixel color values

      // memory is contiguous by x not by y so order all x operations to
      // occur before y operations to take advantage of CPU cache
      int rowoffset = 0;

      for (int y = 0; y < yres; y++) {

        // invert order data is written
        // so viewer sees map upright
        unsigned char* src = image_data + rowoffset;

        // avoid a multiply
        rowoffset += rowbytes;

        int colOff = 0;

        for (int x = 0; x < xres; x++) {

          if (mode != A) {
            GMANColor const& color = image[(std::size_t)y * (std::size_t)xres + (std::size_t)x];

            src[colOff++] = gman::narrowedByte(color.getRed());
            src[colOff++] = gman::narrowedByte(color.getGreen());
            src[colOff++] = gman::narrowedByte(color.getBlue());
          }

          if (mode != RGB) {
            // Coverage, not colour: never gamma-corrected, read straight
            // from the alpha buffer rather than the gamma-corrected image
            // this loop uses above.
            src[colOff++] = gman::coverageByte(getAlpha(x, y));
          }
        }
      }
      // Write out entire image
      png_write_image(png_ptr, row_pointers);
      delete[] row_pointers;
    }
    delete[] image_data;
  }

  png_write_end(png_ptr, info_ptr);

  // clean up write struct. Passing info_ptr here (not NULL) is what frees
  // it -- png_create_info_struct and the png_set_text/tIME/gAMA/bKGD calls
  // above all allocate through it, and passing NULL would destroy only
  // png_ptr, leaking the rest.
  png_destroy_write_struct(&png_ptr, &info_ptr);
  fclose(pngFile);
}

} // namespace gman
