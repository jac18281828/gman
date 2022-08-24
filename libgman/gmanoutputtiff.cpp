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
  FT COLLINS, CO 80525, USA, or write via E-mail john@2ad.com.
*/
#ifdef HAVE_LIBTIFF
extern "C" {
#include <tiff.h> 
#include <tiffio.h> 
}
#endif
/* Local Headers */
#include "ri.h"      /* RenderMan Interface */
#include "gmanoutput.h" /* Super class */
#include "gmanoutputtiff.h" /* Declaration Header */
#include "gmanerror.h"
#include "gmandefaults.h"

/*
 * RenderMan API GMANOutputTIFF
 *
 */

// default constructor
GMANOutputTIFF::GMANOutputTIFF(const char *path, int width, int height) : 
    GMANOutput(path, width, height, DefaultBGColor) { 
  // damn the torpedoes and the patents
  compression=LZW;
};


// default destructor 
GMANOutputTIFF::~GMANOutputTIFF() { };


RtVoid GMANOutputTIFF::save(GMANOutput::DisplayMode mode, 
			    RtFloat gain, 
			    RtFloat gamma) {
#ifdef HAVE_LIBTIFF
    gammaCorrect.setExposure(gain, gamma);

    TIFF *file;

    file = TIFFOpen(outputName.c_str(), "w");
    if (file == NULL) {
	string errorMsg("Unable to open output file: ");
	errorMsg.append(outputName);
	throw(GMANError(RIE_SYSTEM, RIE_SEVERE, errorMsg.c_str()));
    }

    const RtInt samplesperpixel=4;

    // setup the TIFF
    TIFFSetField(file, TIFFTAG_IMAGEWIDTH, (uint32) xres);
    TIFFSetField(file, TIFFTAG_IMAGELENGTH, (uint32) yres);
    TIFFSetField(file, TIFFTAG_BITSPERSAMPLE, 8);
	switch(compression) {
	    case NONE:
		TIFFSetField(file, TIFFTAG_COMPRESSION, COMPRESSION_NONE);
		break;
	    case PACKBITS:
		TIFFSetField(file, TIFFTAG_COMPRESSION, COMPRESSION_PACKBITS);
		break;
	    case LZW:
		TIFFSetField(file, TIFFTAG_COMPRESSION, COMPRESSION_LZW);
		break;
	    case CCITTRLE:
		TIFFSetField(file, TIFFTAG_COMPRESSION, COMPRESSION_CCITTRLE);
		break;
	    case CCITTFAX3:
		TIFFSetField(file, TIFFTAG_COMPRESSION, COMPRESSION_CCITTFAX3);
		break;
	    case CCITTFAX4:
		TIFFSetField(file, TIFFTAG_COMPRESSION, COMPRESSION_CCITTFAX4);
		break;
	}
     
	TIFFSetField(file, TIFFTAG_SAMPLESPERPIXEL, samplesperpixel); // RGBA

	TIFFSetField(file, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT);  // set the origin of the image.
	TIFFSetField(file, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB); 
	TIFFSetField(file, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
	TIFFSetField(file, TIFFTAG_ROWSPERSTRIP, 1);
	TIFFSetField(file, TIFFTAG_IMAGEDESCRIPTION, 
		     "GMAN Generated TIFF Image.\n"
		     "Copyright (c) 2002 John Cairns <john@2ad.com>\n"
		     "Licenced under the terms of the GNU Lesser Public License.\n");
   
	tsize_t linebytes = samplesperpixel * xres;   // length in memory of one row of pixel in the image. 
	unsigned char *buf = NULL;        // buffer used to store the row of pixel information for writing to file
	//    Allocating memory to store the pixels of current row
	if (TIFFScanlineSize(file)==linebytes)
	    buf =(unsigned char *)_TIFFmalloc(linebytes);
	else
	    buf = (unsigned char *)_TIFFmalloc(TIFFScanlineSize(file));
   
	// We set the strip size of the file to be size of one row of pixels
	TIFFSetField(file, TIFFTAG_ROWSPERSTRIP, TIFFDefaultStripSize(file, xres*samplesperpixel));
   
	// copy frameBuffer to jpeg sample array
	for(int y=0; y<yres; y++) {
	    int colOff=0, rowOff=y;
	    for(int x=0; x<xres; x++) {
		GMANColorRGB color;
		color = getPixel(x,y);
       
		// color correct it
		gammaCorrect.correct(color);

		if(quantizer)
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
	    if (TIFFWriteScanline(file, buf, rowOff, 0) < 0) {
		// FIXME
		// throw an error here
		break;
	    }
	}
   
	// Finally we close the output file, and destroy the buffer 
	(void) TIFFClose(file); 
	if (buf)
	    _TIFFfree(buf);
   
	// now isn't that just easy.
#endif
}

// get/set the TIFF compression type
RtVoid GMANOutputTIFF::setCompression(Compression c) {
    compression=c;
}
 
GMANOutputTIFF::Compression GMANOutputTIFF::getCompression(void) const {
    return compression;
}
