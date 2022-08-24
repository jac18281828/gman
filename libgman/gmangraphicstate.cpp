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

#include "gmangraphicstate.h"


// OPTIONS
GMANGraphicState::CommandIdentity 
GMANGraphicState::cmdFormat = { B|F, 0,0,0 };

GMANGraphicState::CommandIdentity 
GMANGraphicState::cmdFrameAspectRatio= { B|F, 0,0,0 };

GMANGraphicState::CommandIdentity 
GMANGraphicState::cmdScreenWindow= { B|F, 0,0,0 };

GMANGraphicState::CommandIdentity
GMANGraphicState::cmdCropWindow= { B|F, 0,0,0 };

GMANGraphicState::CommandIdentity
GMANGraphicState::cmdProjection= { B|F|M, 0,0,0 };

GMANGraphicState::CommandIdentity
GMANGraphicState::cmdClipping= { B|F, 0,0,0 };

GMANGraphicState::CommandIdentity
GMANGraphicState::cmdDepthOfField = { B|F, 0,0,0 };

GMANGraphicState::CommandIdentity
GMANGraphicState::cmdShutter= { B|F, 0,0,0 };

GMANGraphicState::CommandIdentity
GMANGraphicState::cmdPixelVariance= { B|F, 0,0,0 };

GMANGraphicState::CommandIdentity
GMANGraphicState::cmdPixelSamples= { B|F, 0,0,0 };

GMANGraphicState::CommandIdentity
GMANGraphicState::cmdPixelFilter= { B|F, 0,0,0 };

GMANGraphicState::CommandIdentity
GMANGraphicState::cmdExposure= { B|F, 0,0,0 };

GMANGraphicState::CommandIdentity
GMANGraphicState::cmdImager= { B|F, 0,0,0 };

GMANGraphicState::CommandIdentity
GMANGraphicState::cmdQuantize= { B|F, 0,0,0 };

GMANGraphicState::CommandIdentity
GMANGraphicState::cmdDisplay= { B|F, 0,0,0 };

GMANGraphicState::CommandIdentity
GMANGraphicState::cmdHider= { B|F, 0,0,0 };

GMANGraphicState::CommandIdentity
GMANGraphicState::cmdColorSamples= { B|F, 0,0,0 };

GMANGraphicState::CommandIdentity
GMANGraphicState::cmdRelativeDetail= { B|F, 0,0,0 };

GMANGraphicState::CommandIdentity
GMANGraphicState::cmdOption= { 0, 0,0,0 }; // TODO ****

// ATTRIBUTES
GMANGraphicState::CommandIdentity
GMANGraphicState::cmdColor= { B|F|W|A|T|S|M, 1,0,0 };

GMANGraphicState::CommandIdentity
GMANGraphicState::cmdOpacity= { B|F|W|A|T|S|M, 1,0,0 };

GMANGraphicState::CommandIdentity
GMANGraphicState::cmdTextureCoordinates= { B|F|W|A|T|S|M, 1,0,0 };

GMANGraphicState::CommandIdentity
GMANGraphicState::cmdLightSource= { B|F|W|A|T|S|M, 1,0,0 };

GMANGraphicState::CommandIdentity
GMANGraphicState::cmdAreaLightSource= { B|F|W|A|T|S|M, 1,0,0 };

GMANGraphicState::CommandIdentity
GMANGraphicState::cmdIlluminate= { B|F|W|A|T|S|M, 1,0,0 };

GMANGraphicState::CommandIdentity
GMANGraphicState::cmdSurface= { B|F|W|A|T|S|M, 1,0,0 };

GMANGraphicState::CommandIdentity
GMANGraphicState::cmdAtmosphere= { B|F|W|A|T|S|M, 1,0,0 };

GMANGraphicState::CommandIdentity
GMANGraphicState::cmdDisplacement= { B|F|W|A|T|S|M, 1,0,0 };

GMANGraphicState::CommandIdentity
GMANGraphicState::cmdInterior= { B|F|W|A|T|S|M, 1,0,0 };

