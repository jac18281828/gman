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
  FT COLLINS, CO 80525, USA, or write via E-mail john@2ad.com.
*/

#include <string>
#if HAVE_STD_NAMESPACE
using std::string;
#endif

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

/* Local Headers */
#include "ri.h"      /* RenderMan Interface */
#include "universalsuperclass.h" /* Super class */
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
GMANRIBTokenize::GMANRIBTokenize() : UniversalSuperClass()
{ 
  
};


// default destructor 
GMANRIBTokenize::~GMANRIBTokenize() { 
};


const GMANToken
GMANRIBTokenize::getNext(ifstream &ribFile) {
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
GMANRIBTokenize::parseKeyword(ifstream &ribFile) {
  char c;
  
  buffer = "";
  while (! ribFile.eof()) {
    ribFile.get(c);
    if (! isKeyToken(c)) {
      ribFile.putback(c);
      break;
    }
    buffer += c;
  }
  
  int tokenLength = buffer.size();
  char *tokenChars = new char[tokenLength + 1];
  buffer.copy(tokenChars, tokenLength);
  tokenChars[tokenLength] = 0;
  
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
  } else {
    debug("Invalid keyword: '%s'", tokenChars);
    GMANError error(RIE_BADFILE,RIE_WARNING,"Unknown keyword in ribfile");
    throw(error);
  }
  // windows requires a return even though its unreached
  return GMANToken(GMANToken::UNKNOWN);
};

const GMANToken
GMANRIBTokenize::parseString(ifstream &ribFile) {
  char c;
  
  buffer = "";
  ribFile.get(c); // strip opening quote

  while (! ribFile.eof()) {
    ribFile.get(c);
    if (c == '\"') {
      break;
    }
    buffer += c;
  }
  
  int tokenLength = buffer.size();
  char *tokenChars = new char[tokenLength + 1];
  buffer.copy(tokenChars, tokenLength);
  tokenChars[tokenLength] = 0;

  return GMANToken(tokenChars);
};

const GMANToken
GMANRIBTokenize::parseNum(ifstream &ribFile) {
  char c;
  
  buffer = "";
  while (! ribFile.eof()) {
    ribFile.get(c);
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

const void GMANRIBTokenize::consumeWhitespace(ifstream &ribFile) const {
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

#ifdef WIN32
  // windows does not provide this fn
int GMANRIBTokenize::strcasecmp(const char *s1, const char *s2) {

	while(*s1 && *s2) {
		if(tolower(*s1)!=tolower(*s2)) {
			return (*s1-*s2);
		}
		s1++;
		s2++;
	}
	return 0;

}
#endif

