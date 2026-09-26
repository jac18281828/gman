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
#include <memory>
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
  typedef GMANShader* (*LoadShaderFnc)(GMANParameterList const&);
  typedef RtVoid (*DestroyShaderFnc)(GMANShader*);

  static const char* LoadShaderFncName;
  static const char* DestroyShaderFncName;

private:
  DestroyShaderFnc destroyShader;
  GMANShader* shader;

public:
  // Builds one instance from parameters through the plugin's own
  // GMANLoadShader.
  GMANLoadableShader(const char* path, GMANParameterList const& parameters);

  // Owns the one instance GMANLoadShader returned: frees it through the
  // same plugin's own GMANDestroyShader, never through delete, since the
  // plugin's own allocator built it.
  virtual ~GMANLoadableShader();

  GMANLoadableShader(GMANLoadableShader const&) = delete;
  GMANLoadableShader& operator=(GMANLoadableShader const&) = delete;
  GMANLoadableShader(GMANLoadableShader&&) = delete;
  GMANLoadableShader& operator=(GMANLoadableShader&&) = delete;

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

namespace gman {

// Maps name to "lib" + name + ".so", with no inspection of name, resolves
// it and checks its type against expected. On success, returns the loaded
// module. On any failure -- no module, no GMANLoadShader, no
// GMANDestroyShader, a null shader, or the wrong type -- reports through
// GMANHandleError, quoting name exactly as given, and returns null.
// requestName names the request in the report (for example "Surface");
// fallbackPhrase names what renders in the shader's place.
GMAN_EXPORT std::unique_ptr<GMANLoadableShader>
resolveLoadableShader(std::string const& requestName, std::string const& fallbackPhrase, std::string const& name,
                      GMANParameterList const& parameters, GMANShader::ShaderType expected);

} // namespace gman
