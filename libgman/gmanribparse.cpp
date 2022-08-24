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

/* Local Headers */
#include "ri.h"      /* RenderMan Interface */
#include "universalsuperclass.h" /* Super class */
#include "gmanribparse.h" /* Declaration Header */

/*
 * Global static data
 *
 */
const int GMANRIBParse::nKeywords = 0;
RtToken   *GMANRIBParse::KeywordTable=NULL;

/*
 * RenderMan API gmanribparse
 *
 */

// default constructor
GMANRIBParse::GMANRIBParse(GMANRenderMan *renderman,
			   const char *rib,
			   RtToken name) 
  throw(GMANError) : 
  UniversalSuperClass(), 
  handlersRegistered(false),
  renderMan(renderman),
  ribFile(rib)
{ 
  renderMan->RiBegin(name);

};


// default destructor 
GMANRIBParse::~GMANRIBParse() { 
  renderMan->RiEnd();
};


RtBoolean	GMANRIBParse::addHandler(RtToken	keyword,
				       RIBHandler	handler) {

  // attempt to find keyword
  TokenHandlerMap::iterator handlerIter = tokenHandlerMap.find(keyword);
  if(handlerIter == tokenHandlerMap.end()) {

    tokenHandlerMap.insert(TokenHandlerMap::value_type(keyword,
						       handler));
    
    return RI_TRUE;
  }
  return RI_FALSE;

}


RtVoid	GMANRIBParse::addDefaultHandlers(RtVoid) {
#if 0
  for(int i=0; i=nKeywords; i++) {
    // ignore return
    (void)addHandler(KeywordTable[i], defaultHandler);
  };
#endif
}

