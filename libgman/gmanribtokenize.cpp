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
*/

#include <string>

#include <climits>   /* INT_MAX -- not transitive via libstdc++ */

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

/* Local Headers */
#include "ri.h"      /* RenderMan Interface */
#include "gmanlog.h"
#include "gmanribtokenize.h" /* Declaration Header */
#include "gmanerror.h"
/*
 * Global static data
 */
const     int   GMANRIBTokenize::bufSz = 1024;

/*
 * RenderMan API gmanribtokenize
 *
 */

// default constructor
GMANRIBTokenize::GMANRIBTokenize() { 
  
};


// default destructor 
GMANRIBTokenize::~GMANRIBTokenize() { 
};


const GMANToken
GMANRIBTokenize::getNext(std::istream &ribFile) {
  char c;

  consumeWhitespace(ribFile);

  if (ribFile.eof()) {
	  return GMANToken(GMANToken::END_OF_FILE);
  }

  c = ribFile.peek();
  
  if (c == '[') {
    ribFile.get(c);
    return GMANToken(GMANToken::LEFT_BRACKET);
  }
  else if (c == ']') {
    ribFile.get(c);
    return GMANToken(GMANToken::RIGHT_BRACKET);
  }
  else if(isNumToken(c)) {
    return parseNum(ribFile);
  } else if(isKeyToken(c)) {
    return parseKeyword(ribFile);
  } else if(isStrToken(c)) {
    return parseString(ribFile);
  }
  else {
    debug("Found character: '%c'", c);
    GMANError error(RIE_BADFILE,RIE_WARNING,"Unknown character in ribfile");
    throw(error);
  }
  // windows requires a return here, even though the code is unreached
  return GMANToken(GMANToken::UNKNOWN);
}

