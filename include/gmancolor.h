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
 

#ifndef __GMAN_GMANCOLOR_H
#define __GMAN_GMANCOLOR_H 1


/* Headers */

// STL
#include <list>
#include <map>
#include <stack>
#include <string>

// the renderman interface
#include "ri.h"
// the universal super class declaration
#include "universalsuperclass.h"
// standard types
#include "gmantypes.h"
// math helpers
#include "gmanmath.h"

/*
 * RenderMan API GMANColor
 *
 * A simple color object...The atomic color object
 * used in this rendering system.
 *
 */

template <class SampleType>
class GMANDLL GMANColorBase : public UniversalSuperClass {
public:
  typedef	SampleType	ColorSampleType;
protected:

  SampleType		r; // red
  SampleType		g; // green
  SampleType		b; // blue
  
public:
  GMANColorBase() : UniversalSuperClass() { }; // default constructor

  // set each of the color values on construction
  GMANColorBase(SampleType rval, 
		SampleType gval,
		SampleType bval) : UniversalSuperClass() {
    
    r = rval;
    g = gval;
    b = bval;
    
  };

  GMANColorBase(SampleType sval) : UniversalSuperClass() {
    r = g = b = sval;
  };

  // default destructor 
  ~GMANColorBase() { };

  // copy operator
  GMANColorBase &operator = (const GMANColorBase &color) {
    r = color.r;
    g = color.g;
    b = color.b;

    return *this;
  };

  // copy constructor
  GMANColorBase(const GMANColorBase &color) {
    *this = color;
  }

  // less comparison for use by MSVC templates
  bool operator<(const GMANColorBase &c) const {
	  return((r < c.r) &&
		     (g < c.g) &&
			 (b < c.b));
  }


  // Red values
  inline SampleType getRed(RtVoid) const { return r; };
  inline RtVoid setRed(SampleType rval) { r = rval; };


  // Green values
  inline SampleType getGreen(RtVoid) const { return g; };
  inline RtVoid setGreen(SampleType gval) { g = gval; };


  // Blue values
  inline SampleType getBlue(RtVoid) const { return b; };
  inline RtVoid setBlue(SampleType bval) { b = bval; };

  // add a color value
  GMANColorBase &operator +=(const GMANColorBase &c) {
    r += c.r;
    g += c.g;
    b += c.b;
    return *this;
  };

  // subtract a color value
  GMANColorBase &operator -=(const GMANColorBase &c) {
    r -= c.r;
    g -= c.g;
    b -= c.b;
    return *this;
  };

  // divide the color by value
  GMANColorBase &operator /=(SampleType s) {
    r /= s;
    g /= s;
    b /= s;

    return *this;
  };

  // get max color sample
  SampleType maxSampleValue(RtVoid) {
    SampleType maxval;
    
    maxval = GMANMAX(r,g);
    
    maxval = GMANMAX(maxval, b);
    
    return maxval;
  };

  // scale the color by a value
  RtVoid scale(SampleType s) {
    r *= s;
    g *= s;
    b *= s;
  }

};

// combine two colors
template <class ColorObj, class AlphaObj>
struct GMANDLL GMANCombineBase {
  ColorObj operator()(const ColorObj &c1, 
		      const ColorObj &c2, 
		      const AlphaObj &a) 
  {
    ColorObj	res(c1.getRed() + (c2.getRed() - c1.getRed()) * 
		    a.getRed(),
		    c1.getGreen() + (c2.getGreen() - c1.getGreen()) * 
		    a.getGreen(),
		    c1.getBlue() + (c2.getBlue() - c1.getBlue()) * 
		    a.getBlue());
    
    return res;
  };
};

// default color type
class GMANDLL GMANColor : public GMANColorBase<GMANColorSample> {
public:
  GMANColor() : GMANColorBase<GMANColorSample>() { }; // default constructor