RtVoid  GMANRIBParse::parse(RtVoid) throw(GMANError) {

  // add all handlers
  if(!handlersRegistered) {
    addHandlers();
    
    // add default handlers
    addDefaultHandlers();
    
    handlersRegistered=true;
  }
  
  while(true) {
    const GMANToken &tok = nextToken();

    if (tok.getType() == GMANToken::END_OF_FILE) {
      break;
    }

    switch(tok.getType()) {
    case GMANToken::STRING:
      debug("String token: \"%s\"", tok.getString().c_str());
      break;
    case GMANToken::REAL:
      debug("Real token: %f", tok.getReal());
      break;
    case GMANToken::LONGINT:
      debug("Long token: %l", tok.getLongInt());
      break;
    case GMANToken::RI_VERSION:
      debug("Keyword token: Version");
      nextFloat();
      // FIXME: Maybe do something
      break;
    case GMANToken::RI_OPTION:
      debug("Keyword token: Option");
      parseOption();
      break;
    case GMANToken::RI_DISPLAY:
      debug("Keyword token: Display");
      parseDisplay();
      break;
    case GMANToken::RI_FORMAT:
      debug("Keyword token: Format");
      parseFormat();
      break;
    case GMANToken::RI_PROJECTION:
      debug("Keyword token: Projection");
      parseProjection();
      break;
    case GMANToken::RI_GEOMETRIC_APPROXIMATION:
      debug("Keyword token: GeometricApproximation");
      parseGeometricApproximation();
      break;
    case GMANToken::RI_SHADING_INTERPOLATION:
      debug("Keyword token: ShadingInterpolation");
      parseShadingInterpolation();
      break;
    case GMANToken::RI_SHADING_RATE:
      debug("Keyword token: ShadingRate");
      parseShadingRate();
      break;
    case GMANToken::RI_ORIENTATION:
      debug("Keyword token: Orientation");
      parseOrientation();
      break;
    case GMANToken::RI_REVERSE_ORIENTATION:
      debug("Keyword token: ReverseOrientation");
      renderMan->RiReverseOrientation();
      break;
    case GMANToken::RI_PIXEL_SAMPLES:
      debug("Keyword token: PixelSamples");
      parsePixelSamples();
      break;
    case GMANToken::RI_EXPOSURE:
      debug("Keyword token: Exposure");
      parseExposure();
      break;
    case GMANToken::RI_DEPTH_OF_FIELD:
      debug("Keyword token: DepthOfField");
      parseDepthOfField();
      break;
    case GMANToken::RI_SHUTTER:
      debug("Keyword token: Shutter");
      parseShutter();
      break;
    case GMANToken::RI_HIDER:
      debug("Keyword token: Hider");
      parseHider();
      break;
    case GMANToken::RI_CROP_WINDOW:
      debug("Keyword token: CropWindow");
      parseCropWindow();
      break;
    case GMANToken::RI_SCREEN_WINDOW:
      debug("Keyword token: ScreenWindow");
      parseScreenWindow();
      break;
    case GMANToken::RI_CLIPPING:
      debug("Keyword token: Clipping");
      parseClipping();
      break;
    case GMANToken::RI_DECLARE:
      debug("Keyword token: Declare");
      parseDeclare();
      break;
    case GMANToken::RI_ATTRIBUTE:
      debug("Keyword token: Attribute");
      parseAttribute();
      break;
    case GMANToken::RI_WORLD_BEGIN:
      renderMan->RiWorldBegin();
      break;
    case GMANToken::RI_WORLD_END:
      debug("Keyword token: WorldEnd");
      renderMan->RiWorldEnd();
      break;
    case GMANToken::RI_ATTRIBUTE_BEGIN:
      debug("Keyword token: AttributeBegin");
      renderMan->RiAttributeBegin();
      break;
    case GMANToken::RI_ATTRIBUTE_END:
      debug("Keyword token: AttributeEnd");
      renderMan->RiAttributeEnd();
      break;
    case GMANToken::RI_TRANSFORM_BEGIN:
      debug("Keyword token: TransformBegin");
      renderMan->RiTransformBegin();
      break;
    case GMANToken::RI_TRANSFORM_END:
      debug("Keyword token: TransformEnd");
      renderMan->RiTransformEnd();
      break;
    case GMANToken::RI_FRAME_BEGIN:
      debug("Keyword token: FrameBegin");
      renderMan->RiFrameBegin(nextInt());
      break;
    case GMANToken::RI_FRAME_END:
      debug("Keyword token: FrameEnd");
      renderMan->RiFrameEnd();
      break;
    case GMANToken::RI_MOTION_BEGIN:
      debug("Keyword token: MotionBegin");
      parseMotionBegin();
      break;
    case GMANToken::RI_MOTION_END:
      debug("Keyword token: MotionEnd");
      parseMotionEnd();
      break;
    case GMANToken::RI_OBJECT_BEGIN:
      debug("Keyword token: ObjectBegin");
      parseObjectBegin();
      break;
    case GMANToken::RI_OBJECT_END:
      debug("Keyword token: ObjectEnd");
      parseObjectEnd();
      break;
    case GMANToken::RI_COLOR:
      debug("Keyword token: Color");
      parseColor();
      break;
    case GMANToken::RI_OPACITY:
      debug("Keyword token: Opacity");
      parseOpacity();
      break;
    case GMANToken::RI_LIGHT_SOURCE:
      debug("Keyword token: LightSource");
      parseLightSource();
      break;
    case GMANToken::RI_SURFACE:
      debug("Keyword token: Surface");
      parseSurface();
      break;
    case GMANToken::RI_COORDINATE_SYSTEM:
      debug("Keyword token: CoordinateSystem");
      parseCoordinateSystem();
      break;
    case GMANToken::RI_IDENTITY:
      debug("Keyword token: Identity");
      renderMan->RiIdentity();
      break;
    case GMANToken::RI_TRANSFORM:
      debug("Keyword token: Transform");
      parseTransform();
      break;
    case GMANToken::RI_CONCAT_TRANSFORM:
      debug("Keyword token: ConcatTransform");
      parseConcatTransform();
      break;
    case GMANToken::RI_TRANSLATE:
      debug("Keyword token: Translate");
      parseTranslate();
      break;
    case GMANToken::RI_ROTATE:
      debug("Keyword token: Rotate");
      parseRotate();
      break;
    case GMANToken::RI_SCALE:
      debug("Keyword token: Scale");
      parseScale();
      break;
    case GMANToken::RI_SPHERE:
      debug("Keyword token: Sphere");
      parseSphere();
      break;
    case GMANToken::RI_CONE:
      debug("Keyword token: Cone");
      parseCone();
      break;
    case GMANToken::RI_CYLINDER:
      debug("Keyword token: Cylinder");
      parseCylinder();
      break;
    case GMANToken::RI_SIDES:
      debug("Keyword token: Sides");
      parseSides();
      break;
    case GMANToken::RI_HYPERBOLOID:
      debug("Keyword token: Hyperboloid");
      parseHyperboloid();
      break;
    case GMANToken::RI_PARABOLOID:
      debug("Keyword token: Paraboloid");
      parseParaboloid();
      break;
    case GMANToken::RI_TORUS:
      debug("Keyword token: Torus");
      parseTorus();
      break;
    case GMANToken::RI_DISK:
      debug("Keyword token: Disk");
      parseDisk();
      break;
    case GMANToken::RI_POLYGON:
      debug("Keyword token: Polygon");
      parsePolygon();
      break;
    case GMANToken::RI_POINTS:
      debug("Keyword token: Points");
      parsePoints();
      break;
    case GMANToken::RI_POINTS_POLYGONS:
      debug("Keyword token: PointsPolygons");
      parsePointsPolygons();
      break;
    case GMANToken::RI_POINTS_GENERAL_POLYGONS:
      debug("Keyword token: PointsGeneralPolygons");
      parsePointsGeneralPolygons();
      break;
    case GMANToken::RI_PATCH:
      debug("Keyword token: Patch");
      parsePatch();
      break;
    case GMANToken::RI_NU_PATCH:
      debug("Keyword token: NuPatch");
      parseNuPatch();
      break;
    case GMANToken::RI_PATCH_MESH:
      debug("Keyword token: PatchMesh");
      parsePatchMesh();
      break;
    case GMANToken::RI_TEXTURE_COORDINATES:
      debug("Keyword token: TextureCoordinates");
      parseTextureCoordinates();
      break;
    case GMANToken::RI_READ_ARCHIVE:
      debug("Keyword token: ReadArchive");
      parseReadArchive();
      break;
    case GMANToken::RI_OBJECT_INSTANCE:
      debug("Keyword token: ObjectInstance");
      parseObjectInstance();
      break;
    case GMANToken::RI_BASIS:
      debug("Keyword token: Basis");
      parseBasis();
      break;
    case GMANToken::RI_ATMOSPHERE:
      debug("Keyword token: Atmosphere");
      parseAtmosphere();
      break;
    case GMANToken::RI_DISPLACEMENT:
      debug("Keyword token: Displacement");
      parseDisplacement();
      break;
    case GMANToken::RI_IMAGER:
      debug("Keyword token: Imager");
      parseImager();
      break;
    case GMANToken::RI_ILLUMINATE:
      debug("Keyword token: Illuminate");
      parseIlluminate();
      break;
      /*
    case GMANToken::RI_SHADING_RATE:
      parseShadingRate();
      break;
      */
    default:
      debug("Unknown token type: %i", tok.getType());
      GMANError error(RIE_BADFILE,RIE_WARNING,"Unknown token in ribfile");
      throw(error);
      break;
    }
  }

}

