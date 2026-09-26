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

#include <cctype>
#include <memory>
#include <string>

#include "gmanerror.h"
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

namespace gman {

namespace {

// The article-and-noun phrase for the wrong-type report. Only the types
// resolveLoadableShader is ever asked to expect appear here; every other
// GMANShader::ShaderType is unreachable through this function.
std::string const& nounPhraseFor(GMANShader::ShaderType type) {
  static std::string const surface = "a surface";
  static std::string const displacement = "a displacement";
  static std::string const volume = "a volume";
  static std::string const imager = "an imager";
  switch (type) {
  case GMANShader::SURFACE:
    return surface;
  case GMANShader::DISPLACEMENT:
    return displacement;
  case GMANShader::VOLUME:
    return volume;
  case GMANShader::IMAGER:
    return imager;
  case GMANShader::LIGHTSOURCE:
    break;
  }
  throw(GMANError(RIE_BUG, RIE_SEVERE, "resolveLoadableShader never expects a light source."));
}

} // namespace

std::unique_ptr<GMANLoadableShader> resolveLoadableShader(std::string const& requestName,
                                                          std::string const& fallbackPhrase, std::string const& name,
                                                          GMANParameterList const& parameters,
                                                          GMANShader::ShaderType expected) {
  std::string const objectName = "lib" + name + ".so";
  try {
    auto resolved = std::make_unique<GMANLoadableShader>(objectName.c_str(), parameters);
    if (resolved->getType() != expected) {
      std::string requestedKind = requestName;
      requestedKind.front() = static_cast<char>(std::tolower(static_cast<unsigned char>(requestedKind.front())));
      std::string const message =
          "Specified " + requestedKind + " shader is not " + nounPhraseFor(expected) + " shader.";
      throw(GMANError(RIE_NOSHADER, RIE_SEVERE, message.c_str()));
    }
    return resolved;
  } catch (GMANError const& loadError) {
    std::string const message =
        requestName + " \"" + name + "\" failed to load; " + fallbackPhrase + ": " + loadError.getMessage();
    GMANError fallback(RIE_NOSHADER, RIE_ERROR, message.c_str());
    GMANHandleError(fallback);
    return nullptr;
  }
}

} // namespace gman
