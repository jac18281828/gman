/*--------------------------------------------------------------------
  Copyright (C) Lionel Joseph Lacour 2000, 2001
  --------------------------------------------------------------------
  2000/10/19  First release
  2000/11     Moved Stacks from gmanrenderman here.
	      Changed allowed(), and added partial support for motion.
  --------------------------------------------------------------------
  This class store everything related with the graphic state.
  It manage motion, and check that Ri commands appear in the
  right begin-end block
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

#ifndef __GMANGRAPHICSTATE_H
#define __GMANGRAPHICSTATE_H 1

#include <iostream>
#include <stack>
#include <vector>
#if HAVE_STD_NAMESPACE
using std::stack;
using std::vector;
#endif

#include "ri.h"

#include "gmanerror.h"


#include "universalsuperclass.h"

#include "gmanoptions.h"
#include "gmanattributes.h"
#include "gmantransform.h"

class GMANDLL  GMANGraphicState : public UniversalSuperClass
{
public:
  // public types
  typedef stack<GMANOptions>	    OptionsStack;
  typedef stack<GMANAttributes>     AttributesStack;
  typedef stack<GMANTransform>	    TransformStack;


  /* Begin-End Blocks */

  typedef enum { B=0x1, /* BEGIN */
		 F=0x2, /* FRAME */
		 W=0x4, /* WORLD */
		 A=0x8, /* ATTRIBUTE */
		 T=0x10, /* TRANSFORM */
		 O=0x20, /* OBJECT */
		 S=0x40, /* SOLID */
		 M=0x80  /* MOTION */
  } CurrentState;

  struct CommandIdentity
  {
    int allowedBlocks;

    bool isAttribute;
    bool isTransform;
    bool isPrimitive;
  };

protected:
  // all of the command identities recognized by 
  // the state machine

  static CommandIdentity cmdFormat;
  static CommandIdentity cmdFrameAspectRatio;
  static CommandIdentity cmdScreenWindow;
  static CommandIdentity cmdCropWindow;
  static CommandIdentity cmdProjection;
  static CommandIdentity cmdClipping;
  static CommandIdentity cmdDepthOfField ;
  static CommandIdentity cmdShutter;
  static CommandIdentity cmdPixelVariance;
  static CommandIdentity cmdPixelSamples;
  static CommandIdentity cmdPixelFilter;
  static CommandIdentity cmdExposure;
  static CommandIdentity cmdImager;
  static CommandIdentity cmdQuantize;
  static CommandIdentity cmdDisplay;
  static CommandIdentity cmdHider;
  static CommandIdentity cmdColorSamples;
  static CommandIdentity cmdRelativeDetail;
  static CommandIdentity cmdOption;
  static CommandIdentity cmdColor;
  static CommandIdentity cmdOpacity;
  static CommandIdentity cmdTextureCoordinates;
  static CommandIdentity cmdLightSource;
  static CommandIdentity cmdAreaLightSource;
  static CommandIdentity cmdIlluminate;
  static CommandIdentity cmdSurface;
  static CommandIdentity cmdAtmosphere;
  static CommandIdentity cmdInterior;
  static CommandIdentity cmdExterior;
  static CommandIdentity cmdShadingRate;
  static CommandIdentity cmdShadingInterpolation;
  static CommandIdentity cmdMatte;
  static CommandIdentity cmdBound;
  static CommandIdentity cmdDetail;
  static CommandIdentity cmdDetailRange;
  static CommandIdentity cmdGeometricApproximation;
  static CommandIdentity cmdOrientation;
  static CommandIdentity cmdReverseOrientation;
  static CommandIdentity cmdSides;
  static CommandIdentity cmdIdentity;
  static CommandIdentity cmdTransform;
  static CommandIdentity cmdConcatTransform;
  static CommandIdentity cmdPerspective;
  static CommandIdentity cmdTranslate;
  static CommandIdentity cmdRotate;
  static CommandIdentity cmdScale;
  static CommandIdentity cmdSkew;
  static CommandIdentity cmdDisplacement;
  static CommandIdentity cmdCoordinateSystem;
  static CommandIdentity cmdCoordSysTransform;
  static CommandIdentity cmdTransformPoints;
  static CommandIdentity cmdAttribute;
  static CommandIdentity cmdPolygon;
  static CommandIdentity cmdGeneralPolygon;
  static CommandIdentity cmdPointsPolygon;
  static CommandIdentity cmdPointsGeneralPolygons;
  static CommandIdentity cmdBasis;
  static CommandIdentity cmdPatch;
  static CommandIdentity cmdPatchMesh;
  static CommandIdentity cmdNuPatch;
  static CommandIdentity cmdTrimCurve;
  static CommandIdentity cmdSphere;
  static CommandIdentity cmdCone;
  static CommandIdentity cmdCylinder;
  static CommandIdentity cmdHyperboloid;
  static CommandIdentity cmdParaboloid;
  static CommandIdentity cmdDisk;
  static CommandIdentity cmdTorus;
  static CommandIdentity cmdBlobby;
  static CommandIdentity cmdPoints;
  static CommandIdentity cmdCurves;
  static CommandIdentity cmdSubdivisionMesh;
  static CommandIdentity cmdProcedural;
  static CommandIdentity cmdGeometry;
  static CommandIdentity cmdObjectInstance;
  static CommandIdentity cmdMakeTexture;
  static CommandIdentity cmdMakeBump;
  static CommandIdentity cmdMakeLatLongEnvironment;
  static CommandIdentity cmdMakeCubeFaceEnvironment;
  static CommandIdentity cmdMakeShadow;

private:
  stack<CurrentState> nest;
    
  CurrentState block;

  RtInt frame, world;
  RtInt attribute, transform;
  RtInt solid, motion, object;


  bool attribsChangedFlag;
  bool transformChangedFlag;

  /* Motion stuff */
  RtInt nbSamples;
  vector<RtFloat> samples;
  RtInt motionIndex;
  GMANMovingMatrix mm;
  
  CommandIdentity *currentMC;
  bool motionError;
    
  OptionsStack    optionsStack;
  AttributesStack attributesStack;
  TransformStack  transformStack;
    
  RtVoid push(CurrentState st);
  RtVoid pop();
  RtVoid overlap(CurrentState mode, CurrentState  current);
  RtVoid error(char *message);
  RtVoid allowed(RtInt mode);
public:
  GMANGraphicState();
    
  GMANOptions &getOptions();
  GMANAttributes &getAttributes();
  const GMANTransform &getTransform() const;
    
  RtVoid enterMode(CurrentState m);
  RtVoid enterMotion(RtInt n, RtFloat *s);
  RtVoid leaveMode(CurrentState m);

  RtVoid allowed(CommandIdentity &cid);

  /* Attributes and transform state */
  bool sameAttribs(RtVoid);
  bool sameTransform(RtVoid);

  /* Motion block */
  RtVoid setTransform(GMANMatrix4 &m);
  RtVoid buildTransform(GMANMatrix4 &m);
};

#endif