RtFloat GMANRIBParse::nextFloat () {
  const GMANToken &tok = nextToken();
  
  float real;

  if (tok.getType() == GMANToken::REAL) {
    real = tok.getReal();
  } else if (tok.getType() == GMANToken::LONGINT) {
    real = (float) tok.getLongInt();
  } else {
    GMANError error(RIE_BADFILE,RIE_ERROR,"Expecting float token");
    throw(error);
  }

  return real;
}

RtInt GMANRIBParse::nextInt () {
  const GMANToken &tok = nextToken();
  
  if (tok.getType() != GMANToken::LONGINT) {
    GMANError error(RIE_BADFILE,RIE_ERROR,"Expecting int token");
    throw(error);
  }

  return tok.getLongInt();
}

char* GMANRIBParse::copyStringToken() {
  const GMANToken &tok = nextToken();
  
  if (tok.getType() != GMANToken::STRING) {
    GMANError error(RIE_BADFILE,RIE_ERROR,"Expecting string token");
    throw(error);
  }

  const char *string = tok.getString().c_str();
  char *retval = new char[strlen(string) + 1];
  strcpy(retval, string);
  return retval;
}

RtVoid  GMANRIBParse::parseOption(RtVoid) throw(GMANError) {
  char *name = copyStringToken();

  RtInt n = 0;
  RtToken* tokens;
  RtPointer* parms;

  parseParameterList(n, tokens, parms);

  renderMan->RiOptionV(name, n, tokens, parms);

  delete name;
  
}

RtVoid  GMANRIBParse::parseDisplay(RtVoid) throw(GMANError) {
  char *name = copyStringToken();
  char *type = copyStringToken();
  char *mode = copyStringToken();

  RtInt n = 0;
  RtToken* tokens;
  RtPointer* parms;

  parseParameterList(n, tokens, parms);

  renderMan->RiDisplayV(name, type, mode, n, tokens, parms);

  delete name;
  delete type;
  delete mode;
  
}

RtVoid  GMANRIBParse::parseFormat(RtVoid) throw(GMANError) {
  RtInt xresolution = nextInt();
  RtInt yresolution = nextInt();
  RtFloat pixelaspectratio = nextFloat();

  renderMan->RiFormat(xresolution, yresolution, pixelaspectratio);
}

RtVoid GMANRIBParse::parseProjection(RtVoid) throw(GMANError) {
  char *name = copyStringToken();

  RtInt n = 0;
  RtToken* tokens;
  RtPointer* parms;

  parseParameterList(n, tokens, parms);

  renderMan->RiProjectionV(name, n, tokens, parms);

  delete name;
}

