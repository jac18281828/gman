/* SPDX-License-Identifier: LGPL-2.1-or-later */

/* This is part of GMAN, a RenderMan-compatible renderer.
 *
 * Copyright (c) 2001, 2000, 1999  John Cairns
 *
 * Author: John Cairns <john@2ad.com>
 */

/*
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
 */

/* Local Headers */
#include <cctype>    /* tolower */
#include <cstring>   /* strcmp, memcpy -- libstdc++ does not pull these in
                      * transitively the way libc++ does */
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
// the concrete renderer: parsePatch/parsePatchMesh dynamic_cast to it to
// reach the counts-aware RiPatchV/RiPatchMeshV overloads (see there)
#include "gmanrendermanimpl.h"

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

// RiPixelFilter's five standard filters, looked up by RISpec name -- the
// names GMANASCII::RiPixelFilter (libgmanrib/gmanascii.cpp) already writes
// for the reverse direction.
bool filterByName(const std::string &name, RtFilterFunc &filterfunc) {
  std::string lower = name;
  for (std::string::size_type i = 0; i < lower.size(); ++i) {
    lower[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(lower[i])));
  }
  if (lower == "box") {
    filterfunc = RiBoxFilter;
  } else if (lower == "triangle") {
    filterfunc = RiTriangleFilter;
  } else if (lower == "catmull-rom") {
    filterfunc = RiCatmullRomFilter;
  } else if (lower == "sinc") {
    filterfunc = RiSincFilter;
  } else if (lower == "gaussian") {
    filterfunc = RiGaussianFilter;
  } else {
    return false;
  }
  return true;
}

