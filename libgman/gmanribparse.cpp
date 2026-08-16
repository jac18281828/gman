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

/* Local Headers */
#include <cctype>    /* tolower */
#include <cstring>   /* strlen, strcpy, strcmp, memcpy -- libstdc++ does not
                      * pull these in transitively the way libc++ does */
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <system_error>

#include <zlib.h>

#include "ri.h"      /* RenderMan Interface */
#include "gmanlog.h"
#include "gmanribparse.h" /* Declaration Header */

namespace {

namespace fs = std::filesystem;

bool fileExists(const std::string &path) {
  std::error_code ec;
  return fs::exists(path, ec);
}

std::string dirName(const std::string &path) {
  fs::path p(path);
  fs::path dir = p.parent_path();
  return dir.empty() ? std::string(".") : dir.string();
}

std::string canonicalOrSelf(const std::string &path) {
  std::error_code ec;
  fs::path canonical = fs::canonical(path, ec);
  return ec ? path : canonical.string();
}

// RiBasis's five standard bases, looked up by RISpec name.
bool basisByName(const std::string &name, RtBasis &basis) {
  std::string lower = name;
  for (std::string::size_type i = 0; i < lower.size(); ++i) {
    lower[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(lower[i])));
  }
  const RtBasis *match = nullptr;
  if (lower == "bezier") {
    match = &RiBezierBasis;
  } else if (lower == "b-spline" || lower == "bspline") {
    match = &RiBSplineBasis;
  } else if (lower == "catmull-rom" || lower == "catmullrom") {
    match = &RiCatmullRomBasis;
  } else if (lower == "hermite") {
    match = &RiHermiteBasis;
  } else if (lower == "power") {
    match = &RiPowerBasis;
  }
  if (match == nullptr) {
    return false;
  }
  std::memcpy(basis, *match, sizeof(RtBasis));
  return true;
}

} // namespace

/*
 * Global static data
 *
 */
const int GMANRIBParse::nKeywords = 0;
RtToken   *GMANRIBParse::KeywordTable=NULL;

// A RIB that reads itself, directly or through a chain of archives, is a
// hang without a depth cap.
const int GMANRIBParse::maxArchiveDepth = 64;

/*
 * RenderMan API gmanribparse
 *
 */

