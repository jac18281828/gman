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
#include <cstdio>

/* Local Headers */
#include "ri.h"      /* RenderMan Interface */
#include "gmanoutput.h" /* Super class */
#include "gmanoutputpnm.h" /* Declaration Header */
#include "gmancolor.h"
#include "gmanerror.h"

/*
 * RenderMan API GMANOutputPNM
 *
 */

// default constructor
GMANOutputPNM::GMANOutputPNM(const char *path, int width, int height) 
  : GMANOutput(path, width, height) { };


// default destructor 
GMANOutputPNM::~GMANOutputPNM() { };

// Writes a binary P6 portable pixmap directly. This driver used to depend on
// netpbm and its whole body was compiled out when libpnm was absent, which it
// always was, so PNM output never produced a file.
RtVoid GMANOutputPNM::save(GMANOutput::DisplayMode /*mode*/,
			   RtFloat gain, 
			   RtFloat gamma) {

  gammaCorrect.setExposure(gain, gamma);

  FILE *ppmFile = std::fopen(outputName.c_str(), "wb");
  if(!ppmFile) {
    std::string errorMsg("Unable to open output file: ");
    errorMsg.append(outputName);
    throw(GMANError(RIE_SYSTEM, RIE_SEVERE, errorMsg.c_str()));
  }

  std::fprintf(ppmFile, "P6\n%d %d\n255\n", xres, yres);

  for(int row=0; row<yres; row++) {
    for(int col=0; col<xres; col++) {
      GMANColorRGB color;
      color = getPixel(col, row);

      gammaCorrect.correct(color);

      if(quantizer) 
	quantizer->doColor(color);

      const unsigned char rgb[3] = {
	static_cast<unsigned char>(color.getRed()),
	static_cast<unsigned char>(color.getGreen()),
	static_cast<unsigned char>(color.getBlue())
      };
      std::fwrite(rgb, 1, sizeof(rgb), ppmFile);
    }
  }

  std::fclose(ppmFile);
}