// Heap copy of s, including its null terminator. The RI API takes char*,
// so a parsed string handed to it needs its own heap-owned copy; the
// unique_ptr's default deleter is delete[], matching the copy's allocation
// exactly. A caller that needs a raw char* past this function's return
// takes it with .release() and keeps releasing it with delete[] itself.
std::unique_ptr<char[]> duplicateCString(const std::string &s) {
  std::unique_ptr<char[]> dup(new char[s.size() + 1]);
  std::memcpy(dup.get(), s.c_str(), s.size() + 1);
  return dup;
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
GMANRIBParse::GMANRIBParse(GMANRenderMan &renderman,
			   const char *rib,
			   RtToken name)
 : handlersRegistered(false),
  renderMan(renderman),
  ribStream(openRibStream(rib))
{
  includeDirs.push_back(dirName(std::string(rib)));
  openArchives.push_back(canonicalOrSelf(std::string(rib)));

  renderMan.RiBegin(name);

};


// default destructor
GMANRIBParse::~GMANRIBParse() {
  renderMan.RiEnd();
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
    warning("RIB: unrecognized requests skipped: {}", list.c_str());
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
      debug("String token: \"{}\"", tok.getString().c_str());
      break;
    case GMANToken::REAL:
      debug("Real token: {:f}", tok.getReal());
      break;
    case GMANToken::LONGINT:
      debug("Long token: {}", tok.getLongInt());
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
      renderMan.RiReverseOrientation();
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
      renderMan.RiWorldBegin();
      break;
    case GMANToken::RI_WORLD_END:
      debug("Keyword token: WorldEnd");
      renderMan.RiWorldEnd();
      break;
    case GMANToken::RI_ATTRIBUTE_BEGIN:
      debug("Keyword token: AttributeBegin");
      renderMan.RiAttributeBegin();
      break;
    case GMANToken::RI_ATTRIBUTE_END:
      debug("Keyword token: AttributeEnd");
      renderMan.RiAttributeEnd();
      break;
    case GMANToken::RI_TRANSFORM_BEGIN:
      debug("Keyword token: TransformBegin");
      renderMan.RiTransformBegin();
      break;
    case GMANToken::RI_TRANSFORM_END:
      debug("Keyword token: TransformEnd");
      renderMan.RiTransformEnd();
      break;
    case GMANToken::RI_FRAME_BEGIN:
      debug("Keyword token: FrameBegin");
      renderMan.RiFrameBegin(nextInt());
      break;
    case GMANToken::RI_FRAME_END:
      debug("Keyword token: FrameEnd");
      renderMan.RiFrameEnd();
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
      renderMan.RiIdentity();
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
    case GMANToken::RI_GENERAL_POLYGON:
      debug("Keyword token: GeneralPolygon");
      parseGeneralPolygon();
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
      debug("Keyword token: MakeTexture");
      parseMakeTexture();
      break;
    case GMANToken::RI_MAKE_BUMP:
      debug("Keyword token: MakeBump (parse-only)");
      parseMakeBump();
      break;
    case GMANToken::RI_MAKE_LAT_LONG_ENVIRONMENT:
      debug("Keyword token: MakeLatLongEnvironment");
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
    case GMANToken::RI_PIXEL_FILTER:
      debug("Keyword token: PixelFilter (parse-only)");
      parsePixelFilter();
      break;
    case GMANToken::RI_UNKNOWN_REQUEST:
      skipUnknownRequest(tok.getString());
      break;
    default:
      debug("Unknown token type: {}", static_cast<int>(tok.getType()));
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

std::string GMANRIBParse::copyStringToken() {
  const GMANToken &tok = nextToken();

  if (tok.getType() != GMANToken::STRING) {
    GMANError error(RIE_BADFILE,RIE_ERROR,"Expecting string token");
    throw(error);
  }

  return tok.getString();
}

RtVoid  GMANRIBParse::parseOption(RtVoid) {
  const auto name = copyStringToken();

  RtInt n = 0;
  RtToken* tokens;
  RtPointer* parms;
  RtInt* counts;

  parseParameterList(n, tokens, parms, counts);

  renderMan.RiOptionV(name.c_str(), n, tokens, parms);

}

RtVoid  GMANRIBParse::parseDisplay(RtVoid) {
  const auto name = copyStringToken();
  const auto type = copyStringToken();
  const auto mode = copyStringToken();

  RtInt n = 0;
  RtToken* tokens;
  RtPointer* parms;
  RtInt* counts;

  parseParameterList(n, tokens, parms, counts);

  // RiDisplayV's name parameter is char* rather than const char* for
  // historical reasons; it only reads through it.
  renderMan.RiDisplayV(const_cast<char *>(name.c_str()), type.c_str(),
			 mode.c_str(), n, tokens, parms);

}

RtVoid  GMANRIBParse::parseFormat(RtVoid) {
  RtInt xresolution = nextInt();
  RtInt yresolution = nextInt();
  RtFloat pixelaspectratio = nextFloat();

  renderMan.RiFormat(xresolution, yresolution, pixelaspectratio);
}

RtVoid GMANRIBParse::parseProjection(RtVoid) {
  const auto name = copyStringToken();

  RtInt n = 0;
  RtToken* tokens;
  RtPointer* parms;
  RtInt* counts;

  parseParameterList(n, tokens, parms, counts);

  renderMan.RiProjectionV(name.c_str(), n, tokens, parms);
}

RtVoid GMANRIBParse::parseGeometricApproximation(RtVoid) {
  const auto type = copyStringToken();
  RtFloat value = nextFloat();

  renderMan.RiGeometricApproximation(type.c_str(), value);
}

RtVoid GMANRIBParse::parseShadingInterpolation(RtVoid) {

  const auto type = copyStringToken();

  renderMan.RiShadingInterpolation(type.c_str());
}

RtVoid GMANRIBParse::parseShadingRate(RtVoid) {

  RtFloat size = nextFloat();

  renderMan.RiShadingRate(size);
}

RtVoid GMANRIBParse::parseOrientation(RtVoid) {

  const auto orientation = copyStringToken();

  renderMan.RiOrientation(orientation.c_str());
}

RtVoid GMANRIBParse::parsePixelSamples(RtVoid) {

  RtFloat xsamples = nextFloat();
  RtFloat ysamples = nextFloat();

  renderMan.RiPixelSamples(xsamples, ysamples);
}

RtVoid GMANRIBParse::parseExposure(RtVoid) {

  RtFloat gain = nextFloat();
  RtFloat gamma = nextFloat();

  renderMan.RiExposure(gain, gamma);
}

RtVoid GMANRIBParse::parseDepthOfField(RtVoid) {

  RtFloat fstop = nextFloat();
  RtFloat focallength = nextFloat();
  RtFloat focaldistance = nextFloat();

  renderMan.RiDepthOfField(fstop, focallength, focaldistance);
}

RtVoid GMANRIBParse::parseShutter(RtVoid) {

  RtFloat min = nextFloat();
  RtFloat max = nextFloat();

  renderMan.RiShutter(min, max);
}

RtVoid GMANRIBParse::parseHider(RtVoid) {

  const auto type = copyStringToken();

  RtInt n = 0;
  RtToken* tokens;
  RtPointer* parms;
  RtInt* counts;

  parseParameterList(n, tokens, parms, counts);

  renderMan.RiHiderV(type.c_str(), n, tokens, parms);
}

RtVoid GMANRIBParse::parseCropWindow(RtVoid) {

  RtFloat xmin = nextFloat();
  RtFloat xmax = nextFloat();
  RtFloat ymin = nextFloat();
  RtFloat ymax = nextFloat();

  renderMan.RiCropWindow(xmin, xmax, ymin, ymax);
}

RtVoid GMANRIBParse::parseScreenWindow(RtVoid) {

  RtFloat left = nextFloat();
  RtFloat right = nextFloat();
  RtFloat bottom = nextFloat();
  RtFloat top = nextFloat();

  renderMan.RiScreenWindow(left, right, bottom, top);
}

RtVoid GMANRIBParse::parseClipping(RtVoid) {

  RtFloat nearDist = nextFloat();
  RtFloat farDist = nextFloat();

  renderMan.RiClipping(nearDist, farDist);
}

RtVoid GMANRIBParse::parseDeclare(RtVoid) {

  const auto name = copyStringToken();
  const auto declaration = copyStringToken();

  renderMan.RiDeclare(name.c_str(), declaration.c_str());
}

RtVoid GMANRIBParse::parseAttribute(RtVoid) {

  const auto name = copyStringToken();

  RtInt n = 0;
  RtToken* tokens;
  RtPointer* parms;
  RtInt* counts;

  parseParameterList(n, tokens, parms, counts);

  renderMan.RiAttributeV(name.c_str(), n, tokens, parms);
}

RtVoid GMANRIBParse::parseColor(RtVoid) {
  int number = 3;
  //FIXME: Should actually get number from options
  //renderMan.getGraphicsState().getOptions().getColorSamples().getNumber();

  std::vector<RtFloat> color(number);
  const GMANToken &lookAhead = peekToken();
  if (lookAhead.getType() == GMANToken::LEFT_BRACKET) {
    GMANRIBParse::TokenVector tokenVector = parseArray();
    std::vector<RtFloat> values = tokenVector.toRtFloatVector();
    int count = number < (int) tokenVector.size() ? number : (int) tokenVector.size();
    int i;
    for (i = 0; i < count; i++) {
      color[i] = values[i];
    }
    for (; i < number; i++) {
      color[i] = 0.0;
    }
  } else {
    for (int i = 0; i < number; i++) {
      color[i] = nextFloat();
    }
  }

  renderMan.RiColor(color.data());
}

RtVoid GMANRIBParse::parseOpacity(RtVoid) {
  int number = 3;
  //FIXME: Should actually get number from options
  //renderMan.getGraphicsState().getOptions().getColorSamples().getNumber();

  std::vector<RtFloat> color(number);
  const GMANToken &lookAhead = peekToken();
  if (lookAhead.getType() == GMANToken::LEFT_BRACKET) {
    GMANRIBParse::TokenVector tokenVector = parseArray();
    std::vector<RtFloat> values = tokenVector.toRtFloatVector();
    int count = number < (int) tokenVector.size() ? number : (int) tokenVector.size();
    int i;
    for (i = 0; i < count; i++) {
      color[i] = values[i];
    }
    for (; i < number; i++) {
      color[i] = 0.0;
    }
  } else {
    for (int i = 0; i < number; i++) {
      color[i] = nextFloat();
    }
  }

  renderMan.RiOpacity(color.data());
}

RtVoid GMANRIBParse::parseLightSource(RtVoid) {
  const auto shadername = copyStringToken();
  int sequence = nextInt();

  RtInt n = 0;
  RtToken* tokens;
  RtPointer* parms;
  RtInt* counts;

  parseParameterList(n, tokens, parms, counts);

  RtLightHandle handle =
    renderMan.RiLightSourceV(shadername.c_str(), n, tokens, parms);

  lightHandleMap[sequence] = handle;
}

RtVoid GMANRIBParse::parseSurface(RtVoid) {
  const auto shadername = copyStringToken();

  RtInt n = 0;
  RtToken* tokens;
  RtPointer* parms;
  RtInt* counts;

  parseParameterList(n, tokens, parms, counts);

  // FIXME: Implement some surface shaders
  renderMan.RiSurfaceV(shadername.c_str(), n, tokens, parms);
}

RtVoid GMANRIBParse::parseCoordinateSystem(RtVoid) {

  const auto name = copyStringToken();

  renderMan.RiCoordinateSystem(name.c_str());
}

RtVoid GMANRIBParse::parseTransform(RtVoid) {
  GMANRIBParse::TokenVector tokenVector = parseArray();
  if (tokenVector.size() != 16) {
    GMANError error(RIE_SYNTAX, RIE_ERROR,
		     "GMANRIBParse: Transform matrix must have 16 elements");
    throw error;
  }
  std::vector<RtFloat> values = tokenVector.toRtFloatVector();

  RtMatrix transform;
  for (int i = 0; i < 4; i++) {
    for (int j = 0; j < 4; j++) {
      transform[i][j] = values[i * 4 + j];
    }
  }

  renderMan.RiTransform(transform);
}

RtVoid GMANRIBParse::parseConcatTransform(RtVoid) {
  GMANRIBParse::TokenVector tokenVector = parseArray();
  if (tokenVector.size() != 16) {
    GMANError error(RIE_SYNTAX, RIE_ERROR,
		     "GMANRIBParse: ConcatTransform matrix must have 16 elements");
    throw error;
  }
  std::vector<RtFloat> values = tokenVector.toRtFloatVector();

  RtMatrix transform;
  for (int i = 0; i < 4; i++) {
    for (int j = 0; j < 4; j++) {
      transform[i][j] = values[i * 4 + j];
    }
  }

  renderMan.RiConcatTransform(transform);
}

RtVoid GMANRIBParse::parseTranslate(RtVoid) {
  RtFloat dx = nextFloat();
  RtFloat dy = nextFloat();
  RtFloat dz = nextFloat();

  renderMan.RiTranslate(dx, dy, dz);
}

RtVoid GMANRIBParse::parseRotate(RtVoid) {
  RtFloat angle = nextFloat();
  RtFloat dx = nextFloat();
  RtFloat dy = nextFloat();
  RtFloat dz = nextFloat();

  renderMan.RiRotate(angle, dx, dy, dz);
}

RtVoid GMANRIBParse::parseScale(RtVoid) {
  RtFloat sx = nextFloat();
  RtFloat sy = nextFloat();
  RtFloat sz = nextFloat();

  renderMan.RiScale(sx, sy, sz);
}

RtVoid GMANRIBParse::parseSphere(RtVoid) {
  RtFloat radius = nextFloat();
  RtFloat zmin = nextFloat();
  RtFloat zmax = nextFloat();
  RtFloat thetamax = nextFloat();

  RtInt n = 0;
  RtToken* tokens;
  RtPointer* parms;
  RtInt* counts;

  parseParameterList(n, tokens, parms, counts);

  renderMan.RiSphereV(radius, zmin, zmax, thetamax, n, tokens, parms);
}

RtVoid GMANRIBParse::parseCone(RtVoid) {
  RtFloat height = nextFloat();
  RtFloat radius = nextFloat();
  RtFloat thetamax = nextFloat();

  RtInt n = 0;
  RtToken* tokens;
  RtPointer* parms;
  RtInt* counts;

  parseParameterList(n, tokens, parms, counts);

  renderMan.RiConeV(height, radius, thetamax, n, tokens, parms);
}

RtVoid GMANRIBParse::parseCylinder(RtVoid) {
  RtFloat radius = nextFloat();
  RtFloat zmin = nextFloat();
  RtFloat zmax = nextFloat();
  RtFloat thetamax = nextFloat();

  RtInt n = 0;
  RtToken* tokens;
  RtPointer* parms;
  RtInt* counts;

  parseParameterList(n, tokens, parms, counts);

  renderMan.RiCylinderV(radius, zmin, zmax, thetamax, n, tokens, parms);
}

RtVoid GMANRIBParse::parseSides(RtVoid) {
  RtInt sides = nextInt();

  renderMan.RiSides(sides);
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
  RtInt* counts;

  parseParameterList(n, tokens, parms, counts);

  renderMan.RiHyperboloidV(point1, point2, thetamax, n, tokens, parms);
}

RtVoid GMANRIBParse::parseParaboloid(RtVoid) {
  RtFloat rmax = nextFloat();
  RtFloat zmin = nextFloat();
  RtFloat zmax = nextFloat();
  RtFloat thetamax = nextFloat();

  RtInt n = 0;
  RtToken* tokens;
  RtPointer* parms;
  RtInt* counts;

  parseParameterList(n, tokens, parms, counts);

  renderMan.RiParaboloidV(rmax, zmin, zmax, thetamax, n, tokens, parms);
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
  RtInt* counts;

  parseParameterList(n, tokens, parms, counts);

  renderMan.RiTorusV(majorradius, minorradius, phimin, phimax, thetamax,
		      n, tokens, parms);
}

RtVoid GMANRIBParse::parseDisk(RtVoid) {
  RtFloat height = nextFloat();
  RtFloat radius = nextFloat();
  RtFloat thetamax = nextFloat();

  RtInt n = 0;
  RtToken* tokens;
  RtPointer* parms;
  RtInt* counts;

  parseParameterList(n, tokens, parms, counts);

  renderMan.RiDiskV(height, radius, thetamax, n, tokens, parms);
}

RtVoid GMANRIBParse::parsePolygon(RtVoid) {

  RtInt n = 0;
  RtToken* tokens;
  RtPointer* parms;
  RtInt* counts;

  parseParameterList(n, tokens, parms, counts);

  // "P" is a VERTEX/POINT array, 3 floats per vertex; nverts is derived from
  // its element count. pendingParamValues records that count per value
  // pointer in parse order, not sorted by key like tokens/parms, so "P"'s
  // entry is found by matching parms[i] rather than by index. No "P" at all,
  // or a count not a multiple of 3, is malformed: leave nverts at 0 and let
  // RiPolygonV's own degenerate-input handling take it from there, the same
  // soft failure this codebase gives other malformed input.
  int nverts = 0;
  for (int i = 0; i < n; i++) {
    if (! strcmp(tokens[i], "P")) {
      for (const auto &pending : pendingParamValues) {
	if (pending.value == parms[i]) {
	  if (pending.count % 3 == 0) {
	    nverts = pending.count / 3;
	  }
	  break;
	}
      }
      break;
    }
  }

  renderMan.RiPolygonV(nverts, n, tokens, parms);
}

RtVoid GMANRIBParse::parseGeneralPolygon(RtVoid) {

  GMANRIBParse::TokenVector nvertsVector = parseArray();
  std::vector<RtInt> nverts = nvertsVector.toRtIntVector();

  RtInt n = 0;
  RtToken* tokens;
  RtPointer* parms;
  RtInt* counts;

  parseParameterList(n, tokens, parms, counts);

  RtInt nloops = (RtInt) nvertsVector.size();

  // Dispatch beside the RI-mandated RiGeneralPolygonV(5 args): a RIB file
  // is not a trusted caller, so the array length parseParameterList
  // already knows rides along outside that fixed signature. See
  // gmanparameterlist.h.
  if (GMANRenderManImpl *impl = dynamic_cast<GMANRenderManImpl *>(&renderMan)) {
    impl->RiGeneralPolygonV(nloops, nverts.data(), n, tokens, parms, counts);
  } else {
    renderMan.RiGeneralPolygonV(nloops, nverts.data(), n, tokens, parms);
  }
}

RtVoid GMANRIBParse::parsePoints(RtVoid) {

  RtInt n = 0;
  RtToken* tokens;
  RtPointer* parms;
  RtInt* counts;

  parseParameterList(n, tokens, parms, counts);

  int npoints = 0;
  for (int i = 0; i < n; i++) {
    if (! strcmp(tokens[i], "P")) {
      // FIXME: Are arrays NULL terminated?  Can use that for length
    }
  }

  renderMan.RiPointsV(npoints, n, tokens, parms);
}

RtVoid GMANRIBParse::parsePointsPolygons(RtVoid) {

  GMANRIBParse::TokenVector nvertsVector = parseArray();
  std::vector<RtInt> nverts = nvertsVector.toRtIntVector();

  GMANRIBParse::TokenVector vertsVector = parseArray();
  std::vector<RtInt> verts = vertsVector.toRtIntVector();

  RtInt n = 0;
  RtToken* tokens;
  RtPointer* parms;
  RtInt* counts;

  parseParameterList(n, tokens, parms, counts);

  RtInt npolys = (RtInt) nvertsVector.size();

  // The RI signature carries no array length, so a RIB file -- not a
  // trusted caller -- gets its own structural check here: a verts array
  // whose actual length disagrees with nverts' own sum would read past
  // (or short of) its own allocation inside RiPointsPolygonsV, which has
  // no way to know that length either. Every array is already consumed
  // above, so the token stream stays in sync either way.
  long long expectedVerts = 0;
  for (RtInt i = 0; i < npolys; i++) {
    expectedVerts += nverts[i];
  }
  if ((long long) vertsVector.size() != expectedVerts) {
    warning("PointsPolygons: verts length {} does not match nverts sum "
	    "{}; ignoring.", vertsVector.size(), expectedVerts);
    return;
  }

  // Dispatch beside the RI-mandated RiPointsPolygonsV(6 args): a RIB file
  // is not a trusted caller, so the array length parseParameterList
  // already knows rides along outside that fixed signature. See
  // parseGeneralPolygon's own comment.
  if (GMANRenderManImpl *impl = dynamic_cast<GMANRenderManImpl *>(&renderMan)) {
    impl->RiPointsPolygonsV(npolys, nverts.data(), verts.data(), n, tokens,
			    parms, counts);
  } else {
    renderMan.RiPointsPolygonsV(npolys, nverts.data(), verts.data(), n,
				 tokens, parms);
  }
}

RtVoid GMANRIBParse::parsePointsGeneralPolygons(RtVoid) {

  GMANRIBParse::TokenVector nloopsVector = parseArray();
  std::vector<RtInt> nloops = nloopsVector.toRtIntVector();

  GMANRIBParse::TokenVector nvertsVector = parseArray();
  std::vector<RtInt> nverts = nvertsVector.toRtIntVector();

  GMANRIBParse::TokenVector vertsVector = parseArray();
  std::vector<RtInt> verts = vertsVector.toRtIntVector();

  RtInt n = 0;
  RtToken* tokens;
  RtPointer* parms;
  RtInt* counts;

  parseParameterList(n, tokens, parms, counts);

  RtInt npolys = (RtInt) nloopsVector.size();

  // nverts' own required length is nloops' sum, and verts' own required
  // length is nverts' sum -- two structural checks this request carries
  // that PointsPolygons does not, since it has no separate loop count.
  long long expectedNverts = 0;
  for (RtInt i = 0; i < npolys; i++) {
    expectedNverts += nloops[i];
  }
  if ((long long) nvertsVector.size() != expectedNverts) {
    warning("PointsGeneralPolygons: nverts length {} does not match "
	    "nloops sum {}; ignoring.", nvertsVector.size(), expectedNverts);
    return;
  }

  long long expectedVerts = 0;
  for (std::size_t i = 0; i < nvertsVector.size(); i++) {
    expectedVerts += nverts[i];
  }
  if ((long long) vertsVector.size() != expectedVerts) {
    warning("PointsGeneralPolygons: verts length {} does not match "
	    "nverts sum {}; ignoring.", vertsVector.size(), expectedVerts);
    return;
  }

  if (GMANRenderManImpl *impl = dynamic_cast<GMANRenderManImpl *>(&renderMan)) {
    impl->RiPointsGeneralPolygonsV(npolys, nloops.data(), nverts.data(),
				   verts.data(), n, tokens, parms, counts);
  } else {
    renderMan.RiPointsGeneralPolygonsV(npolys, nloops.data(), nverts.data(),
					verts.data(), n, tokens, parms);
  }
}

RtVoid GMANRIBParse::parsePatch(RtVoid) {

  const auto type = copyStringToken();

  RtInt n = 0;
  RtToken* tokens;
  RtPointer* parms;
  RtInt* counts;

  parseParameterList(n, tokens, parms, counts);

  // Dispatch beside the RI-mandated RiPatchV(4 args): a RIB file is not a
  // trusted caller, so the array length parseParameterList already knows
  // rides along outside that fixed signature. See gmanparameterlist.h.
  if (GMANRenderManImpl *impl = dynamic_cast<GMANRenderManImpl *>(&renderMan)) {
    impl->RiPatchV(type.c_str(), n, tokens, parms, counts);
  } else {
    renderMan.RiPatchV(type.c_str(), n, tokens, parms);
  }
}

RtVoid GMANRIBParse::parseNuPatch(RtVoid) {

  RtInt nu = nextInt();
  RtInt uorder = nextInt();
  GMANRIBParse::TokenVector uknotVector = parseArray();
  std::vector<RtFloat> uknot = uknotVector.toRtFloatVector();
  RtFloat umin = nextFloat();
  RtFloat umax = nextFloat();
  RtInt nv = nextInt();
  RtInt vorder = nextInt();
  GMANRIBParse::TokenVector vknotVector = parseArray();
  std::vector<RtFloat> vknot = vknotVector.toRtFloatVector();
  RtFloat vmin = nextFloat();
  RtFloat vmax = nextFloat();

  RtInt n = 0;
  RtToken* tokens;
  RtPointer* parms;
  RtInt* counts;

  parseParameterList(n, tokens, parms, counts);

  // The RI signature carries no knot-array length; a RIB file is not a
  // trusted caller, so a uknot/vknot whose actual length disagrees with
  // the nu+uorder/nv+vorder the request itself declares gets its own
  // check here -- RiNuPatchV has no way to know that length either. Every
  // array is already consumed above, so the token stream stays in sync
  // either way.
  if ((long long) uknotVector.size() != (long long) nu + uorder) {
    warning("NuPatch: uknot length {} does not match nu + uorder = {}; "
	    "ignoring.", uknotVector.size(), (long long) nu + uorder);
    return;
  }
  if ((long long) vknotVector.size() != (long long) nv + vorder) {
    warning("NuPatch: vknot length {} does not match nv + vorder = {}; "
	    "ignoring.", vknotVector.size(), (long long) nv + vorder);
    return;
  }

  // Dispatch beside the RI-mandated RiNuPatchV(13 args): a RIB file is not
  // a trusted caller, so the array length parseParameterList already
  // knows rides along outside that fixed signature. See parsePatch.
  if (GMANRenderManImpl *impl = dynamic_cast<GMANRenderManImpl *>(&renderMan)) {
    impl->RiNuPatchV(nu, uorder, uknot.data(), umin, umax, nv, vorder,
		      vknot.data(), vmin, vmax, n, tokens, parms, counts);
  } else {
    renderMan.RiNuPatchV(nu, uorder, uknot.data(), umin, umax, nv, vorder,
			  vknot.data(), vmin, vmax, n, tokens, parms);
  }
}

RtVoid GMANRIBParse::parsePatchMesh(RtVoid) {

  const auto type = copyStringToken();
  RtInt nu = nextInt();
  const auto uwrap = copyStringToken();
  RtInt nv = nextInt();
  const auto vwrap = copyStringToken();

  RtInt n = 0;
  RtToken* tokens;
  RtPointer* parms;
  RtInt* counts;

  parseParameterList(n, tokens, parms, counts);

  // See parsePatch: dispatch beside the RI-mandated RiPatchMeshV(7 args)
  // when the concrete impl is available, carrying the supplied counts.
  if (GMANRenderManImpl *impl = dynamic_cast<GMANRenderManImpl *>(&renderMan)) {
    impl->RiPatchMeshV(type.c_str(), nu, uwrap.c_str(), nv, vwrap.c_str(),
			n, tokens, parms, counts);
  } else {
    renderMan.RiPatchMeshV(type.c_str(), nu, uwrap.c_str(), nv, vwrap.c_str(),
			     n, tokens, parms);
  }
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

  renderMan.RiTextureCoordinates(s1, t1, s2, t2, s3, t3, s4, t4);
}

RtVoid GMANRIBParse::parseReadArchive(RtVoid) {

  const auto requested = copyStringToken();

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
  std::vector<RtFloat> times = tokenVector.toRtFloatVector();

  renderMan.RiMotionBeginV(n, times.data());
}

RtVoid GMANRIBParse::parseMotionEnd(RtVoid) {
  renderMan.RiMotionEnd();
}

RtVoid GMANRIBParse::parseObjectBegin(RtVoid) {

  int sequence = nextInt();

  RtObjectHandle handle = renderMan.RiObjectBegin();
  objectHandleMap[sequence] = handle;
}

RtVoid GMANRIBParse::parseObjectEnd(RtVoid) {
  renderMan.RiObjectEnd();
}

RtVoid GMANRIBParse::parseObjectInstance(RtVoid) {

  int sequence = nextInt();

  RtObjectHandle handle = objectHandleMap[sequence];

  renderMan.RiObjectInstance(handle);
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
    std::vector<RtFloat> values = tokenVector.toRtFloatVector();
    for (int i = 0; i < 4; i++) {
      for (int j = 0; j < 4; j++) {
	ubasis[i][j] = values[i * 4 + j];
      }
    }
  } else {
    const auto uname = copyStringToken();
    bool found = basisByName(uname, ubasis);
    if (! found) {
      std::string msg = std::string("GMANRIBParse: unknown basis \"") +
	uname + "\"";
      GMANError error(RIE_BADTOKEN, RIE_ERROR, msg.c_str());
      throw error;
    }
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
    std::vector<RtFloat> values = tokenVector.toRtFloatVector();
    for (int i = 0; i < 4; i++) {
      for (int j = 0; j < 4; j++) {
	vbasis[i][j] = values[i * 4 + j];
      }
    }
  } else {
    const auto vname = copyStringToken();
    bool found = basisByName(vname, vbasis);
    if (! found) {
      std::string msg = std::string("GMANRIBParse: unknown basis \"") +
	vname + "\"";
      GMANError error(RIE_BADTOKEN, RIE_ERROR, msg.c_str());
      throw error;
    }
  }
  RtInt vstep = nextInt();

  renderMan.RiBasis(ubasis, ustep, vbasis, vstep);
}

RtVoid GMANRIBParse::parseAtmosphere(RtVoid) {

  const auto name = copyStringToken();

  RtInt n = 0;
  RtToken* tokens;
  RtPointer* parms;
  RtInt* counts;

  parseParameterList(n, tokens, parms, counts);

  // FIXME: implement some atmosphere shaders
  //  renderMan.RiAtmosphereV(name.c_str(), n, tokens, parms);
}

RtVoid GMANRIBParse::parseDisplacement(RtVoid) {

  const auto name = copyStringToken();

  RtInt n = 0;
  RtToken* tokens;
  RtPointer* parms;
  RtInt* counts;

  parseParameterList(n, tokens, parms, counts);

  // FIXME: implement some displacement shaders
  //  renderMan.RiDisplacementV(name.c_str(), n, tokens, parms);
}

RtVoid GMANRIBParse::parseImager(RtVoid) {

  const auto name = copyStringToken();

  RtInt n = 0;
  RtToken* tokens;
  RtPointer* parms;
  RtInt* counts;

  parseParameterList(n, tokens, parms, counts);

  // FIXME: implement some imagers
  //renderMan.RiImagerV(name.c_str(), n, tokens, parms);
}

RtVoid GMANRIBParse::parseIlluminate(RtVoid) {

  int sequence = nextInt();
  int onoff = nextInt();

  RtLightHandle handle = lightHandleMap[sequence];
  renderMan.RiIlluminate(handle, onoff==0?false:true);
}

// **************************************************************
// RISpec 3.2 requests parsed and ignored. Each consumes exactly its own
// grammar so the token stream stays in sync for whatever follows; none
// calls into renderMan, since rendering these is Scope C (SPEC.md S4),
// not Phase 2.
// **************************************************************

RtVoid GMANRIBParse::parseCurves(RtVoid) {
  // Curves type ncurves[] wrap paramlist
  (void) copyStringToken(); // type
  GMANRIBParse::TokenVector ncurves = parseArray();
  (void) copyStringToken(); // wrap

  RtInt n = 0;
  RtToken *tokens;
  RtPointer *parms;
  RtInt *counts;
  parseParameterList(n, tokens, parms, counts);
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
  RtInt *counts;
  parseParameterList(n, tokens, parms, counts);
}

RtVoid GMANRIBParse::parseSubdivisionMesh(RtVoid) {
  // SubdivisionMesh scheme nvertices[] vertices[] tags[] nargs[] intargs[] floatargs[] paramlist
  (void) copyStringToken(); // scheme
  GMANRIBParse::TokenVector nvertices = parseArray();
  GMANRIBParse::TokenVector vertices = parseArray();
  GMANRIBParse::TokenVector tags = parseArray();
  GMANRIBParse::TokenVector nargs = parseArray();
  GMANRIBParse::TokenVector intargs = parseArray();
  GMANRIBParse::TokenVector floatargs = parseArray();

  RtInt n = 0;
  RtToken *tokens;
  RtPointer *parms;
  RtInt *counts;
  parseParameterList(n, tokens, parms, counts);
}

RtVoid GMANRIBParse::parseProcedural(RtVoid) {
  // Procedural procname procargs[] bound[6] -- no trailing paramlist
  (void) copyStringToken(); // procname
  GMANRIBParse::TokenVector procargs = parseArray();
  GMANRIBParse::TokenVector bound = parseArray();
}

RtVoid GMANRIBParse::parseSolidBegin(RtVoid) {
  // SolidBegin "type"
  (void) copyStringToken(); // type
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
  (void) copyStringToken(); // handler
}

RtVoid GMANRIBParse::parseArchiveRecord(RtVoid) {
  // ArchiveRecord "type" "text"
  (void) copyStringToken(); // type
  (void) copyStringToken(); // text
}

RtVoid GMANRIBParse::parseMakeTexture(RtVoid) {
  // MakeTexture picture texture swrap twrap filter swidth twidth paramlist
  const auto picture = copyStringToken();
  const auto texture = copyStringToken();
  const auto swrap = copyStringToken();
  const auto twrap = copyStringToken();
  const auto filterName = copyStringToken();
  RtFloat swidth = nextFloat();
  RtFloat twidth = nextFloat();

  RtFilterFunc filterfunc;
  if (! filterByName(filterName, filterfunc)) {
    std::string msg = std::string("GMANRIBParse: unknown pixel filter \"") +
      filterName + "\"";
    GMANError error(RIE_BADTOKEN, RIE_ERROR, msg.c_str());
    throw error;
  }

  RtInt n = 0;
  RtToken *tokens;
  RtPointer *parms;
  RtInt *counts;
  parseParameterList(n, tokens, parms, counts);

  // RiMakeTextureV's pic/tex parameters are char* rather than const
  // char* for historical reasons; it only reads through them.
  renderMan.RiMakeTextureV(const_cast<char *>(picture.c_str()),
                             const_cast<char *>(texture.c_str()),
                             swrap.c_str(), twrap.c_str(), filterfunc,
                             swidth, twidth, n, tokens, parms);
}

RtVoid GMANRIBParse::parseMakeBump(RtVoid) {
  // Same grammar as MakeTexture.
  (void) copyStringToken(); // picture
  (void) copyStringToken(); // texture
  (void) copyStringToken(); // swrap
  (void) copyStringToken(); // twrap
  (void) copyStringToken(); // filter
  nextFloat();
  nextFloat();

  RtInt n = 0;
  RtToken *tokens;
  RtPointer *parms;
  RtInt *counts;
  parseParameterList(n, tokens, parms, counts);
}

RtVoid GMANRIBParse::parseMakeLatLongEnvironment(RtVoid) {
  // MakeLatLongEnvironment picture texture filter swidth twidth paramlist
  const auto picture = copyStringToken();
  const auto texture = copyStringToken();
  const auto filterName = copyStringToken();
  RtFloat swidth = nextFloat();
  RtFloat twidth = nextFloat();

  RtFilterFunc filterfunc;
  if (! filterByName(filterName, filterfunc)) {
    std::string msg = std::string("GMANRIBParse: unknown pixel filter \"") +
      filterName + "\"";
    GMANError error(RIE_BADTOKEN, RIE_ERROR, msg.c_str());
    throw error;
  }

  RtInt n = 0;
  RtToken *tokens;
  RtPointer *parms;
  RtInt *counts;
  parseParameterList(n, tokens, parms, counts);

  // RiMakeLatLongEnvironmentV's pic/tex parameters are char* rather than
  // const char* for historical reasons; it only reads through them.
  renderMan.RiMakeLatLongEnvironmentV(const_cast<char *>(picture.c_str()),
                                        const_cast<char *>(texture.c_str()),
                                        filterfunc, swidth, twidth, n, tokens,
                                        parms);
}

RtVoid GMANRIBParse::parseMakeCubeFaceEnvironment(RtVoid) {
  // MakeCubeFaceEnvironment px nx py ny pz nz texture fov filter swidth twidth paramlist
  (void) copyStringToken(); // px
  (void) copyStringToken(); // nx
  (void) copyStringToken(); // py
  (void) copyStringToken(); // ny
  (void) copyStringToken(); // pz
  (void) copyStringToken(); // nz
  (void) copyStringToken(); // texture
  nextFloat(); // fov
  (void) copyStringToken(); // filter
  nextFloat();
  nextFloat();

  RtInt n = 0;
  RtToken *tokens;
  RtPointer *parms;
  RtInt *counts;
  parseParameterList(n, tokens, parms, counts);
}

RtVoid GMANRIBParse::parseMakeShadow(RtVoid) {
  // MakeShadow picture texture paramlist
  (void) copyStringToken(); // picture
  (void) copyStringToken(); // texture

  RtInt n = 0;
  RtToken *tokens;
  RtPointer *parms;
  RtInt *counts;
  parseParameterList(n, tokens, parms, counts);
}

RtVoid GMANRIBParse::parseIfBegin(RtVoid) {
  // IfBegin "expression"
  (void) copyStringToken(); // expression
}

RtVoid GMANRIBParse::parseElseIf(RtVoid) {
  // ElseIf "expression"
  (void) copyStringToken(); // expression
}

RtVoid GMANRIBParse::parseElse(RtVoid) {
  // Else -- no arguments
}

RtVoid GMANRIBParse::parseIfEnd(RtVoid) {
  // IfEnd -- no arguments
}

RtVoid GMANRIBParse::parsePixelFilter(RtVoid) {
  // PixelFilter filterfunc xwidth ywidth -- filterfunc names one of the
  // built-in RtFilterFunc implementations.
  const auto name = copyStringToken();
  RtFloat xwidth = nextFloat();
  RtFloat ywidth = nextFloat();

  RtFilterFunc filterfunc;
  if (! filterByName(name, filterfunc)) {
    std::string msg = std::string("GMANRIBParse: unknown pixel filter \"") +
      name + "\"";
    GMANError error(RIE_BADTOKEN, RIE_ERROR, msg.c_str());
    throw error;
  }

  renderMan.RiPixelFilter(filterfunc, xwidth, ywidth);
}

RtVoid GMANRIBParse::skipUnknownRequest(const std::string &name) {
  if (skippedRequests.insert(name).second) {
    warning("RIB: skipping unrecognized request \"{}\"", name.c_str());
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
					RtPointer* &parms, RtInt* &counts) {
  // The supplied element count travels with its value through the map:
  // paramMap emits in key order, not push order, so a counts array built
  // separately in push order would not line up with tokens/parms below.
  struct ParamValue {
    RtPointer value;
    unsigned int count;
  };
  typedef std::map<RtToken, ParamValue> ParamMap;
  ParamMap paramMap;

  // Stores values in pendingParamValues, owned by its floatStorage vector,
  // and returns the pointer RI calls read: moving a vector preserves its
  // buffer, so the pointer stays valid once this struct is in the
  // (possibly reallocated) pendingParamValues vector.
  auto pushFloatValue = [this](std::vector<RtFloat> values) -> RtPointer {
    const unsigned int count = (unsigned int) values.size();
    pendingParamValues.push_back({nullptr, false, count, std::move(values)});
    RtFloat *data = pendingParamValues.back().floatStorage.data();
    pendingParamValues.back().value = (RtPointer) data;
    return (RtPointer) data;
  };

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
    pendingParamKeys.push_back(duplicateCString(keyStr));
    char *key = pendingParamKeys.back().get();

    const GMANToken &lookAhead = peekToken();
    if (lookAhead.getType() == GMANToken::LEFT_BRACKET) {
      GMANRIBParse::TokenVector tokenVector = parseArray();
      const bool isStringArray = ! tokenVector.empty() &&
	tokenVector[0].getType() == GMANToken::STRING;
      const unsigned int count = (unsigned int) tokenVector.size();
      if (isStringArray) {
	RtPointer value = (RtPointer) tokenVector.toRtTokenArray();
	paramMap[key] = {value, count};
	pendingParamValues.push_back({value, true, count, {}});
      } else {
	RtPointer value = pushFloatValue(tokenVector.toRtFloatVector());
	paramMap[key] = {value, count};
      }
    } else if (lookAhead.getType() == GMANToken::LONGINT) {
      GMANToken token = nextToken();
      RtPointer value = pushFloatValue({(RtFloat) token.getLongInt()});
      paramMap[key] = {value, 1};
    } else if (lookAhead.getType() == GMANToken::REAL) {
      GMANToken token = nextToken();
      RtPointer value = pushFloatValue({token.getReal()});
      paramMap[key] = {value, 1};
    } else {
      // This scalar string outlives the local, RAII-managed copyStringToken()
      // result -- it is read back out of paramMap once the whole parameter
      // list has been collected -- so it is duplicated onto the heap and
      // registered with pendingParamValues the same way parseArray's string
      // arrays are, rather than kept as a std::string here.
      const auto str = copyStringToken();
      char *dup = duplicateCString(str).release();
      RtToken *value = new RtToken[1];
      value[0] = dup;
      paramMap[key] = {(RtPointer) value, 1};
      pendingParamValues.push_back({(RtPointer) value, true, 1, {}});
    }
  }

  n = paramMap.size();
  tokens = new RtToken[n];
  parms = new RtPointer[n];
  counts = new RtInt[n];
  ParamMap::iterator cur = paramMap.begin();
  for (unsigned int i = 0; cur != paramMap.end(); i++, cur++) {
    tokens[i] = cur->first;
    parms[i] = cur->second.value;
    counts[i] = (RtInt) cur->second.count;
  }
  pendingTokenArrays.push_back(tokens);
  pendingParmArrays.push_back(parms);
  pendingCountArrays.push_back(counts);
}

RtVoid GMANRIBParse::freePendingParams(RtVoid) {
  pendingParamKeys.clear();

  for (std::vector<PendingParamValue>::iterator it = pendingParamValues.begin();
       it != pendingParamValues.end(); ++it) {
    if (it->isStringArray) {
      RtToken *strings = (RtToken *) it->value;
      for (unsigned int i = 0; i < it->count; i++) {
	delete [] (char *) strings[i];
      }
      delete [] strings;
    }
    // Non-string values live in floatStorage, released by its own
    // destructor when pendingParamValues.clear() runs below.
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

  for (std::vector<RtInt *>::iterator it = pendingCountArrays.begin();
       it != pendingCountArrays.end(); ++it) {
    delete [] *it;
  }
  pendingCountArrays.clear();
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
  for (unsigned int i = 0; i < size(); i++) {
    if ((*this)[i].getType() != GMANToken::STRING) {
      throw(GMANError(RIE_SYNTAX, RIE_ERROR, "Non-string in array."));
    }
  }

  // Every element is a string at this point; each duplicate is held by a
  // unique_ptr until the raw RtToken array is built, so an allocation
  // failure part-way through releases what it already duplicated.
  std::vector<std::unique_ptr<char[]>> owned;
  owned.reserve(size());
  for (unsigned int i = 0; i < size(); i++) {
    owned.push_back(duplicateCString((*this)[i].getString()));
  }

  RtToken* array = new RtToken[size()];
  for (unsigned int i = 0; i < size(); i++) {
    array[i] = owned[i].release();
  }
  return array;
}

std::vector<RtInt> GMANRIBParse::TokenVector::toRtIntVector() {
  std::vector<RtInt> array(size());

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

std::vector<RtFloat> GMANRIBParse::TokenVector::toRtFloatVector() {
  std::vector<RtFloat> array(size());

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