const GMANToken 
GMANRIBTokenize::parseKeyword(std::istream &ribFile) {
  char c;
  
  buffer = "";
  // Loop on get()'s own success, not a prior eof() check: reading the
  // stream's last character does not set eofbit, only the *next* attempt
  // does, and that attempt leaves c untouched on failure. Checking eof()
  // before the read let a failed get() re-test the previous iteration's
  // already-consumed character, duplicating the final character of any
  // token that runs to end of input with no trailing delimiter.
  while (ribFile.get(c)) {
    if (! isKeyToken(c)) {
      ribFile.putback(c);
      break;
    }
    buffer += c;
  }
  
  // buffer.c_str() is already a null-terminated view; no need to copy it
  // onto the heap just to strcasecmp against it.
  const char *tokenChars = buffer.c_str();

  if (! strcmp(tokenChars, "NaN")) {
    return GMANToken((float) sqrt(-1));
  } else if (! strcasecmp(tokenChars, "display")) {
    return GMANToken(GMANToken::RI_DISPLAY);
  } else if (! strcasecmp(tokenChars, "projection")) {
    return GMANToken(GMANToken::RI_PROJECTION);
  } else if (! strcasecmp(tokenChars, "format")) {
    return GMANToken(GMANToken::RI_FORMAT);
  } else if (! strcasecmp(tokenChars, "identity")) {
    return GMANToken(GMANToken::RI_IDENTITY);
  } else if (! strcasecmp(tokenChars, "rotate")) {
    return GMANToken(GMANToken::RI_ROTATE);
  } else if (! strcasecmp(tokenChars, "translate")) {
    return GMANToken(GMANToken::RI_TRANSLATE);
  } else if (! strcasecmp(tokenChars, "scale")) {
    return GMANToken(GMANToken::RI_SCALE);
  } else if (! strcasecmp(tokenChars, "transform")) {
    return GMANToken(GMANToken::RI_TRANSFORM);
  } else if (! strcasecmp(tokenChars, "concatTransform")) {
    return GMANToken(GMANToken::RI_CONCAT_TRANSFORM);
  } else if (! strcasecmp(tokenChars, "transformBegin")) {
    return GMANToken(GMANToken::RI_TRANSFORM_BEGIN);
  } else if (! strcasecmp(tokenChars, "transformEnd")) {
    return GMANToken(GMANToken::RI_TRANSFORM_END);
  } else if (! strcasecmp(tokenChars, "worldBegin")) {
    return GMANToken(GMANToken::RI_WORLD_BEGIN);
  } else if (! strcasecmp(tokenChars, "worldEnd")) {
    return GMANToken(GMANToken::RI_WORLD_END);
  } else if (! strcasecmp(tokenChars, "frameBegin")) {
    return GMANToken(GMANToken::RI_FRAME_BEGIN);
  } else if (! strcasecmp(tokenChars, "frameEnd")) {
    return GMANToken(GMANToken::RI_FRAME_END);
  } else if (! strcasecmp(tokenChars, "lightSource")) {
    return GMANToken(GMANToken::RI_LIGHT_SOURCE);
  } else if (! strcasecmp(tokenChars, "attribute")) {
    return GMANToken(GMANToken::RI_ATTRIBUTE);
  } else if (! strcasecmp(tokenChars, "attributeBegin")) {
    return GMANToken(GMANToken::RI_ATTRIBUTE_BEGIN);
  } else if (! strcasecmp(tokenChars, "attributeEnd")) {
    return GMANToken(GMANToken::RI_ATTRIBUTE_END);
  } else if (! strcasecmp(tokenChars, "shadingInterpolation")) {
    return GMANToken(GMANToken::RI_SHADING_INTERPOLATION);
  } else if (! strcasecmp(tokenChars, "surface")) {
    return GMANToken(GMANToken::RI_SURFACE);
  } else if (! strcasecmp(tokenChars, "opacity")) {
    return GMANToken(GMANToken::RI_OPACITY);
  } else if (! strcasecmp(tokenChars, "displacement")) {
    return GMANToken(GMANToken::RI_DISPLACEMENT);
  } else if (! strcasecmp(tokenChars, "declare")) {
    return GMANToken(GMANToken::RI_DECLARE);
  } else if (! strcasecmp(tokenChars, "color")) {
    return GMANToken(GMANToken::RI_COLOR);
  } else if (! strcasecmp(tokenChars, "sphere")) {
    return GMANToken(GMANToken::RI_SPHERE);
  } else if (! strcasecmp(tokenChars, "cone")) {
    return GMANToken(GMANToken::RI_CONE);
  } else if (! strcasecmp(tokenChars, "cylinder")) {
    return GMANToken(GMANToken::RI_CYLINDER);
  } else if (! strcasecmp(tokenChars, "sides")) {
    return GMANToken(GMANToken::RI_SIDES);
  } else if (! strcasecmp(tokenChars, "hyperboloid")) {
    return GMANToken(GMANToken::RI_HYPERBOLOID);
  } else if (! strcasecmp(tokenChars, "paraboloid")) {
    return GMANToken(GMANToken::RI_PARABOLOID);
  } else if (! strcasecmp(tokenChars, "torus")) {
    return GMANToken(GMANToken::RI_TORUS);
  } else if (! strcasecmp(tokenChars, "polygon")) {
    return GMANToken(GMANToken::RI_POLYGON);
  } else if (! strcasecmp(tokenChars, "points")) {
    return GMANToken(GMANToken::RI_POINTS);
  } else if (! strcasecmp(tokenChars, "pointsPolygons")) {
    return GMANToken(GMANToken::RI_POINTS_POLYGONS);
  } else if (! strcasecmp(tokenChars, "pointsGeneralPolygons")) {
    return GMANToken(GMANToken::RI_POINTS_GENERAL_POLYGONS);
  } else if (! strcasecmp(tokenChars, "disk")) {
    return GMANToken(GMANToken::RI_DISK);
  } else if (! strcasecmp(tokenChars, "patch")) {
    return GMANToken(GMANToken::RI_PATCH);
  } else if (! strcasecmp(tokenChars, "patchMesh")) {
    return GMANToken(GMANToken::RI_PATCH_MESH);
  } else if (! strcasecmp(tokenChars, "nuPatch")) {
    return GMANToken(GMANToken::RI_NU_PATCH);
  } else if (! strcasecmp(tokenChars, "objectBegin")) {
    return GMANToken(GMANToken::RI_OBJECT_BEGIN);
  } else if (! strcasecmp(tokenChars, "objectEnd")) {
    return GMANToken(GMANToken::RI_OBJECT_END);
  } else if (! strcasecmp(tokenChars, "objectInstance")) {
    return GMANToken(GMANToken::RI_OBJECT_INSTANCE);
  } else if (! strcasecmp(tokenChars, "shadingRate")) {
    return GMANToken(GMANToken::RI_SHADING_RATE);
  } else if (! strcasecmp(tokenChars, "screenWindow")) {
    return GMANToken(GMANToken::RI_SCREEN_WINDOW);
  } else if (! strcasecmp(tokenChars, "pixelSamples")) {
    return GMANToken(GMANToken::RI_PIXEL_SAMPLES);
  } else if (! strcasecmp(tokenChars, "depthOfField")) {
    return GMANToken(GMANToken::RI_DEPTH_OF_FIELD);
  } else if (! strcasecmp(tokenChars, "shutter")) {
    return GMANToken(GMANToken::RI_SHUTTER);
  } else if (! strcasecmp(tokenChars, "exposure")) {
    return GMANToken(GMANToken::RI_EXPOSURE);
  } else if (! strcasecmp(tokenChars, "clipping")) {
    return GMANToken(GMANToken::RI_CLIPPING);
  } else if (! strcasecmp(tokenChars, "cropWindow")) {
    return GMANToken(GMANToken::RI_CROP_WINDOW);
  } else if (! strcasecmp(tokenChars, "hider")) {
    return GMANToken(GMANToken::RI_HIDER);
  } else if (! strcasecmp(tokenChars, "imager")) {
    return GMANToken(GMANToken::RI_IMAGER);
  } else if (! strcasecmp(tokenChars, "atmosphere")) {
    return GMANToken(GMANToken::RI_ATMOSPHERE);
  } else if (! strcasecmp(tokenChars, "illuminate")) {
    return GMANToken(GMANToken::RI_ILLUMINATE);
  } else if (! strcasecmp(tokenChars, "basis")) {
    return GMANToken(GMANToken::RI_BASIS);
  } else if (! strcasecmp(tokenChars, "coordinateSystem")) {
    return GMANToken(GMANToken::RI_COORDINATE_SYSTEM);
  } else if (! strcasecmp(tokenChars, "textureCoordinates")) {
    return GMANToken(GMANToken::RI_TEXTURE_COORDINATES);
  } else if (! strcasecmp(tokenChars, "motionBegin")) {
    return GMANToken(GMANToken::RI_MOTION_BEGIN);
  } else if (! strcasecmp(tokenChars, "motionEnd")) {
    return GMANToken(GMANToken::RI_MOTION_END);
  } else if (! strcasecmp(tokenChars, "orientation")) {
    return GMANToken(GMANToken::RI_ORIENTATION);
  } else if (! strcasecmp(tokenChars, "reverseOrientation")) {
    return GMANToken(GMANToken::RI_REVERSE_ORIENTATION);
  } else if (! strcasecmp(tokenChars, "version")) {
    return GMANToken(GMANToken::RI_VERSION);
  } else if (! strcasecmp(tokenChars, "option")) {
    return GMANToken(GMANToken::RI_OPTION);
  } else if (! strcasecmp(tokenChars, "readArchive")) {
    return GMANToken(GMANToken::RI_READ_ARCHIVE);
  } else if (! strcasecmp(tokenChars, "curves")) {
    return GMANToken(GMANToken::RI_CURVES);
  } else if (! strcasecmp(tokenChars, "blobby")) {
    return GMANToken(GMANToken::RI_BLOBBY);
  } else if (! strcasecmp(tokenChars, "subdivisionMesh")) {
    return GMANToken(GMANToken::RI_SUBDIVISION_MESH);
  } else if (! strcasecmp(tokenChars, "procedural")) {
    return GMANToken(GMANToken::RI_PROCEDURAL);
  } else if (! strcasecmp(tokenChars, "solidBegin")) {
    return GMANToken(GMANToken::RI_SOLID_BEGIN);
  } else if (! strcasecmp(tokenChars, "solidEnd")) {
    return GMANToken(GMANToken::RI_SOLID_END);
  } else if (! strcasecmp(tokenChars, "detail")) {
    return GMANToken(GMANToken::RI_DETAIL);
  } else if (! strcasecmp(tokenChars, "detailRange")) {
    return GMANToken(GMANToken::RI_DETAIL_RANGE);
  } else if (! strcasecmp(tokenChars, "relativeDetail")) {
    return GMANToken(GMANToken::RI_RELATIVE_DETAIL);
  } else if (! strcasecmp(tokenChars, "skew")) {
    return GMANToken(GMANToken::RI_SKEW);
  } else if (! strcasecmp(tokenChars, "matte")) {
    return GMANToken(GMANToken::RI_MATTE);
  } else if (! strcasecmp(tokenChars, "trimCurve")) {
    return GMANToken(GMANToken::RI_TRIM_CURVE);
  } else if (! strcasecmp(tokenChars, "errorHandler")) {
    return GMANToken(GMANToken::RI_ERROR_HANDLER);
  } else if (! strcasecmp(tokenChars, "archiveRecord")) {
    return GMANToken(GMANToken::RI_ARCHIVE_RECORD);
  } else if (! strcasecmp(tokenChars, "makeTexture")) {
    return GMANToken(GMANToken::RI_MAKE_TEXTURE);
  } else if (! strcasecmp(tokenChars, "makeBump")) {
    return GMANToken(GMANToken::RI_MAKE_BUMP);
  } else if (! strcasecmp(tokenChars, "makeLatLongEnvironment")) {
    return GMANToken(GMANToken::RI_MAKE_LAT_LONG_ENVIRONMENT);
  } else if (! strcasecmp(tokenChars, "makeCubeFaceEnvironment")) {
    return GMANToken(GMANToken::RI_MAKE_CUBE_FACE_ENVIRONMENT);
  } else if (! strcasecmp(tokenChars, "makeShadow")) {
    return GMANToken(GMANToken::RI_MAKE_SHADOW);
  } else if (! strcasecmp(tokenChars, "ifBegin")) {
    return GMANToken(GMANToken::RI_IF_BEGIN);
  } else if (! strcasecmp(tokenChars, "elseIf")) {
    return GMANToken(GMANToken::RI_ELSE_IF);
  } else if (! strcasecmp(tokenChars, "else")) {
    return GMANToken(GMANToken::RI_ELSE);
  } else if (! strcasecmp(tokenChars, "ifEnd")) {
    return GMANToken(GMANToken::RI_IF_END);
  } else if (! strcasecmp(tokenChars, "pixelFilter")) {
    return GMANToken(GMANToken::RI_PIXEL_FILTER);
  } else if (! strcasecmp(tokenChars, "geometricApproximation")) {
    return GMANToken(GMANToken::RI_GEOMETRIC_APPROXIMATION);
  } else {
    // A request GMAN does not recognize is not a parse error -- production
    // RIB from any real exporter routinely uses requests this front end
    // has never heard of. The parser warns once and skips it; see
    // GMANRIBParse::skipUnknownRequest.
    debug("Unrecognized keyword: '%s'", tokenChars);
    return GMANToken(GMANToken::RI_UNKNOWN_REQUEST, buffer);
  }
};

