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

#include <vector>

#include "gmancolor.h"
#include "gmanlightsourcemgr.h"
#include "gmanmatrix4.h"
#include "gmannormal.h"
#include "gmanocclude.h"
#include "gmanpoint.h"
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

// What a primitive looks like, fixed when it is declared: its surface
// shader (the RISpec's matte default when RiSurface was never called), the
// lights active in its attribute scope (RiIlluminate) and its Cs/Os.
struct GMAN_EXPORT Appearance {
  GMANSurfaceShader* shader = nullptr;
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
};

// Fills a GMANSurfaceEnv from appearance and point and returns the
// shader's Ci. cameraToWorld is the camera's own world-to-camera inverse
// (GMANOptions::getCameraToWorld) -- an argument rather than a member of
// Appearance, since it belongs to the camera, not to what a primitive
// looks like. occluder answers the illuminance loop's "does light reach
// P?" (gmanocclude.h); a caller that omits it keeps every light visible.
GMAN_EXPORT GMANColor shade(Appearance const& appearance, SurfacePoint const& point, GMANMatrix4 const& cameraToWorld,
                            Occluder const* occluder = nullptr);

} // namespace gman
