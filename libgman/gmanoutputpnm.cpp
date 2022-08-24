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

/* system headers */

/* Util headers */
#ifdef HAVE_LIBPNM
extern "C" {
#include <ppm.h>
}
#endif
/* Local Headers */
#include "ri.h"      /* RenderMan Interface */
#include "gmanoutput.h" /* Super class */
#include "gmanoutputpnm.h" /* Declaration Header */
#include "gmancolor.h"

/*
 * RenderMan API GMANOutputPNM
 *
 */

// default constructor
GMANOutputPNM::GMANOutputPNM(const char *path, int width, int height) 
  : GMANOutput(path, width, height) { };


// default destructor 
GMANOutputPNM::~GMANOutputPNM() { };

RtVoid GMANOutputPNM::save(GMANOutput::DisplayMode mode, 
			   RtFloat gain, 
			   RtFloat gamma) {
#ifdef HAVE_LIBPNM

  debug("Have PNM");

  pixel* pixrow;
  pixel* pp;

  int argc = 0;
  char *argv[1] = { "gman" };

  gammaCorrect.setExposure(gain, gamma);

  ppm_init(&argc, argv);
  FILE *ppmFile = fopen(outputName.c_str(), "w");
  if(ppmFile) {

    ppm_writeppminit(ppmFile, xres, yres, PPM_MAXMAXVAL, 0);
    pixrow = ppm_allocrow(xres);


    for(int row=0; row<yres; row++) {
      int col;
      for(col=0, pp=pixrow; col<xres; col++, pp++) {
	GMANColorRGB color;
	color = getPixel(col, row);

	gammaCorrect.correct(color);
	
	//
	// get pixels in inverted order with respect
	// to y, i.e., flip the image on output hoizontally,
	// positive y values go UP


	// reduce if required
	if(quantizer) 
	    quantizer->doColor(color);
	
	// default, (no reduction) is 24bit
	
	// write r, g, and b
	PPM_ASSIGN(*pp, color.getRed(), color.getGreen(), color.getBlue());
      }
      ppm_writeppmrow(ppmFile, pixrow, xres, PPM_MAXMAXVAL, 0);

    }
    pm_close(ppmFile);
  }
#endif
}

