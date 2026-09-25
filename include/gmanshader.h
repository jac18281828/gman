/* SPDX-License-Identifier: LGPL-2.1-or-later */

/* This is part of GMAN, a RenderMan-compatible renderer.
 *
 * Copyright (c) 2001, 2000, 1999 John Cairns
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

#include <string>

#include "gmandictionary.h"
#include "gmanlog.h"
#include "gmanparameterlist.h"
#include "ri.h"

struct GMANShaderParamInfo {
  char* name;
  GMANTokenEntry::TokenClass cls;
  GMANTokenEntry::TokenType type;
  RtInt quantity;
  union {
    float* f;
    int* i;
    char* c;
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

class GMAN_EXPORT GMANShader {
public:
  // public types

  // enumerated shader types
  typedef enum { DISPLACEMENT, VOLUME, IMAGER, LIGHTSOURCE, SURFACE } ShaderType;

  GMANShader();
  virtual ~GMANShader();

  virtual ShaderType getType(RtVoid) const = 0;
};

// A plugin's own pair of entry points, defined with extern "C" linkage so a
// mismatched definition fails to compile rather than silently overloading.
// GMANLoadShader builds one instance from parameters, bound at
// construction, or returns null on failure. The instance outlives the
// Surface call that built it, shared for as long as any Appearance holds
// it; GMANDestroyShader frees it when its last owner releases it.
extern "C" GMAN_EXPORT GMANShader* GMANLoadShader(GMANParameterList const& parameters);
extern "C" GMAN_EXPORT void GMANDestroyShader(GMANShader* shader);
