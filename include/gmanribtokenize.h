/* SPDX-License-Identifier: LGPL-2.1-or-later */

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
*/
 

#ifndef __GMAN_GMANRIBTOKENIZE_H
#define __GMAN_GMANRIBTOKENIZE_H 1


/* Headers */
#include <ctype.h>

// STL
#include <string>
#include <fstream>
#include <istream>

// the renderman interface
#include "ri.h"
// logging
#include "gmanlog.h"


// jac 03/31/2002
// win32 doesn't support inner classes in templates
// so I moved these out 
  
  /*
   * Token return type populated by tokenizer.
   */
class GMAN_EXPORT  GMANToken {
 public:
	 // public types
	 
 typedef enum {
    UNKNOWN,
    STRING,
    REAL,
    LONGINT,
    END_OF_FILE,
    LEFT_BRACKET,
    RIGHT_BRACKET,
    RI_DISPLAY,
    RI_PROJECTION,
    RI_FORMAT,
    RI_IDENTITY,
    RI_ROTATE,
    RI_TRANSLATE,
    RI_SCALE,
    RI_TRANSFORM,
    RI_TRANSFORM_BEGIN,
    RI_TRANSFORM_END,
    RI_CONCAT_TRANSFORM,
    RI_WORLD_BEGIN,
    RI_WORLD_END,
    RI_FRAME_BEGIN,
    RI_FRAME_END,
    RI_LIGHT_SOURCE,
    RI_ATTRIBUTE,
    RI_ATTRIBUTE_BEGIN,
    RI_ATTRIBUTE_END,
    RI_SHADING_INTERPOLATION,
    RI_SURFACE,
    RI_OPACITY,
    RI_DISPLACEMENT,
    RI_DECLARE,
    RI_COLOR,
    RI_SPHERE,
    RI_CONE,
    RI_CYLINDER,
    RI_SIDES,
    RI_HYPERBOLOID,
    RI_PARABOLOID,
    RI_TORUS,
    RI_POLYGON,
    RI_POINTS,
    RI_POINTS_POLYGONS,
    RI_POINTS_GENERAL_POLYGONS,
    RI_DISK,
    RI_PATCH,
    RI_PATCH_MESH,
    RI_NU_PATCH,
    RI_OBJECT_BEGIN,
    RI_OBJECT_END,
    RI_OBJECT_INSTANCE,
    RI_SHADING_RATE,
    RI_SCREEN_WINDOW,
    RI_PIXEL_SAMPLES,
    RI_DEPTH_OF_FIELD,
    RI_SHUTTER,
    RI_EXPOSURE,
    RI_CLIPPING,
    RI_CROP_WINDOW,
    RI_HIDER,
    RI_IMAGER,
    RI_ATMOSPHERE,
    RI_ILLUMINATE,
    RI_BASIS,
    RI_COORDINATE_SYSTEM,
    RI_TEXTURE_COORDINATES,
    RI_MOTION_BEGIN,
    RI_MOTION_END,
    RI_ORIENTATION,
    RI_REVERSE_ORIENTATION,
    RI_VERSION,
    RI_OPTION,
    RI_GEOMETRIC_APPROXIMATION,
    RI_READ_ARCHIVE,

    /* A recognized-but-unimplemented request is a parse error. An
     * unrecognized one is not: it comes back as this, carrying its own name,
     * so the parser can warn once and skip it instead of aborting. */
    RI_UNKNOWN_REQUEST,

    /* RISpec 3.2 requests GMAN's original 66 never covered. Parsed and
     * ignored -- see AGENTS.md's RIB support table. */
    RI_CURVES,
    RI_BLOBBY,
    RI_SUBDIVISION_MESH,
    RI_PROCEDURAL,
    RI_SOLID_BEGIN,
    RI_SOLID_END,
    RI_DETAIL,
    RI_DETAIL_RANGE,
    RI_RELATIVE_DETAIL,
    RI_SKEW,
    RI_MATTE,
    RI_TRIM_CURVE,
    RI_ERROR_HANDLER,
    RI_ARCHIVE_RECORD,
    RI_MAKE_TEXTURE,
    RI_MAKE_BUMP,
    RI_MAKE_LAT_LONG_ENVIRONMENT,
    RI_MAKE_CUBE_FACE_ENVIRONMENT,
    RI_MAKE_SHADOW,
    RI_IF_BEGIN,
    RI_ELSE_IF,
    RI_ELSE,
    RI_IF_END,
    RI_PIXEL_FILTER,

 } TokenType;

  private:
    TokenType		type;

    /* real and longint were left indeterminate by every constructor that did
     * not set them, so reading the wrong arm of this struct read garbage.
     * gcc flags it under -Werror=maybe-uninitialized. */
    struct TokVals {
      std::string	stringVal;
      RtFloat	real{};
      long	longint{};
    } value;
    
  public:
    
    // Empty ctor
    GMANToken() {
      type = UNKNOWN;
    }
    
    // Token ctor
    GMANToken(TokenType t) {
      type = t;
    }

    // Token ctor carrying a name -- RI_UNKNOWN_REQUEST's request name
    GMANToken(TokenType t, const std::string &name) {
      type = t;
      value.stringVal = name;
    }

    // string ctor
    GMANToken(char *str) { 
      type = STRING; 
      value.stringVal = str; 
    };


    // string ctor
    GMANToken(const std::string &str) { 
      type = STRING; 
      value.stringVal = str; 
    };

    // float ctor
    GMANToken(RtFloat f ) { 
      type = REAL; 
      value.real = f; 
    };

    // long ctor
    GMANToken(long l ) { 
      type = LONGINT; 
      value.longint = l; 
    };


    TokenType	getType(RtVoid) const {
      return type;
    };

    operator TokenType() {
      return getType();
    }


    RtToken getRtToken(RtVoid) const {
      return (RtToken)value.stringVal.c_str();
    }

    operator RtToken() {
      return getRtToken();
    }

    const std::string &getString(RtVoid) const {
      return value.stringVal;
    }

    operator	std::string() {
      return getString();
    }

    RtFloat   getReal(RtVoid) const {
      return value.real;
    }

    operator RtFloat() {
      return getReal();
    }

    long      getLongInt(RtVoid) const {
      return value.longint;
    }

    operator long() {
      return getLongInt();
    };

  };


/*
 * RenderMan API gmanribtokenize
 *
 */

class GMAN_EXPORT  GMANRIBTokenize {
public:

  static const int bufSz;

private:

  std::string  		buffer;

  /* private methods */
  bool isKeyToken(char c) const {
    if(isalnum(c))
		return true;
	return false;
  };

  bool isStrToken(char c) const {
    return (c == '\"');
  };

  bool isNumToken(char c) const {
    return (isdigit(c) || (c == '.') || (c == '-') || c == '+' || c == 'e');
  };

  const GMANToken parseKeyword(std::istream &ribFile);

  const GMANToken parseString(std::istream &ribFile);

  const GMANToken parseNum(std::istream &ribFile);

  void consumeWhitespace(std::istream &ribFile) const;


public:

  GMANRIBTokenize(); // default constructor

  ~GMANRIBTokenize(); // default destructor

  // ribFile is any character stream, not necessarily a file -- ReadArchive
  // and gzip decompression both feed this from an in-memory stream.
  const GMANToken	getNext(std::istream &ribFile);
};


#endif