// default constructor
GMANRIBParse::GMANRIBParse(GMANRenderMan *renderman,
			   const char *rib,
			   RtToken name)
 : handlersRegistered(false),
  renderMan(renderman),
  ribStream(openRibStream(rib))
{
  includeDirs.push_back(dirName(std::string(rib)));
  openArchives.push_back(canonicalOrSelf(std::string(rib)));

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

RtVoid  GMANRIBParse::parse(RtVoid) {

  // add all handlers
  if(!handlersRegistered) {
    addHandlers();

    // add default handlers
    addDefaultHandlers();

    handlersRegistered=true;
  }

  parseStream();

  // The set of requests recognized but never rendered, reported once here
  // rather than as a warning per occurrence -- a RIB with 40,000
  // PointsPolygons calls must not produce 40,000 warnings.
  if (! skippedRequests.empty()) {
    std::string list;
    for (std::set<std::string>::const_iterator it = skippedRequests.begin();
	 it != skippedRequests.end(); ++it) {
      if (! list.empty()) {
	list += ", ";
      }
      list += *it;
    }
    warning("RIB: unrecognized requests skipped: %s", list.c_str());
  }
}

RtVoid  GMANRIBParse::parseStream(RtVoid) {

  /* A GMANError thrown out of a request handler -- malformed parameter list,
   * bad token, unreadable archive -- unwinds past the end of the loop body,
   * so releasing the pending parameters there would miss every error path.
   * The guard runs on both. */
  struct PendingParamGuard {
    GMANRIBParse *parser;
    ~PendingParamGuard() { parser->freePendingParams(); }
  };

  while(true) {
    const GMANToken &tok = nextToken();

    if (tok.getType() == GMANToken::END_OF_FILE) {
      break;
    }

    PendingParamGuard guard{this};

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
    case GMANToken::RI_CURVES:
      debug("Keyword token: Curves (parse-only)");
      parseCurves();
      break;
    case GMANToken::RI_BLOBBY:
      debug("Keyword token: Blobby (parse-only)");
      parseBlobby();
      break;
    case GMANToken::RI_SUBDIVISION_MESH:
      debug("Keyword token: SubdivisionMesh (parse-only)");
      parseSubdivisionMesh();
      break;
    case GMANToken::RI_PROCEDURAL:
      debug("Keyword token: Procedural (parse-only)");
      parseProcedural();
      break;
    case GMANToken::RI_SOLID_BEGIN:
      debug("Keyword token: SolidBegin (parse-only)");
      parseSolidBegin();
      break;
    case GMANToken::RI_SOLID_END:
      debug("Keyword token: SolidEnd (parse-only)");
      parseSolidEnd();
      break;
    case GMANToken::RI_DETAIL:
      debug("Keyword token: Detail (parse-only)");
      parseDetail();
      break;
    case GMANToken::RI_DETAIL_RANGE:
      debug("Keyword token: DetailRange (parse-only)");
      parseDetailRange();
      break;
    case GMANToken::RI_RELATIVE_DETAIL:
      debug("Keyword token: RelativeDetail (parse-only)");
      parseRelativeDetail();
      break;
    case GMANToken::RI_SKEW:
      debug("Keyword token: Skew (parse-only)");
      parseSkew();
      break;
    case GMANToken::RI_MATTE:
      debug("Keyword token: Matte (parse-only)");
      parseMatte();
      break;
    case GMANToken::RI_TRIM_CURVE:
      debug("Keyword token: TrimCurve (parse-only)");
      parseTrimCurve();
      break;
    case GMANToken::RI_ERROR_HANDLER:
      debug("Keyword token: ErrorHandler (parse-only)");
      parseErrorHandler();
      break;
    case GMANToken::RI_ARCHIVE_RECORD:
      debug("Keyword token: ArchiveRecord (parse-only)");
      parseArchiveRecord();
      break;
    case GMANToken::RI_MAKE_TEXTURE:
      debug("Keyword token: MakeTexture (parse-only)");
      parseMakeTexture();
      break;
    case GMANToken::RI_MAKE_BUMP:
      debug("Keyword token: MakeBump (parse-only)");
      parseMakeBump();
      break;
    case GMANToken::RI_MAKE_LAT_LONG_ENVIRONMENT:
      debug("Keyword token: MakeLatLongEnvironment (parse-only)");
      parseMakeLatLongEnvironment();
      break;
    case GMANToken::RI_MAKE_CUBE_FACE_ENVIRONMENT:
      debug("Keyword token: MakeCubeFaceEnvironment (parse-only)");
      parseMakeCubeFaceEnvironment();
      break;
    case GMANToken::RI_MAKE_SHADOW:
      debug("Keyword token: MakeShadow (parse-only)");
      parseMakeShadow();
      break;
    case GMANToken::RI_IF_BEGIN:
      debug("Keyword token: IfBegin (parse-only)");
      parseIfBegin();
      break;
    case GMANToken::RI_ELSE_IF:
      debug("Keyword token: ElseIf (parse-only)");
      parseElseIf();
      break;
    case GMANToken::RI_ELSE:
      debug("Keyword token: Else (parse-only)");
      parseElse();
      break;
    case GMANToken::RI_IF_END:
      debug("Keyword token: IfEnd (parse-only)");
      parseIfEnd();
      break;
    case GMANToken::RI_UNKNOWN_REQUEST:
      skipUnknownRequest(tok.getString());
      break;
    default:
      debug("Unknown token type: %i", tok.getType());
      GMANError error(RIE_BADFILE,RIE_WARNING,"Unknown token in ribfile");
      throw(error);
      break;
    }

    // Whatever this request's own RiXxxV call needed from its parameter
    // list, it took by the time control returns here -- either copied into
    // a GMANParameterList, or (most Ri*V bodies are still stubs) not at
    // all. PendingParamGuard releases them as the iteration ends, by either
    // path.
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

  const char *str = tok.getString().c_str();
  char *retval = new char[strlen(str) + 1];
  strcpy(retval, str);
  return retval;
}

RtVoid  GMANRIBParse::parseOption(RtVoid) {
  char *name = copyStringToken();

  RtInt n = 0;
  RtToken* tokens;
  RtPointer* parms;

  parseParameterList(n, tokens, parms);

  renderMan->RiOptionV(name, n, tokens, parms);

  delete [] name;

}

RtVoid  GMANRIBParse::parseDisplay(RtVoid) {
  char *name = copyStringToken();
  char *type = copyStringToken();
  char *mode = copyStringToken();

  RtInt n = 0;
  RtToken* tokens;
  RtPointer* parms;

  parseParameterList(n, tokens, parms);

  renderMan->RiDisplayV(name, type, mode, n, tokens, parms);

  delete [] name;
  delete [] type;
  delete [] mode;

}

RtVoid  GMANRIBParse::parseFormat(RtVoid) {
  RtInt xresolution = nextInt();
  RtInt yresolution = nextInt();
  RtFloat pixelaspectratio = nextFloat();

  renderMan->RiFormat(xresolution, yresolution, pixelaspectratio);
}

RtVoid GMANRIBParse::parseProjection(RtVoid) {
  char *name = copyStringToken();

  RtInt n = 0;
  RtToken* tokens;
  RtPointer* parms;

  parseParameterList(n, tokens, parms);

  renderMan->RiProjectionV(name, n, tokens, parms);

  delete [] name;
}

RtVoid GMANRIBParse::parseGeometricApproximation(RtVoid) {
  char *type = copyStringToken();
  RtFloat value = nextFloat();

  renderMan->RiGeometricApproximation(type, value);

  delete [] type;
}

RtVoid GMANRIBParse::parseShadingInterpolation(RtVoid) {

  RtToken type = copyStringToken();

  renderMan->RiShadingInterpolation(type);

  delete [] type;
}

RtVoid GMANRIBParse::parseShadingRate(RtVoid) {

  RtFloat size = nextFloat();

  renderMan->RiShadingRate(size);
}

RtVoid GMANRIBParse::parseOrientation(RtVoid) {

  RtToken orientation = copyStringToken();

  renderMan->RiOrientation(orientation);

  delete [] orientation;
}

RtVoid GMANRIBParse::parsePixelSamples(RtVoid) {

  RtFloat xsamples = nextFloat();
  RtFloat ysamples = nextFloat();

  renderMan->RiPixelSamples(xsamples, ysamples);
}

RtVoid GMANRIBParse::parseExposure(RtVoid) {

  RtFloat gain = nextFloat();
  RtFloat gamma = nextFloat();

  renderMan->RiExposure(gain, gamma);
}

RtVoid GMANRIBParse::parseDepthOfField(RtVoid) {

  RtFloat fstop = nextFloat();
  RtFloat focallength = nextFloat();
  RtFloat focaldistance = nextFloat();

  renderMan->RiDepthOfField(fstop, focallength, focaldistance);
}

RtVoid GMANRIBParse::parseShutter(RtVoid) {

  RtFloat min = nextFloat();
  RtFloat max = nextFloat();

  renderMan->RiShutter(min, max);
}

RtVoid GMANRIBParse::parseHider(RtVoid) {

  char *type = copyStringToken();

  RtInt n = 0;
  RtToken* tokens;
  RtPointer* parms;

  parseParameterList(n, tokens, parms);

  renderMan->RiHiderV(type, n, tokens, parms);
}

RtVoid GMANRIBParse::parseCropWindow(RtVoid) {

  RtFloat xmin = nextFloat();
  RtFloat xmax = nextFloat();
  RtFloat ymin = nextFloat();
  RtFloat ymax = nextFloat();

  renderMan->RiCropWindow(xmin, xmax, ymin, ymax);
}

RtVoid GMANRIBParse::parseScreenWindow(RtVoid) {

  RtFloat left = nextFloat();
  RtFloat right = nextFloat();
  RtFloat bottom = nextFloat();
  RtFloat top = nextFloat();

  renderMan->RiScreenWindow(left, right, bottom, top);
}

RtVoid GMANRIBParse::parseClipping(RtVoid) {

  RtFloat nearDist = nextFloat();
  RtFloat farDist = nextFloat();

  renderMan->RiClipping(nearDist, farDist);
}

RtVoid GMANRIBParse::parseDeclare(RtVoid) {

  RtToken name = copyStringToken();
  RtToken declaration = copyStringToken();

  renderMan->RiDeclare(name, declaration);

  delete [] name;
  delete [] declaration;
}

RtVoid GMANRIBParse::parseAttribute(RtVoid) {

  RtToken name = copyStringToken();

  RtInt n = 0;
  RtToken* tokens;
  RtPointer* parms;

  parseParameterList(n, tokens, parms);

  renderMan->RiAttributeV(name, n, tokens, parms);

  delete [] name;
}

RtVoid GMANRIBParse::parseColor(RtVoid) {
  int number = 3;
  //FIXME: Should actually get number from options
  //renderMan->getGraphicsState().getOptions().getColorSamples().getNumber();

  RtFloat *color = new RtFloat[number];
  const GMANToken &lookAhead = peekToken();
  if (lookAhead.getType() == GMANToken::LEFT_BRACKET) {
    GMANRIBParse::TokenVector tokenVector = parseArray();
    RtFloat *values = tokenVector.toRtFloatArray();
    int count = number < (int) tokenVector.size() ? number : (int) tokenVector.size();
    int i;
    for (i = 0; i < count; i++) {
      color[i] = values[i];
    }
    for (; i < number; i++) {
      color[i] = 0.0;
    }
    delete [] values;
  } else {
    for (int i = 0; i < number; i++) {
      color[i] = nextFloat();
    }
  }

  renderMan->RiColor(color);
  if(color) delete[]color;
}

RtVoid GMANRIBParse::parseOpacity(RtVoid) {
  int number = 3;
  //FIXME: Should actually get number from options
  //renderMan->getGraphicsState().getOptions().getColorSamples().getNumber();

  RtFloat *color=new RtFloat[number];
  const GMANToken &lookAhead = peekToken();
  if (lookAhead.getType() == GMANToken::LEFT_BRACKET) {
    GMANRIBParse::TokenVector tokenVector = parseArray();
    RtFloat *values = tokenVector.toRtFloatArray();
    int count = number < (int) tokenVector.size() ? number : (int) tokenVector.size();
    int i;
    for (i = 0; i < count; i++) {
      color[i] = values[i];
    }
    for (; i < number; i++) {
      color[i] = 0.0;
    }
    delete [] values;
  } else {
    for (int i = 0; i < number; i++) {
      color[i] = nextFloat();
    }
  }

  renderMan->RiOpacity(color);
  if(color) delete [] color;
}

RtVoid GMANRIBParse::parseLightSource(RtVoid) {
  char *shadername = copyStringToken();
  int sequence = nextInt();

  RtInt n = 0;
  RtToken* tokens;
  RtPointer* parms;

  parseParameterList(n, tokens, parms);

  RtLightHandle handle =
    renderMan->RiLightSourceV(shadername, n, tokens, parms);

  lightHandleMap[sequence] = handle;

  delete [] shadername;
}

RtVoid GMANRIBParse::parseSurface(RtVoid) {
  RtToken shadername = copyStringToken();

  RtInt n = 0;
  RtToken* tokens;
  RtPointer* parms;

  parseParameterList(n, tokens, parms);

  // FIXME: Implement some surface shaders
  renderMan->RiSurfaceV(shadername, n, tokens, parms);

  delete [] shadername;
}

RtVoid GMANRIBParse::parseCoordinateSystem(RtVoid) {

  char* name = copyStringToken();

  renderMan->RiCoordinateSystem(name);

  delete [] name;
}

RtVoid GMANRIBParse::parseTransform(RtVoid) {
  GMANRIBParse::TokenVector tokenVector = parseArray();
  if (tokenVector.size() != 16) {
    GMANError error(RIE_SYNTAX, RIE_ERROR,
		     "GMANRIBParse: Transform matrix must have 16 elements");
    throw error;
  }
  RtFloat *values = tokenVector.toRtFloatArray();

  RtMatrix transform;
  for (int i = 0; i < 4; i++) {
    for (int j = 0; j < 4; j++) {
      transform[i][j] = values[i * 4 + j];
    }
  }
  delete [] values;

  renderMan->RiTransform(transform);
}

RtVoid GMANRIBParse::parseConcatTransform(RtVoid) {
  GMANRIBParse::TokenVector tokenVector = parseArray();
  if (tokenVector.size() != 16) {
    GMANError error(RIE_SYNTAX, RIE_ERROR,
		     "GMANRIBParse: ConcatTransform matrix must have 16 elements");
    throw error;
  }
  RtFloat *values = tokenVector.toRtFloatArray();

  RtMatrix transform;
  for (int i = 0; i < 4; i++) {
    for (int j = 0; j < 4; j++) {
      transform[i][j] = values[i * 4 + j];
    }
  }
  delete [] values;

  renderMan->RiConcatTransform(transform);
}

RtVoid GMANRIBParse::parseTranslate(RtVoid) {
  RtFloat dx = nextFloat();
  RtFloat dy = nextFloat();
  RtFloat dz = nextFloat();

  renderMan->RiTranslate(dx, dy, dz);
}

RtVoid GMANRIBParse::parseRotate(RtVoid) {
  RtFloat angle = nextFloat();
  RtFloat dx = nextFloat();
  RtFloat dy = nextFloat();
  RtFloat dz = nextFloat();

  renderMan->RiRotate(angle, dx, dy, dz);
}

RtVoid GMANRIBParse::parseScale(RtVoid) {
  RtFloat sx = nextFloat();
  RtFloat sy = nextFloat();
  RtFloat sz = nextFloat();

  renderMan->RiScale(sx, sy, sz);
}

RtVoid GMANRIBParse::parseSphere(RtVoid) {
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

RtVoid GMANRIBParse::parseCone(RtVoid) {
  RtFloat height = nextFloat();
  RtFloat radius = nextFloat();
  RtFloat thetamax = nextFloat();

  RtInt n = 0;
  RtToken* tokens;
  RtPointer* parms;

  parseParameterList(n, tokens, parms);

  renderMan->RiConeV(height, radius, thetamax, n, tokens, parms);
}

RtVoid GMANRIBParse::parseCylinder(RtVoid) {
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

RtVoid GMANRIBParse::parseSides(RtVoid) {
  RtInt sides = nextInt();

  renderMan->RiSides(sides);
}

RtVoid GMANRIBParse::parseHyperboloid(RtVoid) {
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

RtVoid GMANRIBParse::parseParaboloid(RtVoid) {
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

RtVoid GMANRIBParse::parseTorus(RtVoid) {
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

RtVoid GMANRIBParse::parseDisk(RtVoid) {
  RtFloat height = nextFloat();
  RtFloat radius = nextFloat();
  RtFloat thetamax = nextFloat();

  RtInt n = 0;
  RtToken* tokens;
  RtPointer* parms;

  parseParameterList(n, tokens, parms);

  renderMan->RiDiskV(height, radius, thetamax, n, tokens, parms);
}

RtVoid GMANRIBParse::parsePolygon(RtVoid) {

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

RtVoid GMANRIBParse::parsePoints(RtVoid) {

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

RtVoid GMANRIBParse::parsePointsPolygons(RtVoid) {

  // nvertices[] and vertices[] are consumed correctly below so the token
  // stream stays in sync; RiPointsPolygonsV itself is an unimplemented stub
  // (rendering this request is out of Phase 2's scope), so building typed
  // arrays for it would have no consumer.
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

RtVoid GMANRIBParse::parsePointsGeneralPolygons(RtVoid) {

  // See parsePointsPolygons: the three arrays are consumed correctly to
  // keep the stream in sync; RiPointsGeneralPolygonsV is an unimplemented
  // stub.
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

RtVoid GMANRIBParse::parsePatch(RtVoid) {

  char *type = copyStringToken();

  RtInt n = 0;
  RtToken* tokens;
  RtPointer* parms;

  parseParameterList(n, tokens, parms);

  renderMan->RiPatchV(type, n, tokens, parms);

  delete [] type;
}

RtVoid GMANRIBParse::parseNuPatch(RtVoid) {

  RtInt nu = nextInt();
  RtInt uorder = nextInt();
  GMANRIBParse::TokenVector uknotVector = parseArray();
  RtFloat umin = nextFloat();
  RtFloat umax = nextFloat();
  RtInt nv = nextInt();
  RtInt vorder = nextInt();
  GMANRIBParse::TokenVector vknotVector = parseArray();
  RtFloat vmin = nextFloat();
  RtFloat vmax = nextFloat();

  RtInt n = 0;
  RtToken* tokens;
  RtPointer* parms;

  parseParameterList(n, tokens, parms);

  RtFloat *uknot = uknotVector.toRtFloatArray();
  RtFloat *vknot = vknotVector.toRtFloatArray();

  // RiNuPatchV is an unimplemented stub (evaluating a NURBS surface is out
  // of Phase 2's scope); the knot arrays are built and freed here so the
  // call, if ever wired up, sees real data rather than NULL.
  renderMan->RiNuPatchV(nu, uorder, uknot, umin, umax, nv, vorder, vknot,
			vmin, vmax,
			n, tokens, parms);

  delete [] uknot;
  delete [] vknot;
}

RtVoid GMANRIBParse::parsePatchMesh(RtVoid) {

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

  delete [] type;
  delete [] uwrap;
  delete [] vwrap;
}

RtVoid GMANRIBParse::parseTextureCoordinates(RtVoid) {
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

RtVoid GMANRIBParse::parseReadArchive(RtVoid) {

  char* name = copyStringToken();
  std::string requested(name);
  delete [] name;

  // Resolve relative to the including file first, then fall back to the
  // path as given (relative to the process's working directory). There is
  // no wired RiOption "searchpath" "archive" to consult beyond that --
  // RiOptionV is an unwired stub outside this phase's scope (gmanrendermanimpl.cpp).
  std::string resolved;
  fs::path requestedPath(requested);
  if (requestedPath.is_absolute()) {
    resolved = requested;
  } else {
    std::string candidate = includeDirs.empty()
      ? requested
      : includeDirs.back() + "/" + requested;
    resolved = fileExists(candidate) ? candidate : requested;
  }

  if (! fileExists(resolved)) {
    GMANError error(RIE_NOFILE, RIE_ERROR,
		     ("GMANRIBParse: cannot find archive \"" + requested +
		      "\"").c_str());
    throw error;
  }

  const std::string canonicalPath = canonicalOrSelf(resolved);

  if (static_cast<int>(openArchives.size()) >= maxArchiveDepth) {
    GMANError error(RIE_LIMIT, RIE_ERROR,
		     "GMANRIBParse: ReadArchive nesting too deep "
		     "(possible cycle)");
    throw error;
  }
  for (std::vector<std::string>::const_iterator it = openArchives.begin();
       it != openArchives.end(); ++it) {
    if (*it == canonicalPath) {
      GMANError error(RIE_LIMIT, RIE_ERROR,
		       ("GMANRIBParse: ReadArchive cycle at \"" + requested +
			"\"").c_str());
      throw error;
    }
  }

  std::unique_ptr<std::istream> savedStream = std::move(ribStream);
  const GMANToken savedLookAhead = lookAheadToken;
  lookAheadToken = GMANToken();

  ribStream = openRibStream(resolved);
  openArchives.push_back(canonicalPath);
  includeDirs.push_back(dirName(canonicalPath));

  try {
    parseStream();
  } catch (GMANError &) {
    includeDirs.pop_back();
    openArchives.pop_back();
    ribStream = std::move(savedStream);
    lookAheadToken = savedLookAhead;
    throw;
  }

  includeDirs.pop_back();
  openArchives.pop_back();
  ribStream = std::move(savedStream);
  lookAheadToken = savedLookAhead;
}

RtVoid GMANRIBParse::parseMotionBegin(RtVoid) {
  GMANRIBParse::TokenVector tokenVector = parseArray();

  int n = tokenVector.size();
  RtFloat* times = tokenVector.toRtFloatArray();

  renderMan->RiMotionBeginV(n, times);

  delete [] times;
}

RtVoid GMANRIBParse::parseMotionEnd(RtVoid) {
  renderMan->RiMotionEnd();
}

RtVoid GMANRIBParse::parseObjectBegin(RtVoid) {

  int sequence = nextInt();

  RtObjectHandle handle = renderMan->RiObjectBegin();
  objectHandleMap[sequence] = handle;
}

RtVoid GMANRIBParse::parseObjectEnd(RtVoid) {
  renderMan->RiObjectEnd();
}

RtVoid GMANRIBParse::parseObjectInstance(RtVoid) {

  int sequence = nextInt();

  RtObjectHandle handle = objectHandleMap[sequence];

  renderMan->RiObjectInstance(handle);
}

RtVoid GMANRIBParse::parseBasis(RtVoid) {

  RtBasis ubasis;
  RtBasis vbasis;

  const GMANToken &uLook = peekToken();
  if (uLook.getType() == GMANToken::LEFT_BRACKET) {
    GMANRIBParse::TokenVector tokenVector = parseArray();
    if (tokenVector.size() != 16) {
      GMANError error(RIE_SYNTAX, RIE_ERROR,
		       "GMANRIBParse: inline basis matrix must have 16 elements");
      throw error;
    }
    RtFloat *values = tokenVector.toRtFloatArray();
    for (int i = 0; i < 4; i++) {
      for (int j = 0; j < 4; j++) {
	ubasis[i][j] = values[i * 4 + j];
      }
    }
    delete [] values;
  } else {
    char *uname = copyStringToken();
    bool found = basisByName(uname, ubasis);
    if (! found) {
      std::string msg = std::string("GMANRIBParse: unknown basis \"") +
	uname + "\"";
      delete [] uname;
      GMANError error(RIE_BADTOKEN, RIE_ERROR, msg.c_str());
      throw error;
    }
    delete [] uname;
  }
  RtInt ustep = nextInt();

  const GMANToken &vLook = peekToken();
  if (vLook.getType() == GMANToken::LEFT_BRACKET) {
    GMANRIBParse::TokenVector tokenVector = parseArray();
    if (tokenVector.size() != 16) {
      GMANError error(RIE_SYNTAX, RIE_ERROR,
		       "GMANRIBParse: inline basis matrix must have 16 elements");
      throw error;
    }
    RtFloat *values = tokenVector.toRtFloatArray();
    for (int i = 0; i < 4; i++) {
      for (int j = 0; j < 4; j++) {
	vbasis[i][j] = values[i * 4 + j];
      }
    }
    delete [] values;
  } else {
    char *vname = copyStringToken();
    bool found = basisByName(vname, vbasis);
    if (! found) {
      std::string msg = std::string("GMANRIBParse: unknown basis \"") +
	vname + "\"";
      delete [] vname;
      GMANError error(RIE_BADTOKEN, RIE_ERROR, msg.c_str());
      throw error;
    }
    delete [] vname;
  }
  RtInt vstep = nextInt();

  renderMan->RiBasis(ubasis, ustep, vbasis, vstep);
}

RtVoid GMANRIBParse::parseAtmosphere(RtVoid) {

  char* name = copyStringToken();

  RtInt n = 0;
  RtToken* tokens;
  RtPointer* parms;

  parseParameterList(n, tokens, parms);

  // FIXME: implement some atmosphere shaders
  //  renderMan->RiAtmosphereV(name, n, tokens, parms);

  delete [] name;
}

RtVoid GMANRIBParse::parseDisplacement(RtVoid) {

  char* name = copyStringToken();

  RtInt n = 0;
  RtToken* tokens;
  RtPointer* parms;

  parseParameterList(n, tokens, parms);

  // FIXME: implement some displacement shaders
  //  renderMan->RiDisplacementV(name, n, tokens, parms);

  delete [] name;
}

RtVoid GMANRIBParse::parseImager(RtVoid) {

  char* name = copyStringToken();

  RtInt n = 0;
  RtToken* tokens;
  RtPointer* parms;

  parseParameterList(n, tokens, parms);

  // FIXME: implement some imagers
  //renderMan->RiImagerV(name, n, tokens, parms);

  delete [] name;
}

RtVoid GMANRIBParse::parseIlluminate(RtVoid) {

  int sequence = nextInt();
  int onoff = nextInt();

  RtLightHandle handle = lightHandleMap[sequence];
  renderMan->RiIlluminate(handle, onoff==0?false:true);
}

// **************************************************************
// RISpec 3.2 requests parsed and ignored. Each consumes exactly its own
// grammar so the token stream stays in sync for whatever follows; none
// calls into renderMan, since rendering these is Scope C (SPEC.md S4),
// not Phase 2.
// **************************************************************

RtVoid GMANRIBParse::parseCurves(RtVoid) {
  // Curves type ncurves[] wrap paramlist
  char *type = copyStringToken();
  GMANRIBParse::TokenVector ncurves = parseArray();
  char *wrap = copyStringToken();

  RtInt n = 0;
  RtToken *tokens;
  RtPointer *parms;
  parseParameterList(n, tokens, parms);

  delete [] type;
  delete [] wrap;
}

RtVoid GMANRIBParse::parseBlobby(RtVoid) {
  // Blobby nleaf code[] float[] str[] paramlist
  nextInt(); // nleaf
  GMANRIBParse::TokenVector code = parseArray();
  GMANRIBParse::TokenVector floats = parseArray();
  GMANRIBParse::TokenVector strs = parseArray();

  RtInt n = 0;
  RtToken *tokens;
  RtPointer *parms;
  parseParameterList(n, tokens, parms);
}

RtVoid GMANRIBParse::parseSubdivisionMesh(RtVoid) {
  // SubdivisionMesh scheme nvertices[] vertices[] tags[] nargs[] intargs[] floatargs[] paramlist
  char *scheme = copyStringToken();
  GMANRIBParse::TokenVector nvertices = parseArray();
  GMANRIBParse::TokenVector vertices = parseArray();
  GMANRIBParse::TokenVector tags = parseArray();
  GMANRIBParse::TokenVector nargs = parseArray();
  GMANRIBParse::TokenVector intargs = parseArray();
  GMANRIBParse::TokenVector floatargs = parseArray();

  RtInt n = 0;
  RtToken *tokens;
  RtPointer *parms;
  parseParameterList(n, tokens, parms);

  delete [] scheme;
}

RtVoid GMANRIBParse::parseProcedural(RtVoid) {
  // Procedural procname procargs[] bound[6] -- no trailing paramlist
  char *procname = copyStringToken();
  GMANRIBParse::TokenVector procargs = parseArray();
  GMANRIBParse::TokenVector bound = parseArray();

  delete [] procname;
}

RtVoid GMANRIBParse::parseSolidBegin(RtVoid) {
  // SolidBegin "type"
  char *type = copyStringToken();
  delete [] type;
}

RtVoid GMANRIBParse::parseSolidEnd(RtVoid) {
  // SolidEnd -- no arguments
}

RtVoid GMANRIBParse::parseDetail(RtVoid) {
  // Detail [bound(6 floats)]
  GMANRIBParse::TokenVector bound = parseArray();
}

RtVoid GMANRIBParse::parseDetailRange(RtVoid) {
  // DetailRange minvis lowtran uptran maxvis
  nextFloat();
  nextFloat();
  nextFloat();
  nextFloat();
}

RtVoid GMANRIBParse::parseRelativeDetail(RtVoid) {
  // RelativeDetail relativedetail
  nextFloat();
}

RtVoid GMANRIBParse::parseSkew(RtVoid) {
  // Skew angle dx1 dy1 dz1 dx2 dy2 dz2
  for (int i = 0; i < 7; i++) {
    nextFloat();
  }
}

RtVoid GMANRIBParse::parseMatte(RtVoid) {
  // Matte onoff
  nextInt();
}

RtVoid GMANRIBParse::parseTrimCurve(RtVoid) {
  // TrimCurve nloops ncurves[] order[] knot[] min[] max[] n[] u[] v[] w[]
  nextInt(); // nloops
  GMANRIBParse::TokenVector ncurves = parseArray();
  GMANRIBParse::TokenVector order = parseArray();
  GMANRIBParse::TokenVector knot = parseArray();
  GMANRIBParse::TokenVector minv = parseArray();
  GMANRIBParse::TokenVector maxv = parseArray();
  GMANRIBParse::TokenVector npts = parseArray();
  GMANRIBParse::TokenVector u = parseArray();
  GMANRIBParse::TokenVector v = parseArray();
  GMANRIBParse::TokenVector w = parseArray();
}

RtVoid GMANRIBParse::parseErrorHandler(RtVoid) {
  // ErrorHandler "handler"
  char *handler = copyStringToken();
  delete [] handler;
}

RtVoid GMANRIBParse::parseArchiveRecord(RtVoid) {
  // ArchiveRecord "type" "text"
  char *type = copyStringToken();
  char *text = copyStringToken();
  delete [] type;
  delete [] text;
}

RtVoid GMANRIBParse::parseMakeTexture(RtVoid) {
  // MakeTexture picture texture swrap twrap filter swidth twidth paramlist
  char *picture = copyStringToken();
  char *texture = copyStringToken();
  char *swrap = copyStringToken();
  char *twrap = copyStringToken();
  char *filter = copyStringToken();
  nextFloat();
  nextFloat();

  RtInt n = 0;
  RtToken *tokens;
  RtPointer *parms;
  parseParameterList(n, tokens, parms);

  delete [] picture;
  delete [] texture;
  delete [] swrap;
  delete [] twrap;
  delete [] filter;
}

RtVoid GMANRIBParse::parseMakeBump(RtVoid) {
  // Same grammar as MakeTexture.
  char *picture = copyStringToken();
  char *texture = copyStringToken();
  char *swrap = copyStringToken();
  char *twrap = copyStringToken();
  char *filter = copyStringToken();
  nextFloat();
  nextFloat();

  RtInt n = 0;
  RtToken *tokens;
  RtPointer *parms;
  parseParameterList(n, tokens, parms);

  delete [] picture;
  delete [] texture;
  delete [] swrap;
  delete [] twrap;
  delete [] filter;
}

RtVoid GMANRIBParse::parseMakeLatLongEnvironment(RtVoid) {
  // MakeLatLongEnvironment picture texture filter swidth twidth paramlist
  char *picture = copyStringToken();
  char *texture = copyStringToken();
  char *filter = copyStringToken();
  nextFloat();
  nextFloat();

  RtInt n = 0;
  RtToken *tokens;
  RtPointer *parms;
  parseParameterList(n, tokens, parms);

  delete [] picture;
  delete [] texture;
  delete [] filter;
}

RtVoid GMANRIBParse::parseMakeCubeFaceEnvironment(RtVoid) {
  // MakeCubeFaceEnvironment px nx py ny pz nz texture fov filter swidth twidth paramlist
  char *px = copyStringToken();
  char *nx = copyStringToken();
  char *py = copyStringToken();
  char *ny = copyStringToken();
  char *pz = copyStringToken();
  char *nz = copyStringToken();
  char *texture = copyStringToken();
  nextFloat(); // fov
  char *filter = copyStringToken();
  nextFloat();
  nextFloat();

  RtInt n = 0;
  RtToken *tokens;
  RtPointer *parms;
  parseParameterList(n, tokens, parms);

  delete [] px;
  delete [] nx;
  delete [] py;
  delete [] ny;
  delete [] pz;
  delete [] nz;
  delete [] texture;
  delete [] filter;
}

RtVoid GMANRIBParse::parseMakeShadow(RtVoid) {
  // MakeShadow picture texture paramlist
  char *picture = copyStringToken();
  char *texture = copyStringToken();

  RtInt n = 0;
  RtToken *tokens;
  RtPointer *parms;
  parseParameterList(n, tokens, parms);

  delete [] picture;
  delete [] texture;
}

RtVoid GMANRIBParse::parseIfBegin(RtVoid) {
  // IfBegin "expression"
  char *expr = copyStringToken();
  delete [] expr;
}

RtVoid GMANRIBParse::parseElseIf(RtVoid) {
  // ElseIf "expression"
  char *expr = copyStringToken();
  delete [] expr;
}

RtVoid GMANRIBParse::parseElse(RtVoid) {
  // Else -- no arguments
}

RtVoid GMANRIBParse::parseIfEnd(RtVoid) {
  // IfEnd -- no arguments
}

RtVoid GMANRIBParse::skipUnknownRequest(const std::string &name) {
  if (skippedRequests.insert(name).second) {
    warning("RIB: skipping unrecognized request \"%s\"", name.c_str());
  }

  // Consume everything up to the next request we do recognize, balancing
  // brackets so a parameter list spread across hundreds of lines does not
  // fool this into stopping early.
  int depth = 0;
  while (true) {
    const GMANToken &tok = peekToken();
    GMANToken::TokenType type = tok.getType();

    if (type == GMANToken::END_OF_FILE) {
      break;
    }
    if (depth == 0 &&
	type != GMANToken::LEFT_BRACKET &&
	type != GMANToken::STRING &&
	type != GMANToken::REAL &&
	type != GMANToken::LONGINT) {
      break;
    }

    const GMANToken consumed = nextToken();
    if (consumed.getType() == GMANToken::LEFT_BRACKET) {
      ++depth;
    } else if (consumed.getType() == GMANToken::RIGHT_BRACKET) {
      if (depth > 0) {
	--depth;
      }
    }
  }
}

GMANRIBParse::TokenVector GMANRIBParse::parseArray(RtVoid) {
  GMANToken tok = nextToken(); // '['

  TokenVector tokens;

  while (true) {
    tok = nextToken();
    if (tok.getType() == GMANToken::RIGHT_BRACKET) {
      break;
    }
    if (tok.getType() == GMANToken::END_OF_FILE) {
      GMANError error(RIE_SYNTAX, RIE_ERROR,
		       "GMANRIBParse: unterminated array (missing ']')");
      throw error;
    }
    tokens.insert(tokens.end(), tok);
  }

  return tokens;
}

RtVoid GMANRIBParse::parseParameterList(RtInt &n, RtToken* &tokens,
					RtPointer* &parms) {
  typedef std::map<RtToken, RtPointer> ParamMap;
  ParamMap paramMap;

  while (true) {
    const GMANToken &tok = peekToken();
    if (tok.getType() != GMANToken::STRING) {
      //debug("While parsing parameter list, got token type: "
      //     << tok.getType());
      break;
    }

    // The map key must outlive this loop iteration -- it is read back out
    // once the whole parameter list has been collected -- so it is copied
    // onto the heap rather than kept as a pointer into a token that is
    // about to be destroyed. freePendingParams releases it once the
    // request that owns this parameter list has been dispatched.
    GMANToken keyToken = nextToken();
    const std::string &keyStr = keyToken.getString();
    char *key = new char[keyStr.size() + 1];
    strcpy(key, keyStr.c_str());
    pendingParamKeys.push_back(key);

    const GMANToken &lookAhead = peekToken();
    if (lookAhead.getType() == GMANToken::LEFT_BRACKET) {
      GMANRIBParse::TokenVector tokenVector = parseArray();
      RtPointer value;
      const bool isStringArray = ! tokenVector.empty() &&
	tokenVector[0].getType() == GMANToken::STRING;
      if (isStringArray) {
	value = (RtPointer) tokenVector.toRtTokenArray();
      } else {
	value = (RtPointer) tokenVector.toRtFloatArray();
      }
      paramMap[key] = value;
      pendingParamValues.push_back(
	{value, isStringArray, (unsigned int) tokenVector.size()});
    } else if (lookAhead.getType() == GMANToken::LONGINT) {
      GMANToken token = nextToken();
      RtFloat *value = new RtFloat[1];
      value[0] = token.getLongInt();
      paramMap[key] = (RtPointer) value;
      pendingParamValues.push_back({(RtPointer) value, false, 1});
    } else if (lookAhead.getType() == GMANToken::REAL) {
      GMANToken token = nextToken();
      RtFloat *value = new RtFloat[1];
      value[0] = token.getReal();
      paramMap[key] = (RtPointer) value;
      pendingParamValues.push_back({(RtPointer) value, false, 1});
    } else {
      RtToken *value = new RtToken[1];
      value[0] = copyStringToken();
      paramMap[key] = (RtPointer) value;
      pendingParamValues.push_back({(RtPointer) value, true, 1});
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
  pendingTokenArrays.push_back(tokens);
  pendingParmArrays.push_back(parms);
}

RtVoid GMANRIBParse::freePendingParams(RtVoid) {
  for (std::vector<char *>::iterator it = pendingParamKeys.begin();
       it != pendingParamKeys.end(); ++it) {
    delete [] *it;
  }
  pendingParamKeys.clear();

  for (std::vector<PendingParamValue>::iterator it = pendingParamValues.begin();
       it != pendingParamValues.end(); ++it) {
    if (it->isStringArray) {
      RtToken *strings = (RtToken *) it->value;
      for (unsigned int i = 0; i < it->count; i++) {
	delete [] (char *) strings[i];
      }
      delete [] strings;
    } else {
      delete [] (RtFloat *) it->value;
    }
  }
  pendingParamValues.clear();

  for (std::vector<RtToken *>::iterator it = pendingTokenArrays.begin();
       it != pendingTokenArrays.end(); ++it) {
    delete [] *it;
  }
  pendingTokenArrays.clear();

  for (std::vector<RtPointer *>::iterator it = pendingParmArrays.begin();
       it != pendingParmArrays.end(); ++it) {
    delete [] *it;
  }
  pendingParmArrays.clear();
}


const GMANToken &GMANRIBParse::nextToken() {
  if (lookAheadToken.getType() == GMANToken::UNKNOWN) {
    currentToken = tokenizer.getNext(*ribStream);
  } else {
    currentToken = lookAheadToken;
    lookAheadToken = GMANToken();
  }

  return currentToken;
}

const GMANToken &GMANRIBParse::peekToken() {
  if (lookAheadToken.getType() == GMANToken::UNKNOWN) {
    lookAheadToken = tokenizer.getNext(*ribStream);
  }

  return lookAheadToken;
}

std::unique_ptr<std::istream> GMANRIBParse::openRibStream(const std::string &path) {
  std::ifstream probe(path, std::ios::binary);
  if (! probe) {
    GMANError error(RIE_NOFILE, RIE_ERROR,
		     ("GMANRIBParse: cannot open \"" + path + "\"").c_str());
    throw error;
  }
  unsigned char magic[2] = {0, 0};
  probe.read(reinterpret_cast<char *>(magic), 2);
  probe.close();

  const bool isGzip = (magic[0] == 0x1f) && (magic[1] == 0x8b);
  if (! isGzip) {
    return std::unique_ptr<std::istream>(
      new std::ifstream(path, std::ios::binary));
  }

  gzFile gz = gzopen(path.c_str(), "rb");
  if (gz == nullptr) {
    GMANError error(RIE_NOFILE, RIE_ERROR,
		     ("GMANRIBParse: cannot open gzip archive \"" + path +
		      "\"").c_str());
    throw error;
  }

  /* The whole archive is decompressed into memory, so a small file with a
   * large expansion ratio would otherwise dictate how much this process
   * allocates. The cap bounds that: RIB is text, and the largest fixture
   * here (bikeData.rib.gz, ~5,300 lines) expands to well under a megabyte. */
  const std::string::size_type maxDecompressed = 512u * 1024u * 1024u;

  std::string decompressed;
  char chunk[65536];
  int bytesRead = 0;
  while ((bytesRead = gzread(gz, chunk, sizeof(chunk))) > 0) {
    if (decompressed.size() + static_cast<std::string::size_type>(bytesRead) >
	maxDecompressed) {
      gzclose(gz);
      GMANError error(RIE_BADFILE, RIE_ERROR,
		       ("GMANRIBParse: gzip archive \"" + path +
			"\" exceeds the decompression limit").c_str());
      throw error;
    }
    decompressed.append(chunk, static_cast<std::string::size_type>(bytesRead));
  }

  int gzErrNum = Z_OK;
  const char *gzErrStr = gzerror(gz, &gzErrNum);
  const bool hadError = (bytesRead < 0) || (gzErrNum != Z_OK);
  std::string errMsg = hadError && gzErrStr != nullptr ? std::string(gzErrStr) : std::string();
  gzclose(gz);

  if (hadError) {
    GMANError error(RIE_BADFILE, RIE_ERROR,
		     ("GMANRIBParse: gzip decompression failed for \"" +
		      path + "\": " + errMsg).c_str());
    throw error;
  }

  return std::unique_ptr<std::istream>(new std::istringstream(decompressed));
}

RtToken* GMANRIBParse::TokenVector::toRtTokenArray() {
  RtToken* array = new RtToken[size()];

  for (unsigned int i = 0; i < size(); i++) {
    const GMANToken &tok = (*this)[i];
    if (tok.getType() != GMANToken::STRING) {
      /* Slots [0,i) already hold heap-duplicated strings; releasing only the
       * pointer array would strand every one of them. */
      for (unsigned int j = 0; j < i; j++) {
	delete [] array[j];
      }
      delete [] array;
      throw(GMANError(RIE_SYNTAX, RIE_ERROR, "Non-string in array."));
    }
    const std::string &s = tok.getString();
    char *dup = new char[s.size() + 1];
    strcpy(dup, s.c_str());
    array[i] = dup;
  }

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
