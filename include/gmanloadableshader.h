/* SPDX-License-Identifier: LGPL-2.1-or-later */

/* This is part of GMAN, a RenderMan-compatible renderer.
 *
 * Copyright (c) 2001, 2000, 1999 by John Cairns
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

#pragma once

#include <list>
#include <map>
#include <stack>
#include <string>

#include "gmandisplacementshader.h"
#include "gmanimagershader.h"
#include "gmanlightsourceshader.h"
#include "gmanloadable.h"
#include "gmanlog.h"
#include "gmanshader.h"
#include "gmansurfaceshader.h"
#include "gmanvolumeshader.h"
#include "ri.h"

/*
 * RenderMan API GMANLoadableShader
 *
 */

class GMANLoadableShader : public GMANShader, GMANLoadable {

public:
  // public types
  typedef GMANShader* (*LoadShaderFnc)(RtVoid);

  static const char* LoadShaderFncName;

private:
  GMANShader* shader;

public:
  // default constructor
  GMANLoadableShader(const char* path);

  virtual ~GMANLoadableShader(); // default destructor

  /*
   * shader interface.
   */

  virtual ShaderType getType(RtVoid) const;

  /*
   * reinterpret the shader as a specific shader instance
   *
   * Useful for converting the shader from GMANShader to the
   * pointer to the specific shader object after a call to getType
   */

  GMANDisplacementShader* getDisplacement(RtVoid);

  GMANImagerShader* getImager(RtVoid);

  GMANLightSourceShader* getLightSource(RtVoid);

  GMANSurfaceShader* getSurface(RtVoid);

  GMANVolumeShader* getVolume(RtVoid);
};
