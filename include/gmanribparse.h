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

class GMAN_EXPORT  GMANRIBParse : public UniversalSuperClass {
public: // types

  typedef RtBoolean (GMANRIBParse::*RIBHandler)(RtToken keyword, 
						GMANRIBTokenize &tokenizer);

  typedef std::map<RtToken, RIBHandler>              TokenHandlerMap;
  typedef std::map<RtInt, RtLightHandle>             LightHandleMap;
  typedef std::map<RtInt, RtObjectHandle>            ObjectHandleMap;

  class TokenVector : public std::vector<GMANToken> {
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

  std::ifstream		ribFile;

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
; // default constructor

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
  RtVoid parse(RtVoid);

private:
  RtVoid	addDefaultHandlers(RtVoid);

  const GMANToken &peekToken();
  const GMANToken &nextToken();

  // Utility functions
  RtFloat nextFloat();
  RtInt nextInt();
  char* copyStringToken();

  TokenVector parseArray(RtVoid);
  RtVoid parseParameterList(RtInt &n, RtToken* &tokens,
			    RtPointer* &params);
  RtVoid parseOption(RtVoid);
  RtVoid parseDisplay(RtVoid);
  RtVoid parseFormat(RtVoid);
  RtVoid parseProjection(RtVoid);
  RtVoid parseGeometricApproximation(RtVoid);
  RtVoid parseShadingInterpolation(RtVoid);
  RtVoid parseShadingRate(RtVoid);
  RtVoid parseOrientation(RtVoid);
  RtVoid parseReverseOrientation(RtVoid);
  RtVoid parsePixelSamples(RtVoid);
  RtVoid parseExposure(RtVoid);
  RtVoid parseDepthOfField(RtVoid);
  RtVoid parseShutter(RtVoid);
  RtVoid parseHider(RtVoid);
  RtVoid parseCropWindow(RtVoid);
  RtVoid parseScreenWindow(RtVoid);
  RtVoid parseClipping(RtVoid);
  RtVoid parseDeclare(RtVoid);
  RtVoid parseAttribute(RtVoid);
  RtVoid parseColor(RtVoid);
  RtVoid parseOpacity(RtVoid);
  RtVoid parseLightSource(RtVoid);
  RtVoid parseSurface(RtVoid);
  RtVoid parseCoordinateSystem(RtVoid);
  RtVoid parseIdentity(RtVoid);
  RtVoid parseTransform(RtVoid);
  RtVoid parseConcatTransform(RtVoid);
  RtVoid parseTranslate(RtVoid);
  RtVoid parseRotate(RtVoid);
  RtVoid parseScale(RtVoid);
  RtVoid parseSphere(RtVoid);
  RtVoid parseCone(RtVoid);
  RtVoid parseCylinder(RtVoid);
  RtVoid parseSides(RtVoid);
  RtVoid parseHyperboloid(RtVoid);
  RtVoid parseParaboloid(RtVoid);
  RtVoid parseTorus(RtVoid);
  RtVoid parseDisk(RtVoid);
  RtVoid parsePolygon(RtVoid);
  RtVoid parsePoints(RtVoid);
  RtVoid parsePointsPolygons(RtVoid);
  RtVoid parsePointsGeneralPolygons(RtVoid);
  RtVoid parsePatch(RtVoid);
  RtVoid parseNuPatch(RtVoid);
  RtVoid parsePatchMesh(RtVoid);
  RtVoid parseTextureCoordinates(RtVoid);
  RtVoid parseMotionBegin(RtVoid);
  RtVoid parseMotionEnd(RtVoid);
  RtVoid parseObjectBegin(RtVoid);
  RtVoid parseObjectEnd(RtVoid);
  RtVoid parseReadArchive(RtVoid);
  RtVoid parseObjectInstance(RtVoid);
  RtVoid parseBasis(RtVoid);
  RtVoid parseAtmosphere(RtVoid);
  RtVoid parseDisplacement(RtVoid);
  RtVoid parseImager(RtVoid);
  RtVoid parseIlluminate(RtVoid);
//  RtVoid parse(RtVoid);

protected:

  RIBHandler	defaultHandler;

};


#endif
