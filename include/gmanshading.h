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

#pragma once

#include <memory>
#include <vector>

#include "gmancolor.h"
#include "gmanlightsourcemgr.h"
#include "gmanmatrix4.h"
#include "gmannormal.h"
#include "gmanocclude.h"
#include "gmanparameterlist.h"
#include "gmanpoint.h"
#include "gmanray.h"
#include "gmanraybbox.h"
#include "gmantrace.h"
#include "gmanvector.h"
#include "ri.h"

class GMANAttributes;
class GMANSurfaceShader;

/*
 * The renderer-facing shading API: whatever more than one renderer needs
 * to shade a surface lives here, public, installed and GMAN_EXPORT, so a
 * renderer plugin -- in this tree or outside it -- keeps only its own
 * algorithm.
 */
namespace gman {

class TextureCache;

// What a primitive looks like, fixed when it is declared: its surface
// shader (the RISpec's matte default when RiSurface was never called,
// already built from its own Surface call's parameters), the lights active
// in its attribute scope (RiIlluminate) and its Cs/Os. Shared ownership,
// not a raw pointer: the ray tracer stores an Appearance in every
// primitive and shades after WorldEnd, when AttributeEnd may have released
// the last GMANAttributes that loaded this shader.
struct GMAN_EXPORT Appearance {
  std::shared_ptr<GMANSurfaceShader const> shader;
  std::vector<GMANLight const*> lights;
  GMANColor Cs;
  GMANColor Os;
};

// Resolves attributes' current surface shader, active lights and Cs/Os
// into an Appearance.
GMAN_EXPORT Appearance appearanceOf(GMANAttributes const& attributes);

// Where on a surface a point sits, and how it is seen -- every field
// camera space. I is the caller's own incident direction, and E its own
// eye position, rather than both assumed at the camera-space origin: an
// orthographic ray, or a secondary ray reflection/refraction casts, does
// not look from there.
struct GMAN_EXPORT SurfacePoint {
  GMANPoint P;
  GMANNormal N;
  GMANNormal Ng;
  GMANVector I;
  GMANPoint E;
  RtFloat u = 0.0;
  RtFloat v = 0.0;
  RtFloat s = 0.0;
  RtFloat t = 0.0;

  // The hit primitive's own camera-space magnitude
  // (gman::primitiveMagnitude, gmanraybbox.h), 0 when
  // P names no real surface -- a renderer's self-shadow offset keys on
  // this alongside |P| itself, so a large primitive near the camera
  // offsets by its own size, not the camera distance alone.
  RtFloat surfaceMagnitude = 0.0;
};

// The SurfacePoint a ray hit implies: P and N/Ng already camera space
// (GMANRayInterface::intersect's own contract), I the ray's own direction
// and E its own origin, rather than both assumed at the camera-space
// origin -- an orthographic ray, or a secondary ray, does not look from
// there. s and t default to u and v, the RISpec's own default
// texture-coordinate mapping. Precondition: a hit, hit.primitive
// non-null. A host's own shading and gman::albedo share this one
// mapping, so the day s and t stop defaulting to u and v, albedo follows
// shading.
GMAN_EXPORT SurfacePoint hitSurfacePoint(GMANRay const& ray, GMANHit const& hit);

// What shading a point produces: colour and opacity together, so a
// front-to-back compositor reads both from one call rather than shading
// the point twice.
struct GMAN_EXPORT Shading {
  GMANColor Ci;
  GMANColor Oi;
};

// Fills a GMANSurfaceEnv from appearance and point and returns the
// shader's Ci and Oi. cameraToWorld is the camera's own world-to-camera
// inverse (GMANOptions::getCameraToWorld) -- an argument rather than a
// member of Appearance, since it belongs to the camera, not to what a
// primitive looks like. occluder answers the illuminance loop's "does
// light reach P?" (gmanocclude.h); a caller that omits it keeps every
// light visible. tracer answers a shader's own trace() calls
// (gmantrace.h); a caller that omits it leaves trace() returning black.
// textureCache is what texture() and environment() sample through; a
// caller that omits it leaves them reading gman::textureCache()'s single
// process cache. A shade re-entered through a Tracer forwards its
// caller's own textureCache, so a reflected or refracted ray keeps
// decoding into the same worker's cache. indirect is the host's answer
// for indirect light at this point (GMANSurfaceEnv::ambient() adds it
// when set); a caller that omits it leaves ambient() reading the ambient
// lights alone.
GMAN_EXPORT Shading shade(Appearance const& appearance, SurfacePoint const& point, GMANMatrix4 const& cameraToWorld,
                          Occluder const* occluder = nullptr, Tracer const* tracer = nullptr,
                          TextureCache* textureCache = nullptr, GMANColor const* indirect = nullptr);

} // namespace gman
