/* This is part of the GNU GMAN Library, a FREE implementation of the
 * RenderMan Interface Specification.
 *
 * Copyright (c) 2001, 2000, 1999 John Cairns 
 *
 * Author: John Cairns <john@2ad.com>
 */

/* LJL - March 2001 - Illuminance added */

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
 

#ifndef __GMAN_GMANSURFACESHADER_H
#define __GMAN_GMANSURFACESHADER_H 1


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

#include "gmanshaderenvironment.h"

/*
 * RenderMan API GMANSurfaceShader
 *
 * A surface shader, associated with a polygon,
 * the surface shader provides a run-time, user
 * configurable means of creating object textures.
 *
 */

class GMANDLL  GMANSurfaceShader : public GMANShader
{
 protected:
  typedef RtVoid (*illuminanceFunc) (GMANVector L, GMANColor Cl, GMANColor Ol); 
 
  vector<illuminanceFunc> istmt;
  
public:
  GMANSurfaceShader(); // default constructor

  virtual ~GMANSurfaceShader(); // default destructor

  ShaderType getType(RtVoid) const throw (GMANError) { return SURFACE; }

  RtVoid illuminance (RtInt i, GMANVector L, GMANColor Cl, GMANColor Ol);

  /*
   * Output of a surface shader
   */

  virtual const GMANColor &computeCi(GMANSurfaceEnv &se)= 0;
  virtual const GMANColor &computeOi(GMANSurfaceEnv &se)= 0;
};


#endif