RtVoid GMANRIBParse::parseGeometricApproximation(RtVoid) throw(GMANError) {
  char *type = copyStringToken();
  RtFloat value = nextFloat();

  renderMan->RiGeometricApproximation(type, value);

  delete type;
}

RtVoid GMANRIBParse::parseShadingInterpolation(RtVoid) throw(GMANError) {
  
  RtToken type = copyStringToken();

  renderMan->RiShadingInterpolation(type);

  delete type;
}

RtVoid GMANRIBParse::parseShadingRate(RtVoid) throw(GMANError) {
  
  RtFloat size = nextFloat();

  renderMan->RiShadingRate(size);
}

RtVoid GMANRIBParse::parseOrientation(RtVoid) throw(GMANError) {
  
  RtToken orientation = copyStringToken();

  renderMan->RiOrientation(orientation);

  delete orientation;
}

RtVoid GMANRIBParse::parsePixelSamples(RtVoid) throw(GMANError) {

  RtFloat xsamples = nextFloat();
  RtFloat ysamples = nextFloat();
  
  renderMan->RiPixelSamples(xsamples, ysamples);
}

RtVoid GMANRIBParse::parseExposure(RtVoid) throw(GMANError) {
  
  RtFloat gain = nextFloat();
  RtFloat gamma = nextFloat();
  
  renderMan->RiExposure(gain, gamma);
}

RtVoid GMANRIBParse::parseDepthOfField(RtVoid) throw(GMANError) {
  
  RtFloat fstop = nextFloat();
  RtFloat focallength = nextFloat();
  RtFloat focaldistance = nextFloat();
  
  renderMan->RiDepthOfField(fstop, focallength, focaldistance);
}

RtVoid GMANRIBParse::parseShutter(RtVoid) throw(GMANError) {
  
  RtFloat min = nextFloat();
  RtFloat max = nextFloat();
  
  renderMan->RiShutter(min, max);
}

RtVoid GMANRIBParse::parseHider(RtVoid) throw(GMANError) {
  
  char *type = copyStringToken();

  RtInt n = 0;
  RtToken* tokens;
  RtPointer* parms;

  parseParameterList(n, tokens, parms);

  renderMan->RiHiderV(type, n, tokens, parms);
}

RtVoid GMANRIBParse::parseCropWindow(RtVoid) throw(GMANError) {
  
  RtFloat xmin = nextFloat();
  RtFloat xmax = nextFloat();
  RtFloat ymin = nextFloat();
  RtFloat ymax = nextFloat();
  
  renderMan->RiCropWindow(xmin, xmax, ymin, ymax);
}

RtVoid GMANRIBParse::parseScreenWindow(RtVoid) throw(GMANError) {
  
  RtFloat left = nextFloat();
  RtFloat right = nextFloat();
  RtFloat bottom = nextFloat();
  RtFloat top = nextFloat();
  
  renderMan->RiScreenWindow(left, right, bottom, top);
}

RtVoid GMANRIBParse::parseClipping(RtVoid) throw(GMANError) {
  
  RtFloat nearDist = nextFloat();
  RtFloat farDist = nextFloat();
  
  renderMan->RiClipping(nearDist, farDist);
}

RtVoid GMANRIBParse::parseDeclare(RtVoid) throw(GMANError) {
  
  RtToken name = copyStringToken();
  RtToken declaration = copyStringToken();

  renderMan->RiDeclare(name, declaration);

  delete name;
  delete declaration;
}

RtVoid GMANRIBParse::parseAttribute(RtVoid) throw(GMANError) {
  
  RtToken name = copyStringToken();

  RtInt n = 0;
  RtToken* tokens;
  RtPointer* parms;

  parseParameterList(n, tokens, parms);

  renderMan->RiAttributeV(name, n, tokens, parms);

  delete name;
}

RtVoid GMANRIBParse::parseColor(RtVoid) throw(GMANError) {
  int number = 3;
  //FIXME: Should actually get number from options
  //renderMan->getGraphicsState().getOptions().getColorSamples().getNumber();

  RtFloat *color = new RtFloat[number];
  const GMANToken &lookAhead = peekToken();
  if (lookAhead.getType() == GMANToken::LEFT_BRACKET) {
    // FIXME: Do something with the array
    GMANRIBParse::TokenVector tokenVector = parseArray();
  } else {
    for (int i = 0; i < number; i++) {
      color[i] = nextFloat();
    }
  }

  renderMan->RiColor(color);
  if(color) delete[]color;
}

