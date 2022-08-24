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
  FT COLLINS, CO 80525, USA, or write via E-mail john@2ad.com.
*/
 

#ifndef __GMAN_GMANRIBTOKENIZE_H
#define __GMAN_GMANRIBTOKENIZE_H 1


/* Headers */
#include <ctype.h>

// STL
#include <string>
#include <fstream>
#if HAVE_STD_NAMESPACE
using std::ifstream;
#endif

// the renderman interface
#include "ri.h"
// the universal super class declaration
#include "universalsuperclass.h"


// jac 03/31/2002
// win32 doesn't support inner classes in templates
// so I moved these out 
  
  /*
   * Token return type populated by tokenizer.
   */
class GMANDLL  GMANToken : public UniversalSuperClass {
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

 } TokenType;

  private:
    TokenType		type;

    struct TokVals {
      string	stringVal;
      RtFloat	real;
      long	longint;
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
    
    // string ctor
    GMANToken(char *str) { 
      type = STRING; 
      value.stringVal = str; 
    };


    // string ctor
    GMANToken(const string &str) { 
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
      return (const RtToken)value.stringVal.c_str();
    }

    operator RtToken() {
      return getRtToken();
    }

    const string &getString(RtVoid) const {
      return value.stringVal;
    }

    operator	string() {
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

class GMANDLL  GMANRIBTokenize : public UniversalSuperClass {
public:

  static const int bufSz;

private:

  string  		buffer;

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

  const GMANToken parseKeyword(ifstream &ribFile);

  const GMANToken parseString(ifstream &ribFile);

  const GMANToken parseNum(ifstream &ribFile);

  const void consumeWhitespace(ifstream &ribFile) const;

#ifdef WIN32
  // windows does not provide this fn
  int strcasecmp(const char *s1, const char *s2);
#endif

public:

  GMANRIBTokenize(); // default constructor

  ~GMANRIBTokenize(); // default destructor

  const GMANToken	getNext(ifstream &ribFile);
};


#endif