  // set each of the color values on construction
  GMANColor(GMANColorSample rval, 
	    GMANColorSample gval,
	    GMANColorSample bval) : 
    GMANColorBase<GMANColorSample>(rval, gval, bval) {
    
  };

  // set each color value with one value
  GMANColor(GMANColorSample sval) :
    GMANColorBase<GMANColorSample>(sval) { };

  GMANColor(GMANByte rval, GMANByte gval, GMANByte bval)  :
    GMANColorBase<GMANColorSample>((GMANColorSample)rval/GMAN_BYTEMAX, 
				   (GMANColorSample)gval/GMAN_BYTEMAX, 
				   (GMANColorSample)bval/GMAN_BYTEMAX)
  {
    
  }

};

// default alpha type
class GMANDLL GMANAlpha : public GMANColorBase<GMANColorSample> {
public:
  GMANAlpha() : GMANColorBase<GMANColorSample>() { }; // default constructor

  // set each of the color values on construction
  GMANAlpha(GMANColorSample rval, 
	    GMANColorSample gval,
	    GMANColorSample bval) :
    GMANColorBase<GMANColorSample>(rval, gval, bval) {
    
  };

  // set each color value with one value
  GMANAlpha(GMANColorSample sval) :
    GMANColorBase<GMANColorSample>(sval) { };

  GMANAlpha(GMANByte rval, GMANByte gval, GMANByte bval)  :
    GMANColorBase<GMANColorSample>((GMANColorSample)rval/GMAN_BYTEMAX, 
				   (GMANColorSample)gval/GMAN_BYTEMAX, 
				   (GMANColorSample)bval/GMAN_BYTEMAX)
  {
    
  }

};


typedef GMANCombineBase<GMANColor, GMANAlpha>  GMANCombine;
typedef GMANCombineBase<GMANAlpha, GMANAlpha>  GMANAlphaCombine;

// 24 bit rgb color object
class GMANDLL GMANColorRGB : public GMANColorBase<GMANByte> {
private:
  /*
   * Weighting factors representing the sensitivity of the
   * human eye to a given color.
   *
   */
	static const RtFloat		redWeight;
	static const RtFloat		blueWeight;
	static const RtFloat		greenWeight;


  
public:
  GMANColorRGB &operator=(const GMANColor &c) {
    r = (GMANByte) (c.getRed() * (RtFloat)GMAN_BYTEMAX);
    g = (GMANByte) (c.getGreen() * (RtFloat)GMAN_BYTEMAX);
    b = (GMANByte) (c.getBlue() * (RtFloat)GMAN_BYTEMAX);
    
    return *this;
  };

  // set color to 24bit grayscale
  RtVoid setMono(const GMANColor &c) {
    r = g = b = (GMANByte)(c.getRed() * redWeight +
			   c.getGreen() * greenWeight +
			   c.getBlue() * blueWeight);
  };

};


/*
 * January 2001 -- LJL
 * Color space conversions. 
 */

GMANDLL RtVoid GMANConvertRGBtoHSV (RtFloat *c);
GMANDLL RtVoid GMANConvertHSVtoRGB (RtFloat *c);

GMANDLL RtVoid GMANConvertRGBtoHSL (RtFloat *c);
GMANDLL RtVoid GMANConvertHSLtoRGB (RtFloat *c);

GMANDLL RtVoid GMANConvertRGBtoXYZ (RtFloat *c);
GMANDLL RtVoid GMANConvertXYZtoRGB (RtFloat *c);

GMANDLL RtVoid GMANConvertRGBtoXYY (RtFloat *c);
GMANDLL RtVoid GMANConvertXYYtoRGB (RtFloat *c);

GMANDLL RtVoid GMANConvertRGBtoYIQ (RtFloat *c);
GMANDLL RtVoid GMANConvertYIQtoRGB (RtFloat *c);
GMANDLL RtVoid GMANConvertRGBtoYUV (RtFloat *c);
GMANDLL RtVoid GMANConvertYUVtoRGB (RtFloat *c);

#endif

