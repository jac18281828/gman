/* This is part of the GNU GMAN Library, a FREE implementation of the
 * RenderMan Interface Specification.
 *
 * Copyright (c) 2001, 2000, 1999 by John Cairns 
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
 

#ifndef __GMAN_GMANRIBPARSE_H
#define __GMAN_GMANRIBPARSE_H 1


/* Headers */

// STL
#include <map>
#include <vector>
#include <fstream>
#if HAVE_STD_NAMESPACE
using std::map;
#endif

// the renderman interface
#include "ri.h"
// the universal super class declaration
#include "universalsuperclass.h"
// GMAN Renderman state machine
#include "gmanrenderman.h"
// GMAN RIB tokenizer
#include "gmanribtokenize.h"
// GMAN Error
#include "gmanerror.h"

/*
 * RenderMan API gmanribparse
 *
 */

class GMANDLL  GMANRIBParse : public UniversalSuperClass {
public: // types

  typedef RtBoolean (GMANRIBParse::*RIBHandler)(RtToken keyword, 
						GMANRIBTokenize &tokenizer);

  typedef map<RtToken, RIBHandler>              TokenHandlerMap;
  typedef map<RtInt, RtLightHandle>             LightHandleMap;
  typedef map<RtInt, RtObjectHandle>            ObjectHandleMap;

  class TokenVector : public vector<GMANToken> {
  public:
    RtToken* toRtTokenArray();
    RtInt* toRtIntArray();
    RtFloat* toRtFloatArray();
  };

  /*
   * Global static data
   */

  /* The number of RIB keywords in KeywordTable */
  static  const int	nKeywords;

  /* A table of all RIB keywords/commands */
  static RtToken *KeywordTable;

private:
  bool			handlersRegistered;
  
  GMANRenderMan		*renderMan;

  ifstream		ribFile;
  
  GMANRIBTokenize	tokenizer;
  
  GMANToken currentToken;
  GMANToken lookAheadToken;

  TokenHandlerMap	tokenHandlerMap;

  LightHandleMap        lightHandleMap;
  ObjectHandleMap       objectHandleMap;

public:
  /** 
   * open the file rib, where name is the parameter to RiBegin
   */
  GMANRIBParse(GMANRenderMan *renderman, 
	       const char *rib, 
	       RtToken name="gmanzbuffer") 
    throw(GMANError); // default constructor

  virtual ~GMANRIBParse(); // default destructor


  /*
   * This method registers the indicated rib keyword token
   * with the specified handler method, if it is not currently
   * registered, and returns false if the keyword has been
   * registered in another context.
   */
  virtual RtBoolean addHandler(RtToken keyword, RIBHandler handler);

  /*
   * the decendant parser may provide the 'addHandlers' method 
   * this method registers all handlers provided by the parser
   * that have not already been registered
   */

  virtual RtVoid addHandlers(RtVoid) { 
  }

  /*
   * parse the rib file and take any specified action based on the rib stream.
   *
   * parse searches the rib input stream for rib keyword tokens and then
   * searches for a registered rib token handler 
   */
  RtVoid parse(RtVoid) throw(GMANError);

private:
  RtVoid	addDefaultHandlers(RtVoid);

  const GMANToken &peekToken();
  const GMANToken &nextToken();

  // Utility functions
  RtFloat nextFloat();
  RtInt nextInt();
  char* copyStringToken();