GMANGraphicState::CommandIdentity
GMANGraphicState::cmdExterior= { B|F|W|A|T|S|M, 1,0,0 };

GMANGraphicState::CommandIdentity
GMANGraphicState::cmdShadingRate= { B|F|W|A|T|S|M, 1,0,0 };

GMANGraphicState::CommandIdentity
GMANGraphicState::cmdShadingInterpolation= { B|F|W|A|T|S|M, 1,0,0 };

GMANGraphicState::CommandIdentity
GMANGraphicState::cmdMatte= { B|F|W|A|T|S|M, 1,0,0 };

GMANGraphicState::CommandIdentity
GMANGraphicState::cmdBound= { B|F|W|A|T|S|M, 1,0,0 };

GMANGraphicState::CommandIdentity
GMANGraphicState::cmdDetail= { B|F|W|A|T|S|M, 1,0,0 };

GMANGraphicState::CommandIdentity
GMANGraphicState::cmdDetailRange= { B|F|W|A|T|S|M, 1,0,0 };

GMANGraphicState::CommandIdentity
GMANGraphicState::cmdGeometricApproximation= { B|F|W|A|T|S|M, 1,0,0 };

GMANGraphicState::CommandIdentity
GMANGraphicState::cmdOrientation= { B|F|W|A|T|S|M, 1,0,0 };

GMANGraphicState::CommandIdentity
GMANGraphicState::cmdReverseOrientation= { B|F|W|A|T|S|M, 1,0,0 };

GMANGraphicState::CommandIdentity
GMANGraphicState::cmdSides= { B|F|W|A|T|S|M, 1,0,0 };

GMANGraphicState::CommandIdentity 
GMANGraphicState::cmdBasis= { B|F|W|A|T|S|M, 1,0,0 };

// TRANSFORMS
GMANGraphicState::CommandIdentity
GMANGraphicState::cmdIdentity= { B|F|W|A|T|S|M, 0,1,0 };

GMANGraphicState::CommandIdentity
GMANGraphicState::cmdTransform= { B|F|W|A|T|S|M, 0,1,0 };

GMANGraphicState::CommandIdentity
GMANGraphicState::cmdConcatTransform= { B|F|W|A|T|S|M, 0,1,0 };

GMANGraphicState::CommandIdentity
GMANGraphicState::cmdPerspective= { B|F|W|A|T|S|M, 0,1,0 };

GMANGraphicState::CommandIdentity
GMANGraphicState::cmdTranslate= { B|F|W|A|T|S|M, 0,1,0 };

GMANGraphicState::CommandIdentity
GMANGraphicState::cmdRotate= { B|F|W|A|T|S|M, 0,1,0 };

GMANGraphicState::CommandIdentity
GMANGraphicState::cmdScale= { B|F|W|A|T|S|M, 0,1,0 };

GMANGraphicState::CommandIdentity
GMANGraphicState::cmdSkew= { B|F|W|A|T|S|M, 0,1,0 };


GMANGraphicState::CommandIdentity
GMANGraphicState::cmdCoordinateSystem= { B|F|W|A|T|S, 0,0,0 };

GMANGraphicState::CommandIdentity
GMANGraphicState::cmdCoordSysTransform= { B|F|W|A|T|S, 0,0,0 };

GMANGraphicState::CommandIdentity
GMANGraphicState::cmdTransformPoints= { B|F|W|A|T||S, 0,0,0 };


GMANGraphicState::CommandIdentity
GMANGraphicState::cmdAttribute= { 0, 0,0,0 }; // TODO ****

// PRIMITIVES
GMANGraphicState::CommandIdentity
GMANGraphicState::cmdPolygon= { W|A|T|S|O|M, 0,0,1 };

GMANGraphicState::CommandIdentity
GMANGraphicState::cmdGeneralPolygon= { W|A|T|S|O|M, 0,0,1 };

GMANGraphicState::CommandIdentity
GMANGraphicState::cmdPointsPolygon= { W|A|T|S|O|M, 0,0,1 };

