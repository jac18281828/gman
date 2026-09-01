/* SPDX-License-Identifier: LGPL-2.1-or-later */

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
*/

#ifndef __GMANATTRIBUTES_H
#define __GMANATTRIBUTES_H 1

#include <memory>
#include <string>
#include "ri.h"
#include "gmanlog.h"
#include "gmancolor.h"

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

class GMAN_EXPORT GMANRenderer;

struct GMAN_EXPORT GMANTextureCoordinates {
  RtFloat s1, t1, s2, t2;
  RtFloat s3, t3, s4, t4;
};

struct GMAN_EXPORT GMANDetailRange {
  RtFloat minVisible;
  RtFloat lowerTransition;
  RtFloat upperTransition;
  RtFloat maxVisible;
};

struct GMAN_EXPORT GMANGeometricApproximation {
  RtToken type;
  RtFloat value;
};

class GMAN_EXPORT GMANAttributes
{
private:
  /* SHADING ATTRIBUTES */
  RtColor                color;
  RtColor                opacity;

  GMANTextureCoordinates     textureCoordinates;
  GMANLightList          lightList;

  // Shared, not deep-copied: a loaded module is immutable once dlopen'd, and
  // AttributeBegin/AttributeEnd (GMANGraphicState::attributesStack.push
  // (attributesStack.top())) copies GMANAttributes on every block entry --
  // deep-copying would re-dlopen on every block even when no shader
  // changes inside it. A shared_ptr also makes the block's own semantics
  // free: reassigning a block-local shared_ptr (RiSurface inside
  // AttributeBegin) never touches the parent's copy, and AttributeEnd
  // discards it along with the rest of the popped GMANAttributes.
  std::shared_ptr<GMANLoadableShader>     areaLightModule;
  [[maybe_unused]] GMANLightSourceShader  *areaLight;

  std::shared_ptr<GMANLoadableShader>     surfaceModule;
  GMANSurfaceShader      *surface;

  std::shared_ptr<GMANLoadableShader>     atmosphereModule;
  GMANVolumeShader       *atmosphere;

  std::shared_ptr<GMANLoadableShader>     interiorModule;
  GMANVolumeShader       *interior;

  std::shared_ptr<GMANLoadableShader>     exteriorModule;
  GMANVolumeShader       *exterior;

  std::shared_ptr<GMANLoadableShader>     displacementModule;
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
  GMANAttributes();
  ~GMANAttributes();


  /* SHADING ATTRIBUTES */
  RtVoid setColor (RtColor c);
  RtVoid setOpacity (RtColor o);
  GMANColor getColor () const { return GMANColor(color[0], color[1], color[2]); };
  GMANColor getOpacity () const { return GMANColor(opacity[0], opacity[1], opacity[2]); };

  RtVoid setTextureCoordinates (RtFloat s1, RtFloat t1, RtFloat s2, RtFloat t2,
				RtFloat s3, RtFloat t3, RtFloat s4, RtFloat t4);
  GMANTextureCoordinates const getTextureCoordinates () const {return textureCoordinates;};

  // RiLightSource
  // RtLightHandle setAreaLight (const string & name, GMANParameterList &pl);
  RtVoid setIlluminate (RtLightHandle lh, RtBoolean onoff);
  const GMANLightList &getLightList () const { return lightList; };

  /* SHADERS */
  RtVoid setSurface (const std::string & name, GMANParameterList &pl,
		     GMANRenderer &rd);
  const GMANSurfaceShader *getSurface(RtFloat /*time*/) const {return surface;};

  RtVoid setAtmosphere (const std::string & name, GMANParameterList &pl,
			GMANRenderer &rd);
  const GMANVolumeShader *getAtmosphere(RtFloat /*time*/) const {return atmosphere;};

  RtVoid setInterior (const std::string & name, GMANParameterList &pl,
		      GMANRenderer &rd);
  const GMANVolumeShader  *getInterior(RtFloat /*time*/) const {return interior;};

  RtVoid setExterior (const std::string & name, GMANParameterList &pl,
		      GMANRenderer &rd);
  const GMANVolumeShader *getExterior(RtFloat /*time*/) const {return exterior;};

  RtVoid setDisplacement (const std::string & name, GMANParameterList &pl,
			  GMANRenderer &rd);
  const GMANDisplacementShader *getDisplacement(RtFloat /*time*/) const {return displacement;};



  RtVoid setShadingRate (RtFloat sz);
  RtFloat getShadingRate () const {return shadingRate;};

  RtVoid setShadingInterpolation (RtToken si);
  RtToken getShadingInterpolation () const {return shadingInterpolation;};

  RtVoid setMatte (RtBoolean on);
  bool getMatte () const {return matte;};

  /* GEOMETRY ATTRIBUTES */
  RtVoid setBound (RtBound b);
  GMANBBox const getBound (RtFloat /*time*/) const {return bound;};

  RtVoid setDetail (RtBound d);
  GMANBBox const getDetail (RtFloat /*time*/) const {return bound;};

  RtVoid setDetailRange (RtFloat minv, RtFloat lt,  RtFloat up, RtFloat maxv);
  GMANDetailRange const getDetailRange () const {return detailRange;};

  RtVoid setGeometricApproximation (RtToken ga, RtFloat v);
  GMANGeometricApproximation const getGeometricApproximation () const {return geometricApproximation;};

  RtVoid setOrientation (RtToken o);
  RtToken getOrientation () const {return orientation;};

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



