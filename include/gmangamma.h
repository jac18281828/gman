/* This is part of the GNU GMAN Library, a FREE implementation of the
 * RenderMan Interface Specification.
 *
 * Copyright (c) 2001, 2000, 1999 John Cairns 
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
 

#ifndef __GMAN_GAMMA_H
#define __GMAN_GAMMA_H 1

#include <math.h>

#include "ri.h"
#include "gmancolor.h"
#include "gmantypes.h"

// gamma correction class
class GMANDLL  GMANGammaCorrect {
public:

  static const RtFloat DEFAULT_GAMMA;
  static const RtFloat DEFAULT_GAIN;

private:

  static const int G_Domain;
  static const int G_Range;

  // gamma value lookup table
  static GMANByte GammaTable[256];

  RtFloat gamma; // gamma 
  RtFloat gain;    // exposure gain

  RtVoid initTable() {
    int i; // counter
    
    // pre calculate gamma correction lookup table entries
    for(i=0; i<= G_Domain; i++) {
      GammaTable[i] = (GMANByte) ((RtFloat) G_Range *
			      pow((RtFloat)i*gain / (RtFloat) G_Domain, 
				  1.0 / gamma));
      
    }

    // future gamma correction with this object will only
    // require a lookup in gamma table.
  };

public:
  /*
   * Default Constructor with required gamma correction value.
   * 
   * \param g Gamma value used for correction.
   *
   */
  GMANGammaCorrect(RtFloat gainVal = DEFAULT_GAIN, RtFloat gammaVal = DEFAULT_GAMMA) 
  { 
    setExposure(gainVal, gammaVal);
  };

  /*
   * Return the current gamma value used in this object
   *
   * \retval RtFloat Gamma Value.
   */
  RtFloat getGamma() { return gamma; };


  
  /*
   * Correct the color objects gamma values.
   *
   * param color The color object to be corrected.
   *
   */
  RtVoid correct( GMANColorRGB &color ) {
    // lookup each color value in turn, and 
    // set that value here.
    color.setRed( GammaTable[color.getRed()] );
    color.setGreen( GammaTable[color.getGreen()] );
    color.setBlue( GammaTable[color.getBlue()] );
    
  };

  /*
   * Correct the color objects gamma value.
   */
  RtVoid correct( GMANColor &color ) {

    color.setRed (pow(gain * color.getRed(), 1.0 / gamma) );
    color.setGreen (pow(gain * color.getGreen(), 1.0 / gamma) );
    color.setBlue (pow(gain* color.getBlue(), 1.0 / gamma) );

  }

  /*
   * Set the current gamma value used
   * for correction.
   *
   * \param g The gamma value to use for gamma correction.
   */
  RtVoid setExposure( RtFloat gainVal, RtFloat gammaVal ) {
    gain = gainVal;
    gamma = gammaVal;
    initTable();

  };

};


#endif