RtVoid GMANRIBParse::parseOpacity(RtVoid) throw(GMANError) {
  int number = 3;
  //FIXME: Should actually get number from options
  //renderMan->getGraphicsState().getOptions().getColorSamples().getNumber();

  RtFloat *color=new RtFloat[number];
  const GMANToken &lookAhead = peekToken();
  if (lookAhead.getType() == GMANToken::LEFT_BRACKET) {
    // FIXME: Do something with the array
    GMANRIBParse::TokenVector tokenVector = parseArray();
  } else {
    for (int i = 0; i < number; i++) {
      color[i] = nextFloat();
    }
  }

  renderMan->RiOpacity(color);
  if(color) delete [] color;
}

RtVoid GMANRIBParse::parseLightSource(RtVoid) throw(GMANError) {
  char *shadername = copyStringToken();
  int sequence = nextInt();

  RtInt n = 0;
  RtToken* tokens;
  RtPointer* parms;

  parseParameterList(n, tokens, parms);

  RtLightHandle handle =
    renderMan->RiLightSourceV(shadername, n, tokens, parms);

  lightHandleMap[sequence] = handle;

  delete shadername;
}

RtVoid GMANRIBParse::parseSurface(RtVoid) throw(GMANError) {
  RtToken shadername = copyStringToken();

  RtInt n = 0;
  RtToken* tokens;
  RtPointer* parms;

  parseParameterList(n, tokens, parms);

  // FIXME: Implement some surface shaders
  renderMan->RiSurfaceV(shadername, n, tokens, parms);

  delete shadername;
}

RtVoid GMANRIBParse::parseCoordinateSystem(RtVoid) throw(GMANError) {

  char* name = copyStringToken();

  renderMan->RiCoordinateSystem(name);

  delete name;
}

RtVoid GMANRIBParse::parseTransform(RtVoid) throw(GMANError) {
  GMANRIBParse::TokenVector tokenVector = parseArray();
  // FIXME: Use the array

  RtMatrix transform;

  renderMan->RiTransform(transform);
}

RtVoid GMANRIBParse::parseConcatTransform(RtVoid) throw(GMANError) {
  GMANRIBParse::TokenVector tokenVector = parseArray();
  // FIXME: Use the array

  RtMatrix transform;

  renderMan->RiConcatTransform(transform);
}

RtVoid GMANRIBParse::parseTranslate(RtVoid) throw(GMANError) {
  RtFloat dx = nextFloat();
  RtFloat dy = nextFloat();
  RtFloat dz = nextFloat();

  renderMan->RiTranslate(dx, dy, dz);
}

RtVoid GMANRIBParse::parseRotate(RtVoid) throw(GMANError) {
  RtFloat angle = nextFloat();
  RtFloat dx = nextFloat();
  RtFloat dy = nextFloat();
  RtFloat dz = nextFloat();

  renderMan->RiRotate(angle, dx, dy, dz);
}

RtVoid GMANRIBParse::parseScale(RtVoid) throw(GMANError) {
  RtFloat sx = nextFloat();
  RtFloat sy = nextFloat();
  RtFloat sz = nextFloat();

  renderMan->RiScale(sx, sy, sz);
}

RtVoid GMANRIBParse::parseSphere(RtVoid) throw(GMANError) {
  RtFloat radius = nextFloat();
  RtFloat zmin = nextFloat();
  RtFloat zmax = nextFloat();
  RtFloat thetamax = nextFloat();

  RtInt n = 0;
  RtToken* tokens;
  RtPointer* parms;

  parseParameterList(n, tokens, parms);

  renderMan->RiSphereV(radius, zmin, zmax, thetamax, n, tokens, parms);
}

RtVoid GMANRIBParse::parseCone(RtVoid) throw(GMANError) {
  RtFloat height = nextFloat();
  RtFloat radius = nextFloat();
  RtFloat thetamax = nextFloat();

  RtInt n = 0;
  RtToken* tokens;
  RtPointer* parms;

  parseParameterList(n, tokens, parms);

  renderMan->RiConeV(height, radius, thetamax, n, tokens, parms);
}

RtVoid GMANRIBParse::parseCylinder(RtVoid) throw(GMANError) {
  RtFloat radius = nextFloat();
  RtFloat zmin = nextFloat();
  RtFloat zmax = nextFloat();
  RtFloat thetamax = nextFloat();

  RtInt n = 0;
  RtToken* tokens;
  RtPointer* parms;

  parseParameterList(n, tokens, parms);

  renderMan->RiCylinderV(radius, zmin, zmax, thetamax, n, tokens, parms);
}