const GMANToken
GMANRIBTokenize::parseString(std::istream &ribFile) {
  char c;
  
  buffer = "";
  ribFile.get(c); // strip opening quote

  // See parseKeyword: loop on get()'s success, not a prior eof() check, so
  // an unterminated string at end of input can't replay its last character.
  while (ribFile.get(c)) {
    if (c == '\"') {
      break;
    }
    buffer += c;
  }
  
  return GMANToken(buffer);
};

const GMANToken
GMANRIBTokenize::parseNum(std::istream &ribFile) {
  char c;
  
  buffer = "";
  // See parseKeyword: loop on get()'s success, not a prior eof() check, so
  // a number that runs to end of input can't replay its last digit.
  while (ribFile.get(c)) {
    if (! isNumToken(c)) {
      ribFile.putback(c);
      break;
    }
    buffer += c;
  }
  
  // attempt to parse as a long int
  char *endptr;
  long int longIntValue = strtol(buffer.c_str(), &endptr, 10);
  if (*endptr == '\0') {
    return GMANToken(longIntValue);
  } else {
    float floatValue;
    int consumed = sscanf(buffer.c_str(), "%f", &floatValue);
    if (consumed == 1) {
      return GMANToken(floatValue);
    } else {
      GMANError error(RIE_BADFILE,RIE_WARNING,"Unparseable number");
      throw(error);
    }
  }
  // win32 requires return even if its unreached
  return GMANToken(GMANToken::UNKNOWN);
};

void GMANRIBTokenize::consumeWhitespace(std::istream &ribFile) const {
  char c;

  while (! ribFile.eof()) {
    ribFile.get(c);
    
    if (isspace(c)) {
      continue;
    } else if (c == '#') {
      ribFile.ignore(INT_MAX, '\n');
    } else {
      ribFile.putback(c);
      break;
    }
  }

  return;
}