  TokenVector parseArray(RtVoid) throw(GMANError);
  RtVoid parseParameterList(RtInt &n, RtToken* &tokens,
			    RtPointer* &params) throw(GMANError);
  RtVoid parseOption(RtVoid) throw(GMANError);
  RtVoid parseDisplay(RtVoid) throw(GMANError);
  RtVoid parseFormat(RtVoid) throw(GMANError);
  RtVoid parseProjection(RtVoid) throw(GMANError);
  RtVoid parseGeometricApproximation(RtVoid) throw(GMANError);
  RtVoid parseShadingInterpolation(RtVoid) throw(GMANError);
  RtVoid parseShadingRate(RtVoid) throw(GMANError);
  RtVoid parseOrientation(RtVoid) throw(GMANError);
  RtVoid parseReverseOrientation(RtVoid) throw(GMANError);
  RtVoid parsePixelSamples(RtVoid) throw(GMANError);
  RtVoid parseExposure(RtVoid) throw(GMANError);
  RtVoid parseDepthOfField(RtVoid) throw(GMANError);
  RtVoid parseShutter(RtVoid) throw(GMANError);
  RtVoid parseHider(RtVoid) throw(GMANError);
  RtVoid parseCropWindow(RtVoid) throw(GMANError);
  RtVoid parseScreenWindow(RtVoid) throw(GMANError);
  RtVoid parseClipping(RtVoid) throw(GMANError);
  RtVoid parseDeclare(RtVoid) throw(GMANError);
  RtVoid parseAttribute(RtVoid) throw(GMANError);
  RtVoid parseColor(RtVoid) throw(GMANError);
  RtVoid parseOpacity(RtVoid) throw(GMANError);
  RtVoid parseLightSource(RtVoid) throw(GMANError);
  RtVoid parseSurface(RtVoid) throw(GMANError);
  RtVoid parseCoordinateSystem(RtVoid) throw(GMANError);
  RtVoid parseIdentity(RtVoid) throw(GMANError);
  RtVoid parseTransform(RtVoid) throw(GMANError);
  RtVoid parseConcatTransform(RtVoid) throw(GMANError);
  RtVoid parseTranslate(RtVoid) throw(GMANError);
  RtVoid parseRotate(RtVoid) throw(GMANError);
  RtVoid parseScale(RtVoid) throw(GMANError);
  RtVoid parseSphere(RtVoid) throw(GMANError);
  RtVoid parseCone(RtVoid) throw(GMANError);
  RtVoid parseCylinder(RtVoid) throw(GMANError);
  RtVoid parseSides(RtVoid) throw(GMANError);
  RtVoid parseHyperboloid(RtVoid) throw(GMANError);
  RtVoid parseParaboloid(RtVoid) throw(GMANError);
  RtVoid parseTorus(RtVoid) throw(GMANError);
  RtVoid parseDisk(RtVoid) throw(GMANError);
  RtVoid parsePolygon(RtVoid) throw(GMANError);
  RtVoid parsePoints(RtVoid) throw(GMANError);
  RtVoid parsePointsPolygons(RtVoid) throw(GMANError);
  RtVoid parsePointsGeneralPolygons(RtVoid) throw(GMANError);
  RtVoid parsePatch(RtVoid) throw(GMANError);
  RtVoid parseNuPatch(RtVoid) throw(GMANError);
  RtVoid parsePatchMesh(RtVoid) throw(GMANError);
  RtVoid parseTextureCoordinates(RtVoid) throw(GMANError);
  RtVoid parseMotionBegin(RtVoid) throw(GMANError);
  RtVoid parseMotionEnd(RtVoid) throw(GMANError);
  RtVoid parseObjectBegin(RtVoid) throw(GMANError);
  RtVoid parseObjectEnd(RtVoid) throw(GMANError);
  RtVoid parseReadArchive(RtVoid) throw(GMANError);
  RtVoid parseObjectInstance(RtVoid) throw(GMANError);
  RtVoid parseBasis(RtVoid) throw(GMANError);
  RtVoid parseAtmosphere(RtVoid) throw(GMANError);
  RtVoid parseDisplacement(RtVoid) throw(GMANError);
  RtVoid parseImager(RtVoid) throw(GMANError);
  RtVoid parseIlluminate(RtVoid) throw(GMANError);
//  RtVoid parse(RtVoid) throw(GMANError);

protected:

  RIBHandler	defaultHandler;
  
};


#endif
