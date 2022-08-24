/* This is part of the GNU GMAN Library, a FREE implementation of the
 * RenderMan Interface Specification.
 *
 * Copyright (c) 2001, 2000, 1999  John Cairns 
 *
 * Author: John Cairns <john@2ad.com>
 */

/*
  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Library General Public
  License as published by the Free Software Foundation; either
  version 2 of the License, or (at your option) any later version.
  

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Library General Public License for more details.
  
  You should have received a copy of the GNU Library General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.

  To contact the author of GNU GMAN, write to John Cairns, 607 E STUART ST, 
  FT COLLINS, CO, 80525, USA, or write via E-mail john@2ad.com.
*/

#include <stdio.h>
/* util headers */
#ifdef HAVE_LIBJPEG
extern "C" {
#include <jpeglib.h> 
}
#endif

/* Local Headers */
#include "ri.h"      /* RenderMan Interface */
#include "gmanoutput.h" /* Super class */
#include "gmanoutputjpeg.h" /* Declaration Header */
#include "gmandefaults.h"

/*
 * RenderMan API GMANOutputJPEG
 *
 */


// default constructor
GMANOutputJPEG::GMANOutputJPEG(const char *path, int width, int height) : 
  GMANOutput(path, width, height, DefaultBGColor)  { };


// default destructor 
GMANOutputJPEG::~GMANOutputJPEG() { };

RtVoid GMANOutputJPEG::save(GMANOutput::DisplayMode mode, 
			    RtFloat gain, 
			    RtFloat gamma) {
#ifdef HAVE_LIBJPEG

  gammaCorrect.setExposure(gain, gamma);
  FILE *jpegFile = fopen(outputName.c_str(), "w");
  if(jpegFile) {
    struct jpeg_compress_struct cinfo;  // jpeg compression params
    struct jpeg_error_mgr jerr;         // error handler
    
    /* 3 color samples per pixel */
    JSAMPLE *row = new JSAMPLE[xres*3];
    if(row) {
      /* pointer to JSAMPLE row */
      JSAMPROW row_pointer[1] = { row };

      // allocate jpeg compression object
      cinfo.err = jpeg_std_error(&jerr);
      // compress object
      jpeg_create_compress(&cinfo);

      // specify output file
      jpeg_stdio_dest(&cinfo, jpegFile);

      // setup compression params

      cinfo.image_width = xres;
      cinfo.image_height = yres;

      cinfo.input_components = 3;  // number of color components per pixel
      cinfo.in_color_space = JCS_RGB; // color space of image

    // set defaults
      jpeg_set_defaults(&cinfo);

      // set image quality
      jpeg_set_quality(&cinfo, getQuality(), TRUE /* limit to baseline-JPEG values */);

      // start compress job
      jpeg_start_compress(&cinfo, TRUE);


      // copy frameBuffer to jpeg sample array
      for(int y=0; y<yres; y++) {
	int colOff=0, rowOff=y;
	for(int x=0; x<xres; x++) {
	  GMANColorRGB color;

	  // get a pixel
	  color = getPixel(x,y);

	  // color correct it
	  gammaCorrect.correct(color);

	  if(quantizer) {
	      color = quantizer->doColor(color);
	  }
	
	  // default, (no reduction) is 24bit
	
	  // write r, g, and b

	  // use x*3 + [0,1,2] .. aRtVoid a multiply by summing.
	  row[colOff++] = color.getRed();
	  row[colOff++] = color.getGreen();
	  row[colOff++] = color.getBlue();
	}
	// write jpeg scanline
	jpeg_write_scanlines(&cinfo, row_pointer, 1);
      }

      jpeg_finish_compress(&cinfo);

      // we are done
      jpeg_destroy_compress(&cinfo);
      delete []row;
    }

    fclose(jpegFile);
  } else {
    // FIXME
    // throw an error here
  }
#endif
}


RtVoid GMANOutputJPEG::setQuality(int q) {
  quality = q;
}

int GMANOutputJPEG::getQuality(RtVoid) {
  return quality;
}

