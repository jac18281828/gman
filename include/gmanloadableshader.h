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
 

#ifndef __GMAN_GMANLOADABLESHADER_H
#define __GMAN_GMANLOADABLESHADER_H 1


/* Headers */

// STL
#include <list>
#include <map>
#include <stack>
#include <string>

// the renderman interface
#include "ri.h"
// the universal super class declaration
#include "universalsuperclass.h"
// Our parent class
#include "gmanshader.h"
#include "gmandisplacementshader.h"
#include "gmanimagershader.h"
#include "gmanlightsourceshader.h"
#include "gmansurfaceshader.h"
#include "gmanvolumeshader.h"

// also the DSO loader
#include "gmanloadable.h"

/*
 * RenderMan API GMANLoadableShader
 *
 */

class GMANDLL  GMANLoadableShader : public GMANShader, GMANLoadable {
  
public:
  // public types
  typedef GMANShader *	(*LoadShaderFnc)(RtVoid);

  static const char *		LoadShaderFncName;
  
private:
  GMANShader		*shader;
  
public:
// default constructor
  GMANLoadableShader(const char *path) throw(GMANError); 

  virtual ~GMANLoadableShader(); // default destructor


  /*
   * shader interface.
   */

  virtual ShaderType getType(RtVoid) const throw(GMANError);


  /*
   * reinterpret the shader as a specific shader instance
   *
   * Useful for converting the shader from GMANShader to the
   * pointer to the specific shader object after a call to getType
   */

  GMANDisplacementShader *getDisplacement(RtVoid);

  GMANImagerShader       *getImager(RtVoid);

  GMANLightSourceShader  *getLightSource(RtVoid);
  
  GMANSurfaceShader      *getSurface(RtVoid);

  GMANVolumeShader	 *getVolume(RtVoid);

};


#endif

