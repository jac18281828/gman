/* This is part of the GNU GMAN Library, a FREE implementation of the
 * RenderMan Interface Specification.
 *
 * Copyright (c) 2001, 2000, 1999 John Cairns 
 *
 * Author: John Cairns <john@2ad.com>
 */

/* Added display and additional options LJL (2000/07/28) */

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
 

#ifndef __GMAN_OPTIONS_H
#define __GMAN_OPTIONS_H 1


/* Headers */
#include <string>
#include <math.h>

#include "ri.h"
#include "gmanparameterlist.h"
#include "gmancolorsamples.h"
#include "gmandefaults.h"
#include "gmanoutput.h"

// the universal super class declaration
#include "universalsuperclass.h"
#include "gmancolor.h" // color decl
#include "gmanmath.h"

#include "gmanshader.h"
#include "gmanimagershader.h"
#include "gmanloadableshader.h"

class GMANRenderer;

/*
 * RenderMan API camera, display and additional options.
 *
 */

class GMANDLL  GMANOptions : public UniversalSuperClass {
  public:
    // public types
    
    struct FormatStruct {
	RtInt xres;
	RtInt yres;
	RtFloat pixelAspectRatio;
    };
    
    struct ScreenWindowStruct {
	RtFloat left;
	RtFloat right;
	RtFloat bottom;
	RtFloat top;
    };
    
    struct CropWindowStruct {
	RtFloat xmin;
	RtFloat xmax;
	RtFloat ymin;
	RtFloat ymax;
    };
    
    struct ProjectionStruct {
	string name;
	GMANParameterList pl;
    };
    
    struct ClippingStruct {
	RtFloat nearDist;
	RtFloat farDist;
    };
    
    struct DepthOfFieldStruct {
	RtFloat fstop;
	RtFloat focallength;
	RtFloat focaldistance;
    };
    
    struct ShutterStruct {
	RtFloat min;
	RtFloat max;
    };
    
    struct PixelSamplesStruct {
	RtFloat xsamples;
	RtFloat ysamples;
    };
    
    struct PixelFilterStruct {
	RtFilterFunc filterfunc;
	RtFloat xwidth;
	RtFloat ywidth;
    };
    
    struct ExposureStruct {
	RtFloat gain;
	RtFloat gamma;
    };
    
    struct QuantizeStruct {
	RtInt one;
	RtInt min;
	RtInt max;
	RtFloat ditheramplitude;
    };
    
    struct DisplayStruct {
	string name;
	string type;
	string mode;
	GMANParameterList pl;
    };
    
    struct HiderStruct {
	string type;
	GMANParameterList pl;
    };
    
    struct RasterInfo {
	RtInt xres;
	RtInt yres;
	RtInt rxmin, rxmax;
	RtInt rymin, rymax;
    };
    
    struct OutputDefaults {
	RtInt xres;
	RtInt yres;
	RtFloat par;
    };
    
    
  private:
    /******* CAMERA OPTIONS *******/
    FormatStruct       format;
    RtFloat            frameAspectRatio;
    ScreenWindowStruct screenWindow;
    CropWindowStruct   cropWindow;
    ProjectionStruct   projection;
    ClippingStruct     clipping;
    DepthOfFieldStruct depthOfField;
    ShutterStruct      shutter;
    
    /******* DISPLAY OPTIONS *******/
    RtFloat            pixelVariance;
    PixelSamplesStruct pixelSamples;
    PixelFilterStruct  pixelFilter;
    ExposureStruct     exposure;
    GMANLoadableShader *imagerModule;
    GMANImagerShader   *imager;
    QuantizeStruct     colorQuantize;
    QuantizeStruct     depthQuantize;
    DisplayStruct      display;
    
    /******* ADDITIONAL OPTIONS *******/
    HiderStruct        hider;
    GMANColorSamples   colorSamples;
    RtFloat            relativeDetail;
    
    
    // the background color
    GMANColor background;
    bool screenWindowSet;