GMANGraphicState::CommandIdentity
GMANGraphicState::cmdPointsGeneralPolygons= { W|A|T|S|O|M, 0,0,1 };

GMANGraphicState::CommandIdentity
GMANGraphicState::cmdPatch= { W|A|T|S|O|M, 0,0,1 };

GMANGraphicState::CommandIdentity
GMANGraphicState::cmdPatchMesh= { W|A|T|S|O|M, 0,0,1 };

GMANGraphicState::CommandIdentity
GMANGraphicState::cmdNuPatch= { W|A|T|S|O|M, 0,0,1 };

GMANGraphicState::CommandIdentity
GMANGraphicState::cmdTrimCurve= { W|A|T|S|O|M, 1,0,0 };

GMANGraphicState::CommandIdentity
GMANGraphicState::cmdSphere= { W|A|T|S|O|M, 0,0,1 };

GMANGraphicState::CommandIdentity
GMANGraphicState::cmdCone= { W|A|T|S|O|M, 0,0,1 };

GMANGraphicState::CommandIdentity
GMANGraphicState::cmdCylinder= { W|A|T|S|O|M, 0,0,1 };

GMANGraphicState::CommandIdentity
GMANGraphicState::cmdHyperboloid= { W|A|T|S|O|M, 0,0,1 };

GMANGraphicState::CommandIdentity
GMANGraphicState::cmdParaboloid= { W|A|T|S|O|M, 0,0,1 };

GMANGraphicState::CommandIdentity
GMANGraphicState::cmdDisk= { W|A|T|S|O|M, 0,0,1 };

GMANGraphicState::CommandIdentity
GMANGraphicState::cmdTorus= { W|A|T|S|O|M, 0,0,1 };

GMANGraphicState::CommandIdentity
GMANGraphicState::cmdBlobby= { W|A|T|S|O|M, 0,0,1 };

GMANGraphicState::CommandIdentity
GMANGraphicState::cmdCurves= { W|A|T|S|O|M, 0,0,1 };

GMANGraphicState::CommandIdentity
GMANGraphicState::cmdPoints= { W|A|T|S|O|M, 0,0,1 };

GMANGraphicState::CommandIdentity
GMANGraphicState::cmdSubdivisionMesh= { W|A|T|S|O|M, 0,0,1 };

GMANGraphicState::CommandIdentity
GMANGraphicState::cmdProcedural= { W|A|T|S|O, 0,0,1 };

GMANGraphicState::CommandIdentity
GMANGraphicState::cmdGeometry= { W|A|T|S|O, 0,0,1 };


GMANGraphicState::CommandIdentity
GMANGraphicState::cmdObjectInstance= { W|A|T|S, 0,0,0 };

GMANGraphicState::CommandIdentity
GMANGraphicState::cmdMakeTexture= { B|F, 0,0,0 };

GMANGraphicState::CommandIdentity
GMANGraphicState::cmdMakeBump= { B|F, 0,0,0 };

GMANGraphicState::CommandIdentity
GMANGraphicState::cmdMakeLatLongEnvironment= { B|F, 0,0,0 };

GMANGraphicState::CommandIdentity
GMANGraphicState::cmdMakeCubeFaceEnvironment= { B|F, 0,0,0 };

GMANGraphicState::CommandIdentity
GMANGraphicState::cmdMakeShadow= { B|F, 0,0,0 };


static RtFloat tr_times[1]={0}; 

