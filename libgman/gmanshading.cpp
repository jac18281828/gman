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

#include <algorithm>
#include <list>
#include <memory>

#include "gmanattributes.h"
#include "gmanbbox.h"
#include "gmanbsdf.h"
#include "gmanlightsourcemgr.h"
#include "gmanloadableshader.h"
#include "gmanmath.h"
#include "gmanmatrix4.h"
#include "gmanocclude.h"
#include "gmanprimitive.h"
#include "gmanray.h"
#include "gmanshaderenvironment.h"
#include "gmanshading.h"
#include "gmansurfaceshader.h"
#include "gmantrace.h"
#include "ri.h"

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
    // An area light is read through appearance.areaLight, below, never
    // through this list: a shader's ambient()/diffuse()/specular() loops
    // have no notion of its shape, and must never sample it as if it were
    // a positioned light.
    if (light && light->getType() != GMAN_LIGHT_AREA) {
      appearance.lights.push_back(light);
    }
  }

  // The same "was it on when this primitive was declared" rule the light
  // list above already applies: a pending area light counts only when it
  // is also currently illuminated.
  RtLightHandle const pendingAreaLight = attributes.getAreaLight();
  if (pendingAreaLight != 0 && std::find(handles.begin(), handles.end(), pendingAreaLight) != handles.end()) {
    appearance.areaLight = gmanLightSourceMgr().get(pendingAreaLight);
  }

  appearance.Cs = attributes.getColor();
  appearance.Os = attributes.getOpacity();
  return appearance;
}

SurfacePoint hitSurfacePoint(GMANRay const& ray, GMANHit const& hit) {
  SurfacePoint point;
  point.P = hit.point;
  GMANVector const shading = hit.shadingNormal.value_or(hit.normal);
  GMANVector const geometric = hit.shadingNormal && hit.normal.dot(shading) < (RtFloat)0.0 ? -hit.normal : hit.normal;
  point.N = GMANNormal(shading.getX(), shading.getY(), shading.getZ());
  point.Ng = GMANNormal(geometric.getX(), geometric.getY(), geometric.getZ());
  point.I = ray.getDirection();
  point.E = ray.getOrigin();
  point.u = hit.u;
  point.v = hit.v;
  point.s = hit.u;
  point.t = hit.v;
  point.dPdu = hit.dPdu;
  point.dPdv = hit.dPdv;
  point.surfaceMagnitude = gman::primitiveMagnitude(hit.primitive->getBBox());
  return point;
}

namespace {

// shade's and albedo's shared fill, so the two can never drift apart:
// every field but occluder, tracer and indirect, which each caller binds
// for itself.
GMANSurfaceEnv fillEnv(Appearance const& appearance, SurfacePoint const& point, GMANMatrix4 const& cameraToWorld,
                       TextureCache* textureCache) {
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
  env.dPdu = point.dPdu;
  env.dPdv = point.dPdv;
  env.surfaceMagnitude = point.surfaceMagnitude;
  env.lights = appearance.lights;
  env.cameraToWorld = cameraToWorld;
  env.textureCache = textureCache;
  return env;
}

} // namespace

Shading shade(Appearance const& appearance, SurfacePoint const& point, GMANMatrix4 const& cameraToWorld,
              Occluder const* occluder, Tracer const* tracer, TextureCache* textureCache, GMANColor const* indirect) {
  GMANSurfaceEnv env = fillEnv(appearance, point, cameraToWorld, textureCache);
  env.occluder = occluder;
  env.tracer = tracer;
  env.indirect = indirect;

  Shading shading;
  shading.Ci = appearance.shader->computeCi(env);
  shading.Oi = appearance.shader->computeOi(env);
  return shading;
}

GMANColor albedo(Appearance const& appearance, SurfacePoint const& point, GMANMatrix4 const& cameraToWorld,
                 TextureCache* textureCache) {
  if (!appearance.shader) {
    return GMANColor(GMANClamp(appearance.Cs.getRed(), RtFloat(0.0), RtFloat(1.0)),
                     GMANClamp(appearance.Cs.getGreen(), RtFloat(0.0), RtFloat(1.0)),
                     GMANClamp(appearance.Cs.getBlue(), RtFloat(0.0), RtFloat(1.0)));
  }
  GMANSurfaceEnv const env = fillEnv(appearance, point, cameraToWorld, textureCache);
  return appearance.shader->albedo(env);
}

BSDF bsdf(Appearance const& appearance, SurfacePoint const& point, GMANMatrix4 const& cameraToWorld,
          TextureCache* textureCache) {
  if (!appearance.shader) {
    BSDF closure(point.N);
    closure.addLambert(appearance.Cs);
    return closure;
  }
  GMANSurfaceEnv const env = fillEnv(appearance, point, cameraToWorld, textureCache);
  return appearance.shader->bsdf(env);
}

GMANColor opacity(Appearance const& appearance, SurfacePoint const& point, GMANMatrix4 const& cameraToWorld,
                  TextureCache* textureCache) {
  if (!appearance.shader) {
    return appearance.Os;
  }
  GMANSurfaceEnv const env = fillEnv(appearance, point, cameraToWorld, textureCache);
  return appearance.shader->computeOi(env);
}

} // namespace gman
