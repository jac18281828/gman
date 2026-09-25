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

#include "gmanloadableshader.h"
#include "gmanshader.h"
#include "ri.h"

/*
 * RenderMan API GMANLoadableShader
 *
 */

const char* GMANLoadableShader::LoadShaderFncName = "GMANLoadShader";
const char* GMANLoadableShader::DestroyShaderFncName = "GMANDestroyShader";

// Resolves GMANLoadShader, then GMANDestroyShader, and only then calls
// GMANLoadShader: a plugin missing either symbol is refused before its own
// GMANLoadShader ever runs, since calling a single-instance GMANLoadShader
// through this signature and then destroying its static would be
// undefined behaviour.
GMANLoadableShader::GMANLoadableShader(const char* path, GMANParameterList const& parameters)
    : GMANShader(), GMANLoadable(path), destroyShader(NULL), shader(NULL) {

  LoadShaderFnc loadShader = (LoadShaderFnc)loadSymbol(LoadShaderFncName);

  if (loadShader == NULL) {
    throw(GMANError(RIE_NOSHADER, RIE_SEVERE, "Loadable module missing shader."));
  }

  destroyShader = (DestroyShaderFnc)loadSymbol(DestroyShaderFncName);

  if (destroyShader == NULL) {
    throw(GMANError(RIE_NOSHADER, RIE_SEVERE, "Loadable module missing GMANDestroyShader."));
  }

  shader = loadShader(parameters);

  if (shader == NULL) {
    throw(GMANError(RIE_NOSHADER, RIE_SEVERE, "Loadable module missing shader."));
  }
};

// Reached only once construction finished without throwing, so
// destroyShader and shader are both known good here.
GMANLoadableShader::~GMANLoadableShader() { destroyShader(shader); }

GMANShader::ShaderType GMANLoadableShader::getType(RtVoid) const

{
  if (!shader) {
    throw(GMANError(RIE_NOSHADER, RIE_SEVERE, "No shader available."));
  }
  return shader->getType();
}

GMANImagerShader* GMANLoadableShader::getImager(RtVoid) { return dynamic_cast<GMANImagerShader*>(shader); }

GMANDisplacementShader* GMANLoadableShader::getDisplacement(RtVoid) {
  return dynamic_cast<GMANDisplacementShader*>(shader);
}

GMANLightSourceShader* GMANLoadableShader::getLightSource(RtVoid) {
  return dynamic_cast<GMANLightSourceShader*>(shader);
}

GMANSurfaceShader* GMANLoadableShader::getSurface(RtVoid) { return dynamic_cast<GMANSurfaceShader*>(shader); }

GMANVolumeShader* GMANLoadableShader::getVolume(RtVoid) { return dynamic_cast<GMANVolumeShader*>(shader); }