GMANGraphicState::GMANGraphicState() : mm(1,tr_times)
{
  nest.push(B);
  block=B;
  frame=0;
  world=0;
  attribute=0;
  transform=0;
  solid=0;
  motion=0;
  object=0;

  attribsChangedFlag=false;
  transformChangedFlag=false;

  nbSamples=0;
  motionIndex=0;
  currentMC=0;
  motionError=false;

  optionsStack.push(*(new GMANOptions));
  attributesStack.push(*(new GMANAttributes));
  transformStack.push(*(new GMANTransform));
}
GMANOptions &GMANGraphicState::getOptions()
{
  return optionsStack.top();
}
GMANAttributes &GMANGraphicState::getAttributes()
{
  return attributesStack.top();
}
RtVoid GMANGraphicState::enterMode (CurrentState mode)
{
  switch (mode) {
  case F: // ** ENTER FRAME ** //
    allowed(B);
    if (frame==1) {
      error("RiFrameBegin cannot be nested");
    }
    frame=1;
    push(F);

    optionsStack.push(optionsStack.top());
    attributesStack.push(attributesStack.top());
    transformStack.push(transformStack.top());
    break;

  case W: // ** ENTER WORLD ** //
    allowed(B|F);
    if (world==1) {
      error("RiWorldBegin cannot be nested");
    }
    world=1;
    push(W);

    attributesStack.push(attributesStack.top());
    transformStack.push(transformStack.top());
    break;

  case A: // ** ENTER ATTRIBUTE ** //
    allowed(B|F|W|A|T|S);
    attribute+=1;
    push(A);

    attributesStack.push(attributesStack.top());
    transformStack.push(transformStack.top());
    break;

  case T: // ** ENTER TRANSFORM ** //
    allowed(B|F|W|A|T|S);
    transform+=1;
    push(T);

    transformStack.push(transformStack.top());
    break;

  case O: // ** ENTER OBJECT ** //
    allowed(B|F|W|S);
    if (object==1) {
      error("RiObjectBegin cannot be nested");
    }
    object=1;
    push(O);
    break;

  case S: // ** ENTER SOLID ** //
    allowed(W|S);
    solid+=1;
    push(S);

    attributesStack.push(attributesStack.top());
    transformStack.push(transformStack.top());
    break;

  case M: // ** ENTER MOTION ** //
    GMANError error (RIE_BUG,RIE_SEVERE,"Internal error: use of entermode(int) for a motion block");
    throw error;
  }
}
RtVoid GMANGraphicState::enterMotion (RtInt n, RtFloat *s)
{
  allowed(B|F|W|A|T|S);
  if (motion==1) {
    error("RiMotionBegin cannot be nested");
  }
  motion=1;
  push(M);
  
  nbSamples=n;
  samples.resize(n);
  for(RtInt i=0;i<n;i++)
      samples[i]=s[i];
  motionIndex=0;
  motionError=false;
}

RtVoid GMANGraphicState::leaveMode (CurrentState mode)
{
  CurrentState i=nest.top();
  switch (mode) {
  case F: // ** LEAVE FRAME ** //
    if (frame==0) {
      error("RiFrameBegin not called");
    }
    overlap(F,i);
    frame=0;
    pop();

    optionsStack.pop();
    attributesStack.pop();
    transformStack.pop();
    break;

  case W: // ** LEAVE WORLD ** //
    if (world==0) {
      error("RiWorldBegin not called");
    }
    overlap(W,i);
    world=0;
    pop();

    attributesStack.pop();
    transformStack.pop();
    break;

  case A: // ** LEAVE ATTRIBUTE ** //
    if (attribute==0) {
      error("RiAttributeBegin not called");
    }
    overlap(A,i);
    attribute-=1;
    pop();

    attributesStack.pop();
    transformStack.pop();
    break;

  case T: // ** LEAVE TRANSFORM ** //
    if (transform==0) {
      error("RiTransformBegin not called");
    }
    overlap(T,i);
    transform-=1;
    pop();

    transformStack.pop();
    break;

  case O: // ** LEAVE OBJECT ** //
    if (object==0) {
      error("RiObjectBegin not called");
    }
    object=0;
    pop();
    overlap(O,i);
    break;

  case S: // ** LEAVE SOLID ** //
    if (solid==0) {
      error("RiSolidBegin not called");
    }
    overlap(S,i);
    solid-=1;
    pop();

    attributesStack.pop();
    transformStack.pop();
    break;

  case M: // ** LEAVE MOTION ** //
    if (motion==0) {
      error("RiMotionBegin not called");
    }
    overlap(M,i);
    motion=0;
    pop();

    nbSamples=0;
    samples.resize(0);
    break;
  }
}

