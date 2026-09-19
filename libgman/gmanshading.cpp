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

namespace {

// The RISpec's own default: a scene that never calls RiSurface still
// shades, as matte. One instance, loaded on first use and reused -- dlopen
// once, not once per primitive.
GMANSurfaceShader* defaultSurfaceShader() {
  static GMANLoadableShader loader("libmatte.so");
  static GMANSurfaceShader* shader = loader.getSurface();
  return shader;
}

} // namespace

Appearance appearanceOf(GMANAttributes const& attributes) {
  Appearance appearance;

  // getSurface's const pointer just reflects that GMANAttributes doesn't
  // want its shader pointer reseated through it; computeCi/computeOi are
  // not logically const on the shader instance itself, which is why this
  // casts rather than threading const through shade() below.
  GMANSurfaceShader const* constShader = attributes.getSurface(0.0);
  appearance.shader = constShader ? const_cast<GMANSurfaceShader*>(constShader) : defaultSurfaceShader();

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

GMANColor shade(Appearance const& appearance, SurfacePoint const& point, GMANMatrix4 const& cameraToWorld,
                Occluder const* occluder) {
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
  env.lights = appearance.lights;
  env.cameraToWorld = cameraToWorld;
  env.occluder = occluder;
  return appearance.shader->computeCi(env);
}

} // namespace gman
