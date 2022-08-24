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

/* Local Headers */
#include "ri.h"      /* RenderMan Interface */
#include "gmanshader.h" /* Super class */
#include "gmanloadableshader.h" /* Declaration Header */


/*
 * RenderMan API GMANLoadableShader
 *
 */

const char *		GMANLoadableShader::LoadShaderFncName = "GMANLoadShader";

// default constructor
GMANLoadableShader::GMANLoadableShader(const char *path) throw(GMANError) 
  : GMANShader(),  GMANLoadable(path), shader(NULL) { 
  
  LoadShaderFnc loadShader = 
    (LoadShaderFnc)loadSymbol(LoadShaderFncName);
  
  if(loadShader == NULL) {
    throw(GMANError(RIE_NOSHADER, RIE_SEVERE, "Loadable module missing shader."));
  }

  shader = loadShader();

  if(shader == NULL) {
    throw(GMANError(RIE_NOSHADER, RIE_SEVERE, "Loadable module missing shader."));
  }
};


// default destructor 
GMANLoadableShader::~GMANLoadableShader() { };

GMANShader::ShaderType GMANLoadableShader::getType(RtVoid) const 
  throw(GMANError)
{
  if(!shader) {
    throw(GMANError(RIE_NOSHADER, RIE_SEVERE, "No shader available."));
  }
  return shader->getType();
}

GMANImagerShader	*GMANLoadableShader::getImager(RtVoid) {
  return dynamic_cast<GMANImagerShader*>(shader);
}

GMANDisplacementShader	*GMANLoadableShader::getDisplacement(RtVoid) {
  return dynamic_cast<GMANDisplacementShader*>(shader);
}


GMANLightSourceShader	*GMANLoadableShader::getLightSource(RtVoid) {
  return dynamic_cast<GMANLightSourceShader*>(shader);
}


GMANSurfaceShader	*GMANLoadableShader::getSurface(RtVoid) {
  return dynamic_cast<GMANSurfaceShader*>(shader);
}


GMANVolumeShader	*GMANLoadableShader::getVolume(RtVoid) {
  return dynamic_cast<GMANVolumeShader*>(shader);
}
