/* This is part of the GNU GMAN Library, a FREE implementation of the
 * RenderMan Interface Specification.
 *
 * Copyright (c) 2001, 2000, 1999 John Cairns 
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
 

#ifndef __GMAN_GMANSHADER_H
#define __GMAN_GMANSHADER_H 1


/* Headers */
#include <string>

#include "ri.h"
#include "universalsuperclass.h"
#include "gmanparameterlist.h"
#include "gmandictionary.h"

class GMANRenderer;

struct GMANShaderParamInfo
{
  char *name;
  GMANTokenEntry::TokenClass cls;
  GMANTokenEntry::TokenType type;
  RtInt quantity;
  union {
    float *f;
    int *i;
    char *c;
  } def;
  GMANTokenId id;
};

/*
 * RenderMan API GMANShader
 *
 * A user definable and customizable shading object
 * for producing image and rendering customizations 
 * on the fly and at run time.
 *
 */

class GMANDLL  GMANShader : public UniversalSuperClass 
{
public:
  
  // public types
  
  // enumerated shader types
  typedef enum { DISPLACEMENT, VOLUME, IMAGER, LIGHTSOURCE, SURFACE } ShaderType;
  
  
protected:
  GMANParameterList pl;
  GMANRenderer *renderer;
  /* load */
public:
  GMANShader ();
  virtual ~GMANShader ();
  
  virtual RtVoid set (GMANParameterList &p);
  virtual RtVoid set (GMANRenderer &p);
  
  virtual ShaderType getType(RtVoid) const throw(GMANError) = 0;
};


#endif

