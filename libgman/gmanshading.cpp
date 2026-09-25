/* SPDX-License-Identifier: LGPL-2.1-or-later */

/* This is part of GMAN, a RenderMan-compatible renderer.
 *
 * Copyright (c) 2026 John Cairns <john@2ad.com>
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

#include <list>

#include "gmanattributes.h"
#include "gmanloadableshader.h"
#include "gmanshaderenvironment.h"
#include "gmanshading.h"
#include "gmansurfaceshader.h"

namespace gman {

// Out-of-line so this translation unit, not every one that includes
// gmanocclude.h, emits Occluder's vtable and typeinfo.
Occluder::~Occluder() = default;

// Out-of-line beside Occluder's, for the same reason: gmantrace.h is
// included by gmanshaderenvironment.h, which every shading translation
// unit pulls in.
Tracer::~Tracer() = default;

namespace {

// The RISpec's own default: a scene that never calls RiSurface still
// shades, as matte. One instance for the process, built from an empty
// parameter list and shared by every Appearance that needs it. The
// function-local statics hold the module and the aliasing shared_ptr for
// the life of the process.
std::shared_ptr<GMANSurfaceShader const> defaultSurfaceShader() {
  static auto const module = std::make_shared<GMANLoadableShader>("libmatte.so", GMANParameterList());
  static std::shared_ptr<GMANSurfaceShader const> const shader(module, module->getSurface());
  return shader;
}

} // namespace

Appearance appearanceOf(GMANAttributes const& attributes) {
  Appearance appearance;

  std::shared_ptr<GMANSurfaceShader const> shader = attributes.getSurface(0.0);
  appearance.shader = shader ? shader : defaultSurfaceShader();

  std::list<RtLightHandle> const& handles = attributes.getLightList().getHandles();
  for (RtLightHandle const handle : handles) {
    GMANLight const* light = gmanLightSourceMgr().get(handle);
    if (light) {
      appearance.lights.push_back(light);
    }
  }

  appearance.Cs = attributes.getColor();
  appearance.Os = attributes.getOpacity();
  return appearance;
}

Shading shade(Appearance const& appearance, SurfacePoint const& point, GMANMatrix4 const& cameraToWorld,
              Occluder const* occluder, Tracer const* tracer) {
  GMANSurfaceEnv env;
  env.Cs = appearance.Cs;
  env.Os = appearance.Os;
  env.P = point.P;
  env.N = point.N;
  env.Ng = point.Ng;
  env.I = point.I;
  env.E = point.E;
  env.u = point.u;
  env.v = point.v;
  env.s = point.s;
  env.t = point.t;
  env.surfaceMagnitude = point.surfaceMagnitude;
  env.lights = appearance.lights;
  env.cameraToWorld = cameraToWorld;
  env.occluder = occluder;
  env.tracer = tracer;

  Shading shading;
  shading.Ci = appearance.shader->computeCi(env);
  shading.Oi = appearance.shader->computeOi(env);
  return shading;
}

} // namespace gman
