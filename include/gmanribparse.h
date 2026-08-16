/* SPDX-License-Identifier: LGPL-2.1-or-later */

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


#ifndef __GMAN_GMANRIBPARSE_H
#define __GMAN_GMANRIBPARSE_H 1


/* Headers */

// STL
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>
#include <fstream>
#include <istream>

// the renderman interface
#include "ri.h"
// logging
#include "gmanlog.h"
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

class GMAN_EXPORT  GMANRIBParse {
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

  /* A RIB that includes itself, directly or through a chain of archives,
   * is a hang rather than a parse error without a depth cap. */
  static const int maxArchiveDepth;

private:
  bool			handlersRegistered;

  GMANRenderMan		*renderMan;

  // the token stream currently being read -- the top-level RIB file or,
  // while inside ReadArchive, the archive. Gzip'd sources are transparently
  // decompressed into an in-memory stream by openRibStream, so this is an
  // istream rather than an ifstream.
  std::unique_ptr<std::istream>	ribStream;

  // the directory each currently-open file (or archive) resolves its own
  // relative ReadArchive paths against, innermost last
  std::vector<std::string>	includeDirs;

  // absolute, resolved paths of every file currently open, for cycle
  // detection: a RIB that reads itself, directly or through a chain of
  // archives, errors cleanly instead of hanging
  std::vector<std::string>	openArchives;

  // request names skipped by skipUnknownRequest, reported once at end of
  // parse rather than once per occurrence
  std::set<std::string>	skippedRequests;

  // parseParameterList's own buffers (the flattened tokens/parms arrays,
  // each key, each value) outlive the parseXxx call that built them -- the
  // RiXxxV they are handed to either copies them into a GMANParameterList
  // immediately or ignores them (most Ri*V bodies are still stubs) -- but
  // nothing downstream takes ownership. freePendingParams releases them
  // once per dispatched request; not releasing them at all is what
  // LeakSanitizer catches on the very first RIB with real parameters.
  struct PendingParamValue {
    RtPointer value;
    bool isStringArray;
    unsigned int count; // element count, only meaningful when isStringArray
  };
  std::vector<char *>			pendingParamKeys;
  std::vector<PendingParamValue>	pendingParamValues;
  std::vector<RtToken *>		pendingTokenArrays;
  std::vector<RtPointer *>		pendingParmArrays;

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

  // Reads the next STRING token and hands back an owning copy. A
  // std::string's destructor runs on every unwind path -- including a
  // parameter list, or a sibling copyStringToken() call, throwing before
  // the caller reaches its own cleanup -- so nothing downstream of this
  // call can leak the token by forgetting to release it.
  std::string copyStringToken();

  TokenVector parseArray(RtVoid);
  RtVoid parseParameterList(RtInt &n, RtToken* &tokens,
			    RtPointer* &params);

  // the token-dispatch loop, run once for the top-level file and once more,
  // re-entrantly, for each nested ReadArchive
  RtVoid parseStream(RtVoid);

  // opens path, transparently decompressing it first if it is gzip'd
  // (detected by magic bytes, not by extension)
  static std::unique_ptr<std::istream> openRibStream(const std::string &path);

  // an unrecognized keyword: warn once per name, then consume its arguments
  // by balancing brackets so the token stream stays in sync, without
  // knowing the request's grammar
  RtVoid skipUnknownRequest(const std::string &name);

  // releases every buffer parseParameterList allocated for the request just
  // dispatched
  RtVoid freePendingParams(RtVoid);

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

  // RISpec 3.2 requests parsed and ignored -- see AGENTS.md's RIB support
  // table. None of these renders; each still has to consume its own
  // arguments correctly, or every request after it desyncs.
  RtVoid parseCurves(RtVoid);
  RtVoid parseBlobby(RtVoid);
  RtVoid parseSubdivisionMesh(RtVoid);
  RtVoid parseProcedural(RtVoid);
  RtVoid parseSolidBegin(RtVoid);
  RtVoid parseSolidEnd(RtVoid);
  RtVoid parseDetail(RtVoid);
  RtVoid parseDetailRange(RtVoid);
  RtVoid parseRelativeDetail(RtVoid);
  RtVoid parseSkew(RtVoid);
  RtVoid parseMatte(RtVoid);
  RtVoid parseTrimCurve(RtVoid);
  RtVoid parseErrorHandler(RtVoid);
  RtVoid parseArchiveRecord(RtVoid);
  RtVoid parseMakeTexture(RtVoid);
  RtVoid parseMakeBump(RtVoid);
  RtVoid parseMakeLatLongEnvironment(RtVoid);
  RtVoid parseMakeCubeFaceEnvironment(RtVoid);
  RtVoid parseMakeShadow(RtVoid);
  RtVoid parseIfBegin(RtVoid);
  RtVoid parseElseIf(RtVoid);
  RtVoid parseElse(RtVoid);
  RtVoid parseIfEnd(RtVoid);
//  RtVoid parse(RtVoid);

protected:

  RIBHandler	defaultHandler;

};


#endif