RtVoid GMANRIBParse::parseSides(RtVoid) throw(GMANError) {
  RtInt sides = nextInt();

  renderMan->RiSides(sides);
}

RtVoid GMANRIBParse::parseHyperboloid(RtVoid) throw(GMANError) {
  RtPoint point1, point2;
  point1[0] = nextFloat();
  point1[1] = nextFloat();
  point1[2] = nextFloat();
  point2[0] = nextFloat();
  point2[1] = nextFloat();
  point2[2] = nextFloat();

  RtFloat thetamax = nextFloat();

  RtInt n = 0;
  RtToken* tokens;
  RtPointer* parms;

  parseParameterList(n, tokens, parms);

  renderMan->RiHyperboloidV(point1, point2, thetamax, n, tokens, parms);
}

RtVoid GMANRIBParse::parseParaboloid(RtVoid) throw(GMANError) {
  RtFloat rmax = nextFloat();
  RtFloat zmin = nextFloat();
  RtFloat zmax = nextFloat();
  RtFloat thetamax = nextFloat();

  RtInt n = 0;
  RtToken* tokens;
  RtPointer* parms;

  parseParameterList(n, tokens, parms);

  renderMan->RiParaboloidV(rmax, zmin, zmax, thetamax, n, tokens, parms);
}

RtVoid GMANRIBParse::parseTorus(RtVoid) throw(GMANError) {
  RtFloat majorradius = nextFloat();
  RtFloat minorradius = nextFloat();
  RtFloat phimin = nextFloat();
  RtFloat phimax = nextFloat();
  RtFloat thetamax = nextFloat();

  RtInt n = 0;
  RtToken* tokens;
  RtPointer* parms;

  parseParameterList(n, tokens, parms);

  renderMan->RiTorusV(majorradius, minorradius, phimin, phimax, thetamax,
		      n, tokens, parms);
}

RtVoid GMANRIBParse::parseDisk(RtVoid) throw(GMANError) {
  RtFloat height = nextFloat();
  RtFloat radius = nextFloat();
  RtFloat thetamax = nextFloat();

  RtInt n = 0;
  RtToken* tokens;
  RtPointer* parms;

  parseParameterList(n, tokens, parms);

  renderMan->RiDiskV(height, radius, thetamax, n, tokens, parms);
}

RtVoid GMANRIBParse::parsePolygon(RtVoid) throw(GMANError) {

  RtInt n = 0;
  RtToken* tokens;
  RtPointer* parms;

  parseParameterList(n, tokens, parms);

  int nverts = 0;
  for (int i = 0; i < n; i++) {
    if (! strcmp(tokens[i], "P")) {
      // FIXME: Are arrays NULL terminated?  Can use that for length
    }
  }

  renderMan->RiPolygonV(nverts, n, tokens, parms);
}

RtVoid GMANRIBParse::parsePoints(RtVoid) throw(GMANError) {

  RtInt n = 0;
  RtToken* tokens;
  RtPointer* parms;

  parseParameterList(n, tokens, parms);

  int npoints = 0;
  for (int i = 0; i < n; i++) {
    if (! strcmp(tokens[i], "P")) {
      // FIXME: Are arrays NULL terminated?  Can use that for length
    }
  }

  renderMan->RiPointsV(npoints, n, tokens, parms);
}

RtVoid GMANRIBParse::parsePointsPolygons(RtVoid) throw(GMANError) {

  // FIXME!!!
  GMANRIBParse::TokenVector tokenVector = parseArray();
  tokenVector = parseArray();

  RtInt n = 0;
  RtToken* tokens;
  RtPointer* parms;

  parseParameterList(n, tokens, parms);

  RtInt npolys = 0;
  RtInt *nvertices=NULL;
  RtInt *vertices=NULL;

  renderMan->RiPointsPolygonsV(npolys, nvertices, vertices,
			       n, tokens, parms);
}

RtVoid GMANRIBParse::parsePointsGeneralPolygons(RtVoid) throw(GMANError) {

  // FIXME!!!
  GMANRIBParse::TokenVector tokenVector = parseArray();
  tokenVector = parseArray();
  tokenVector = parseArray();

  RtInt n = 0;
  RtToken* tokens;
  RtPointer* parms;

  parseParameterList(n, tokens, parms);

  int npolys = 0;

  renderMan->RiPointsGeneralPolygonsV(npolys, NULL, NULL, NULL,
				      n, tokens, parms);
}

RtVoid GMANRIBParse::parsePatch(RtVoid) throw(GMANError) {

  char *type = copyStringToken();

  RtInt n = 0;
  RtToken* tokens;
  RtPointer* parms;

  parseParameterList(n, tokens, parms);

  renderMan->RiPatchV(type, n, tokens, parms);

  delete type;
}

