/* This is part of the GNU GMAN Library, a FREE implementation of the
 * RenderMan Interface Specification.
 *
 * Copyright (c) 2002, 2001, 2000, 1999  John Cairns 
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

/* System Headers */
#ifdef WIN32
#include <windows.h>
#endif

#ifdef HAVE_LIBPNG
extern "C" {
#include <png.h>
}
#endif

/* Local Headers */
#include "ri.h"      /* RenderMan Interface */
#include "gmanoutput.h" /* Super class */
#include "gmanerror.h"
#include "gmanoutputpng.h" /* Declaration Header */
#include "gmandefaults.h"

/*
 * RenderMan API GMANOutputPNG
 *
 */

// default constructor
GMANOutputPNG::GMANOutputPNG(const char *path, int width, int height) : 
    GMANOutput(path, width, height, DefaultBGColor) { 
};


// default destructor 
GMANOutputPNG::~GMANOutputPNG() { };

RtVoid GMANOutputPNG::save(GMANOutput::DisplayMode mode, 
			   RtFloat gain, 
			   RtFloat gamma) {
#ifdef HAVE_LIBPNG
    gammaCorrect.setExposure(gain, gamma);

    // write a PNG file to 'fileName'
  
    // open jpeg output file for writing
    FILE *pngFile = fopen(outputName.c_str(), "w");
    if(pngFile == NULL) {
	string errorMsg("Unable to open output file: ");
	errorMsg.append(outputName);
	throw(GMANError(RIE_SYSTEM, RIE_SEVERE, errorMsg.c_str()));
    }
	
    png_structp png_ptr;
    png_infop   info_ptr;

    /* initialize png */
    png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);

    if(png_ptr == NULL) {
	// very bad
	fclose(pngFile);
	throw(GMANError(RIE_SYSTEM, 
			RIE_SEVERE, 
			"PNG initialization failure."));
    }
    
    // allocate the image imformation data 
    info_ptr = png_create_info_struct(png_ptr);
    if(info_ptr == NULL) {
	// ouch!
	fclose(pngFile);
	png_destroy_write_struct(&png_ptr, (png_infopp)NULL);
	throw(GMANError(RIE_SYSTEM, 
			RIE_SEVERE, 
			"PNG initialization failure."));
    }
    
    // set the error handler
#ifdef PNG_SETJMP_SUPPORTED
    if(setjmp(png_ptr->jmpbuf)) {
	// jump here on error
	fclose(pngFile);
	png_destroy_write_struct(&png_ptr, (png_infopp)NULL);
	throw(GMANError(RIE_SYSTEM, 
			RIE_SEVERE, 
			"Internal PNG library error."));
    }
#endif
    
    /* setup C stream */
    png_init_io(png_ptr, pngFile);
    
    // set compression level
    png_set_compression_level(png_ptr, Z_BEST_COMPRESSION);
    
    //png_set_invert_mono(png_ptr); // reverse video black->white
    
    /* setup image information... */

    // for now we only support RGBA
    //
    // need support for other display modes
    png_set_IHDR(png_ptr, info_ptr, xres, 
		 yres, 8, PNG_COLOR_TYPE_RGB_ALPHA,
		 PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, 
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
    text_ptr[0].key = "Title";
    text_ptr[0].text = "GMAN Generated Image";
    text_ptr[0].compression = PNG_TEXT_COMPRESSION_NONE;
    text_ptr[1].key = "Copyright";
    text_ptr[1].text = "Copyright (c) 2002 John Cairns";
    text_ptr[1].compression = PNG_TEXT_COMPRESSION_NONE;
    text_ptr[2].key = "Author";
    text_ptr[2].text = "John Cairns <john@2ad.com> ";
    text_ptr[2].compression = PNG_TEXT_COMPRESSION_NONE;
    png_set_text(png_ptr, info_ptr, text_ptr, 2);
    png_write_info(png_ptr, info_ptr);

    // make sure < 8-bit images are packed into pixels as much as possible
    png_set_packing(png_ptr);

#if BIG_ENDIAN_HOST == 1
    /* Get rid of filler (OR ALPHA) bytes, pack XRGB/RGBX/ARGB/RGBA into
     * RGB (4 channels -> 3 channels). The second parameter is not used.
     */
    if (mode != RGBA) {
	// png_set_filler(png_ptr, 0, PNG_FILLER_AFTER);
    }

#else
    /* Get rid of filler (OR ALPHA) bytes, pack XRGB/RGBX/ARGB/RGBA into
     * RGB (4 channels -> 3 channels). The second parameter is not used.
     */
    if (mode != RGBA) {
	// png_set_filler(png_ptr, 0, PNG_FILLER_BEFORE);
    }
    
    // Swap the location of alpha
    png_set_swap_alpha(png_ptr);

#endif

    png_read_update_info(png_ptr, info_ptr);
    png_uint_32 rowbytes = png_get_rowbytes(png_ptr, info_ptr);
    
    png_byte *image_data = new png_byte[rowbytes*xres];
    if(image_data) {
	png_bytep *row_pointers = new png_bytep[yres];
	if(row_pointers) {
    
	    /* set the row_pointers to point at the correct offset */
	    png_uint_32 i;
	    for(i = 0; i < (png_uint_32)yres; ++i)
		row_pointers[i] = image_data + i*rowbytes;
	    
	    // set x,y pixel color values
	    
	    // memory is contiguous by x not by y so order all x operations to
	    // occur before y operations to take advantage of CPU cache
	    int rowoffset = 0;
	    

	    for(int y=0; y<yres; y++) {
      
		// invert order data is written
		// so viewer sees map upright
		unsigned char *src = image_data + rowoffset;
		
		// avoid a multiply
		rowoffset += rowbytes;

		int colOff=0;
      
		for(int x=0; x<xres; x++) {
	    
		    GMANColorRGB color;
		    color = getPixel(x, y);
       
		    // color correct it
		    gammaCorrect.correct(color);
	
		    // mask off appropriate bits for image generation
		    if(quantizer) 
			quantizer->doColor(color);

		    src[colOff++] = color.getRed();
		    src[colOff++] = color.getGreen();
		    src[colOff++] = color.getBlue();
		    
		    // FIXME FIXME FIXME
		    // FIX Alpha support
       
		    src[colOff++] = 255; 
		    break;
		}
	    }
	    // Write out entire image
	    png_write_image(png_ptr, row_pointers);
	    delete []row_pointers;
	}
	delete []image_data;
    }
    
    png_write_end(png_ptr, info_ptr);

    // clean up write struct
    png_destroy_write_struct(&png_ptr, (png_infopp)NULL);
    fclose(pngFile); 

#endif
}
