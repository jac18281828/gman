/*----------------------------------------------------------
  Copyright (C) Lionel Joseph Lacour 2000, 2001
  2000/08/06 First release
  ----------------------------------------------------------
  RenderMan graphic state attributes.
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

#ifndef __GMANATTRIBUTES_H
#define __GMANATTRIBUTES_H 1

#include <string>
#include "ri.h"
#include "universalsuperclass.h"

#include "gmanshader.h"
#include "gmanlightsourceshader.h"
#include "gmandisplacementshader.h"
#include "gmansurfaceshader.h"
#include "gmanvolumeshader.h"

#include "gmanloadableshader.h"

#include "gmantrimcurve.h"
#include "gmanbasis.h"
#include "gmanbbox.h"
#include "gmanlightsourcemgr.h"

class GMANDLL GMANRenderer;

struct GMANDLL GMANTextureCoordinates {
  RtFloat s1, t1, s2, t2;
  RtFloat s3, t3, s4, t4;
};

struct GMANDLL GMANDetailRange {
  RtFloat minVisible;
  RtFloat lowerTransition;
  RtFloat upperTransition;
  RtFloat maxVisible;
};

struct GMANDLL GMANGeometricApproximation {
  RtToken type;
  RtFloat value;
};

class GMANDLL GMANAttributes : public UniversalSuperClass
{
private:
  /* SHADING ATTRIBUTES */
  RtColor                color;
  RtColor                opacity;
  
  GMANTextureCoordinates     textureCoordinates;
  GMANLightList          lightList;
  
  GMANLoadableShader     *areaLightModule;
  GMANLightSourceShader  *areaLight;

  GMANLoadableShader     *surfaceModule;
  GMANSurfaceShader      *surface;

  GMANLoadableShader     *atmosphereModule;
  GMANVolumeShader       *atmosphere;

  GMANLoadableShader     *interiorModule;
  GMANVolumeShader       *interior;
  
  GMANLoadableShader     *exteriorModule;
  GMANVolumeShader       *exterior;
  
  GMANLoadableShader     *displacementModule;
  GMANDisplacementShader *displacement;
  
  RtFloat                shadingRate;
  RtToken                shadingInterpolation;
  bool                   matte;

  /* GEOMETRY ATTRIBUTES */
  GMANBBox               bound;
  GMANBBox               detail;
  GMANDetailRange            detailRange;
  GMANGeometricApproximation geometricApproximation;
  RtToken                orientation;
  RtInt                  sides;
  GMANTrimCurve          trimCurves;
  GMANBasis              uvBasis;
  GMANBasis              objectBasis;
  bool                   objectFlag;


public:
  GMANAttributes() throw(GMANError);
  ~GMANAttributes();


  /* SHADING ATTRIBUTES */
  RtVoid setColor (RtColor c);
  RtVoid setOpacity (RtColor o);

  RtVoid setTextureCoordinates (RtFloat s1, RtFloat t1, RtFloat s2, RtFloat t2,
				RtFloat s3, RtFloat t3, RtFloat s4, RtFloat t4);
  GMANTextureCoordinates const getTextureCoordinates () const {return textureCoordinates;};

  // RiLightSource
  // RtLightHandle setAreaLight (const string & name, GMANParameterList &pl);
  RtVoid setIlluminate (RtLightHandle lh, RtBoolean onoff);

  /* SHADERS */
  RtVoid setSurface (const string & name, GMANParameterList &pl,
		     GMANRenderer &rd) throw(GMANError);
  const GMANSurfaceShader *getSurface(RtFloat time) const {return surface;};

  RtVoid setAtmosphere (const string & name, GMANParameterList &pl,
			GMANRenderer &rd) throw(GMANError);
  const GMANVolumeShader *getAtmosphere(RtFloat time) const {return atmosphere;};

  RtVoid setInterior (const string & name, GMANParameterList &pl,
		      GMANRenderer &rd) throw(GMANError);
  const GMANVolumeShader  *getInterior(RtFloat time) const {return interior;};

  RtVoid setExterior (const string & name, GMANParameterList &pl,
		      GMANRenderer &rd) throw(GMANError);
  const GMANVolumeShader *getExterior(RtFloat time) const {return exterior;};

  RtVoid setDisplacement (const string & name, GMANParameterList &pl,
			  GMANRenderer &rd) throw(GMANError);
  const GMANDisplacementShader *getDisplacement(RtFloat time) const {return displacement;};



  RtVoid setShadingRate (RtFloat sz);
  RtFloat getShadingRate () const {return shadingRate;};

  RtVoid setShadingInterpolation (RtToken si);
  RtToken const getShadingInterpolation () const {return shadingInterpolation;};

  RtVoid setMatte (RtBoolean on);
  bool getMatte () const {return matte;};

  /* GEOMETRY ATTRIBUTES */
  RtVoid setBound (RtBound b);
  GMANBBox const getBound (RtFloat time) const {return bound;};

  RtVoid setDetail (RtBound d);
  GMANBBox const getDetail (RtFloat time) const {return bound;};

  RtVoid setDetailRange (RtFloat minv, RtFloat lt,  RtFloat up, RtFloat maxv);
  GMANDetailRange const getDetailRange () const {return detailRange;};

  RtVoid setGeometricApproximation (RtToken ga, RtFloat v);
  GMANGeometricApproximation const getGeometricApproximation () const {return geometricApproximation;};

  RtVoid setOrientation (RtToken o);
  RtToken const getOrientation () const {return orientation;};

  RtVoid toggleOrientation ();

  RtVoid setSides (RtInt n);
  RtInt getSides () const {return sides;};

  RtVoid setTrimCurves (GMANTrimCurve const &tc);
  GMANTrimCurve const getTrimCurves () const {return trimCurves;};

  RtVoid setUVBasis (RtBasis u, 
		     RtInt ustep, 
		     RtBasis v, 
		     RtInt vstep);

  RtVoid setObjectBasis (GMANBasis *b);
  RtVoid clearObjectFlag ();
  GMANBasis const getUVBasis () const {
    if (objectFlag==true)
      return objectBasis;
    else
      return uvBasis;
  };

};

#endif