RtVoid GMANRIBParse::parseNuPatch(RtVoid) throw(GMANError) {

  RtInt nu = nextInt();
  RtInt uorder = nextInt();
  GMANRIBParse::TokenVector tokenVector = parseArray(); // FIXME
  RtFloat umin = nextFloat();
  RtFloat umax = nextFloat();
  RtInt nv = nextInt();
  RtInt vorder = nextInt();
  tokenVector = parseArray(); // FIXME
  RtFloat vmin = nextFloat();
  RtFloat vmax = nextFloat();
  
  RtInt n = 0;
  RtToken* tokens;
  RtPointer* parms;

  parseParameterList(n, tokens, parms);

  renderMan->RiNuPatchV(nu, uorder, NULL, umin, umax, nv, vorder, NULL,
			vmin, vmax,
			n, tokens, parms);
}

RtVoid GMANRIBParse::parsePatchMesh(RtVoid) throw(GMANError) {

  char *type = copyStringToken();
  RtInt nu = nextInt();
  char *uwrap = copyStringToken();
  RtInt nv = nextInt();
  char *vwrap = copyStringToken();

  RtInt n = 0;
  RtToken* tokens;
  RtPointer* parms;

  parseParameterList(n, tokens, parms);

  renderMan->RiPatchMeshV(type, nu, uwrap, nv, vwrap, n, tokens, parms);

  delete type;
  delete uwrap;
  delete vwrap;
}

RtVoid GMANRIBParse::parseTextureCoordinates(RtVoid) throw(GMANError) {
  RtFloat s1 = nextFloat();
  RtFloat t1 = nextFloat();
  RtFloat s2 = nextFloat();
  RtFloat t2 = nextFloat();
  RtFloat s3 = nextFloat();
  RtFloat t3 = nextFloat();
  RtFloat s4 = nextFloat();
  RtFloat t4 = nextFloat();

  renderMan->RiTextureCoordinates(s1, t1, s2, t2, s3, t3, s4, t4);
}

RtVoid GMANRIBParse::parseReadArchive(RtVoid) throw(GMANError) {

  char* name = copyStringToken();

  // FIXME: Read the archive file!

  delete name;
}

RtVoid GMANRIBParse::parseMotionBegin(RtVoid) throw(GMANError) {
  GMANRIBParse::TokenVector tokenVector = parseArray();

  int n = tokenVector.size();
  RtFloat* times = tokenVector.toRtFloatArray();

  renderMan->RiMotionBeginV(n, times);

  delete times;
}

RtVoid GMANRIBParse::parseMotionEnd(RtVoid) throw(GMANError) {
  renderMan->RiMotionEnd();
}

RtVoid GMANRIBParse::parseObjectBegin(RtVoid) throw(GMANError) {

  int sequence = nextInt();

  RtObjectHandle handle = renderMan->RiObjectBegin();
  objectHandleMap[sequence] = handle;
}

RtVoid GMANRIBParse::parseObjectEnd(RtVoid) throw(GMANError) {
  renderMan->RiObjectEnd();
}

RtVoid GMANRIBParse::parseObjectInstance(RtVoid) throw(GMANError) {

  int sequence = nextInt();

  RtObjectHandle handle = objectHandleMap[sequence];

  renderMan->RiObjectInstance(handle);
}

RtVoid GMANRIBParse::parseBasis(RtVoid) throw(GMANError) {

  char* uname = copyStringToken();
  int ustep = nextInt();
  char* vname = copyStringToken();
  int vstep = nextInt();

  // FIXME: Look up actual basis by name, allow basis by matrix

  renderMan->RiBasis(RiBezierBasis, ustep, RiBezierBasis, vstep);

  delete uname;
  delete vname;
}

RtVoid GMANRIBParse::parseAtmosphere(RtVoid) throw(GMANError) {

  char* name = copyStringToken();

  RtInt n = 0;
  RtToken* tokens;
  RtPointer* parms;

  parseParameterList(n, tokens, parms);

  // FIXME: implement some atmosphere shaders
  //  renderMan->RiAtmosphereV(name, n, tokens, parms);

  delete name;
}

RtVoid GMANRIBParse::parseDisplacement(RtVoid) throw(GMANError) {

  char* name = copyStringToken();

  RtInt n = 0;
  RtToken* tokens;
  RtPointer* parms;

  parseParameterList(n, tokens, parms);

  // FIXME: implement some displacement shaders
  //  renderMan->RiDisplacementV(name, n, tokens, parms);

  delete name;
}