  /* default static data */
  static OutputDefaults  outputDefaults;
  
  public:
    GMANOptions (); // default constructor
    ~GMANOptions (); // default destructor
    
    /******* CAMERA OPTIONS *******/
    RtVoid setFormat (RtInt xres, RtInt yres, RtFloat aspect);
    const FormatStruct getFormat (RtVoid) const;

    RtVoid setFrameAspectRatio (RtFloat ar);
    RtFloat getFrameAspectRatio (RtVoid) const;
    
    RtVoid setScreenWindow (RtFloat left, RtFloat right, 
			    RtFloat bottom, RtFloat top);

    const ScreenWindowStruct getScreenWindow (RtVoid) const;
    const RasterInfo getRasterInfo(RtVoid) const;

    RtVoid  setCropWindow(RtFloat xmin, RtFloat xmax, 
			  RtFloat ymin, RtFloat ymax);

    const CropWindowStruct &getCropWindow (RtVoid) const {return cropWindow;};

    RtVoid setProjection (string n, GMANParameterList &p);
    
    const ProjectionStruct &getProjection (RtFloat time=0.0) const {
	return projection;
    };

    RtVoid setClipping (RtFloat nearDist, RtFloat farDist);

    const ClippingStruct &getClipping (RtVoid) const {return clipping;};

    RtVoid setDepthOfField (RtFloat fstop,  RtFloat flength, 
			    RtFloat fdistance);
    
    DepthOfFieldStruct const getDepthOfField () const {return depthOfField;};

    RtVoid setShutter (RtFloat mn, RtFloat mx);

    const ShutterStruct &getShutter (RtVoid) const {return shutter;};


    /******* DISPLAY OPTIONS *******/
    RtVoid setPixelVariance (RtFloat var);
    RtFloat getPixelVariance (RtVoid) const {return pixelVariance;};

    RtVoid setPixelSamples(RtFloat xsamples, RtFloat ysamples);
    const PixelSamplesStruct &getPixelSamples (RtVoid) const {
	return pixelSamples;
    };

    RtVoid setPixelFilter (RtFilterFunc ff, RtFloat xw, RtFloat yw);
    const PixelFilterStruct &getPixelFilter (RtVoid) const {
	return pixelFilter;
    };

    RtVoid setExposure (RtFloat gn, RtFloat gmm);
    const ExposureStruct &getExposure (RtVoid) const {return exposure;};

    RtVoid setImager (string nm, GMANParameterList &p, GMANRenderer &rd);
    const GMANImagerShader *getImager(RtVoid) const {return imager;};

    RtVoid setColorQuantize (RtInt one, RtInt min, RtInt max, RtFloat da);
    
    const QuantizeStruct &getColorQuantize (RtVoid) const {
	return colorQuantize;
    };
    
    RtVoid setDepthQuantize (RtInt one, RtInt min, RtInt max, RtFloat da);
    
    const QuantizeStruct &getDepthQuantize (RtVoid) const {
	return depthQuantize;
    };

    RtVoid setDisplay (string nm, string tp, string md, GMANParameterList &p);
    
    const DisplayStruct &getDisplay (RtVoid) const {return display;};


    /******* ADDITIONAL OPTIONS *******/
    RtVoid setHider (string nm, GMANParameterList &p);
    const HiderStruct &getHider (RtVoid) const {return hider;};
    
    RtVoid setColorSamples (RtInt nb, RtFloat *nr, RtFloat *rn);
    const GMANColorSamples &getColorSamples (RtVoid) const {
	return colorSamples;
    };

    RtVoid setRelativeDetail (RtFloat rd);
    RtFloat getRelativeDetail (RtVoid) const {return relativeDetail;};


    RtVoid setBackground(const GMANColor &bgcolor) {
	background = bgcolor;
    }

    const GMANColor &getBackground(RtVoid) const {
	return background;
    }  

    const OutputDefaults &getOutputDefaults(RtVoid) const;

};


#endif