RtVoid GMANGraphicState::allowed(CommandIdentity &cid)
{
  allowed(cid.allowedBlocks);
  if (cid.isAttribute==true) attribsChangedFlag=true; 
  if (cid.isTransform==true) transformChangedFlag=true;
  if (cid.isPrimitive==true) {
    attribsChangedFlag=false;
    transformChangedFlag=false;
  };

  if (motion==1) {
    if (motionIndex==0) {
      currentMC=&cid;
    } else if (&cid!=currentMC) {
      motionError=true;
      GMANError error(RIE_BADMOTION,RIE_WARNING,
		      "Commands must be the same in a Motion Block");
      throw error;
    }
  }
}
RtVoid GMANGraphicState::allowed (RtInt mode)
{
  if ((block&mode)==0) {
    GMANError r(RIE_ILLSTATE,RIE_WARNING,"Invalid mode for procedure");
    throw r;
  }
}
RtVoid GMANGraphicState::push(CurrentState mode)
{
  nest.push(mode);
  //if ((mode!=T)&&(mode!=A)) block=mode;
  block=mode;
}
RtVoid GMANGraphicState::pop()
{
  if (nest.size()==0) return;
  nest.pop();
  //if ((nest.top()!=T)&&(nest.top()!=A)) block=nest.top();
  block=nest.top();
}
RtVoid GMANGraphicState::overlap (CurrentState mode, CurrentState current)
{
  if (mode!=current) {
    error("Blocks cannot overlap");
  }
}
RtVoid GMANGraphicState::error(char *message)
{
  GMANError error(RIE_NESTING,RIE_SEVERE,message);
  throw error;
}

bool GMANGraphicState::sameAttribs()
{
  return !attribsChangedFlag;
}

bool GMANGraphicState::sameTransform()
{
  return !transformChangedFlag;
}

const GMANTransform &GMANGraphicState::getTransform() const 
{
  return transformStack.top();
}

RtVoid GMANGraphicState::setTransform(GMANMatrix4 &m)
{
  if (motion==1) { // in motion
    if (motionIndex==0) {
      RtFloat *temp = new RtFloat[nbSamples];
      for(RtInt i=0;i<nbSamples;i++)
		temp[i]=samples[i];
	  GMANMovingMatrix mm2(nbSamples,temp);
	  mm=mm2;
	  if(temp) delete[] temp;
    } else if (motionIndex==nbSamples) {
      GMANError error(RIE_BADMOTION,RIE_WARNING,"Too many Transforms in Motion Block");
      throw error;
    }
    mm.get(motionIndex)=m;
    motionIndex+=1;
    if (motionIndex==nbSamples) {
      if (motionError==true) return;
      GMANTransform a(mm);
      transformStack.top()=a;
    }
  } else { // not in motion
    GMANOneMatrix a(m);
    GMANTransform b(a);
    transformStack.top()=b;
  }
}
RtVoid GMANGraphicState::buildTransform(GMANMatrix4 &m)
{
  if (motion==1) { // in motion
    if (motionIndex==0) {
      RtFloat *temp = new RtFloat[nbSamples];
      for(RtInt i=0;i<nbSamples;i++)
		temp[i]=samples[i];
      GMANMovingMatrix mm2(nbSamples,temp);
      mm=mm2;
	  if(temp) delete[] temp;
    } else if (motionIndex==nbSamples) {
      GMANError error(RIE_BADMOTION,RIE_WARNING,"Too many Transforms in Motion Block");
      throw error;
    }
    mm.get(motionIndex)=m;
    motionIndex+=1;
    if (motionIndex==nbSamples) {
      if (motionError=true) return;
      GMANTransform a(mm);
      transformStack.top().concat(a);
    }
  } else { // not in motion
    GMANOneMatrix a(m);
    GMANTransform b(a);
    transformStack.top().concat(b);
  }
}