RtVoid GMANRIBParse::parseImager(RtVoid) throw(GMANError) {

  char* name = copyStringToken();

  RtInt n = 0;
  RtToken* tokens;
  RtPointer* parms;

  parseParameterList(n, tokens, parms);

  // FIXME: implement some imagers
  //renderMan->RiImagerV(name, n, tokens, parms);

  delete name;
}

RtVoid GMANRIBParse::parseIlluminate(RtVoid) throw(GMANError) {

  int sequence = nextInt();
  int onoff = nextInt();

  RtLightHandle handle = lightHandleMap[sequence];
  renderMan->RiIlluminate(handle, onoff==0?false:true);
}


GMANRIBParse::TokenVector GMANRIBParse::parseArray(RtVoid) throw(GMANError) {
  GMANToken tok = nextToken(); // '['
  
  TokenVector tokens;

  while (true) {
    tok = nextToken();
    if (tok.getType() == GMANToken::RIGHT_BRACKET) {
      break;
    }
    tokens.insert(tokens.end(), tok);
  }

  return tokens;
}

RtVoid GMANRIBParse::parseParameterList(RtInt &n, RtToken* &tokens,
					RtPointer* &parms) throw(GMANError) {
  typedef map<RtToken, RtPointer> ParamMap;
  ParamMap paramMap;
  
  while (true) {
    const GMANToken &tok = peekToken();
    if (tok.getType() != GMANToken::STRING) {
      //debug("While parsing parameter list, got token type: "
      //     << tok.getType());
      break;
    }

    GMANToken key = nextToken();
    const GMANToken &lookAhead = peekToken();
    if (lookAhead.getType() == GMANToken::LEFT_BRACKET) {
      GMANRIBParse::TokenVector tokenVector = parseArray();
      // FIXME: parse floats, build array, add param
    } else if (lookAhead.getType() == GMANToken::LONGINT) {
      GMANToken token = nextToken();
      RtFloat *value = new RtFloat[1];
      value[0] = token.getLongInt();
      paramMap[key] = (RtPointer) value;
    } else if (lookAhead.getType() == GMANToken::REAL) {
      GMANToken token = nextToken();
      RtFloat *value = new RtFloat[1];
      value[0] = token.getReal();
      paramMap[key] = (RtPointer) value;
    } else {
      RtToken value = copyStringToken();
      paramMap[key] = (RtPointer) value;
    }
  }
    
  n = paramMap.size();
  tokens = new RtToken[n];
  parms = new RtPointer[n];
  //map<RtToken, RtPointer>::iterator cur = paramMap.begin();
  ParamMap::iterator cur = paramMap.begin();
  for (unsigned int i = 0; cur != paramMap.end(); i++, cur++) {
    tokens[i] = (*cur).first;
    parms[i] = (*cur).second;
  }
}


const GMANToken &GMANRIBParse::nextToken() {
  if (lookAheadToken.getType() == GMANToken::UNKNOWN) {
    currentToken = tokenizer.getNext(ribFile);
  } else {
    currentToken = lookAheadToken;
    lookAheadToken = GMANToken();
  }
  
  return currentToken;
}

const GMANToken &GMANRIBParse::peekToken() {
  if (lookAheadToken.getType() == GMANToken::UNKNOWN) {
    lookAheadToken = tokenizer.getNext(ribFile);
  }

  return lookAheadToken;
}

RtToken* GMANRIBParse::TokenVector::toRtTokenArray() {
  RtToken* array = new RtToken[size()];

  for (unsigned int i = 0; i < size(); i++) {
  }

  throw(GMANError(RIE_SYNTAX, RIE_ERROR, "Code needs some work."));

  return array;
}

RtInt* GMANRIBParse::TokenVector::toRtIntArray() {
  RtInt* array = new RtInt[size()];

  for (unsigned int i = 0; i < size(); i++) {
    GMANToken tok = (*this)[i];
    if (tok.getType() == GMANToken::LONGINT) {
      array[i] = tok.getLongInt();
    } else {
      throw(GMANError(RIE_SYNTAX, RIE_ERROR, "Non-integer in array."));
    }
  }

  return array;
}

RtFloat* GMANRIBParse::TokenVector::toRtFloatArray() {
  RtFloat* array = new RtFloat[size()];

  for (unsigned int i = 0; i < size(); i++) {
    GMANToken tok = (*this)[i];
    if (tok.getType() == GMANToken::LONGINT) {
      array[i] = (RtFloat) tok.getLongInt();
    } else if (tok.getType() == GMANToken::REAL) {
      array[i] = tok.getReal();
    } else {
      throw(GMANError(RIE_SYNTAX, RIE_ERROR, "Non-number in array."));
    }
  }

  return array;
}
