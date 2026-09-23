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

#include "gmancolor.h"
#include "gmanpoint.h"
#include "gmanvector.h"
#include "ri.h"

namespace gman {

/*
 * What a renderer answers when a shader's reflect()/refract() direction
 * needs an actual traced colour rather than a texture lookup
 * (GMANSurfaceEnv::trace, gmanshaderenvironment.h) -- the RSL trace()
 * shadeop's own renderer-side counterpart, mirroring Occluder
 * (gmanocclude.h) exactly.
 *
 * Ng, unlike RSL's plain trace(P, R), offsets the new ray's origin off
 * the surface it leaves -- the same deviation from the spec that
 * Occluder::transmission already takes, for the identical reason: P sits
 * on a real surface, and a ray cast from exactly there can re-hit it.
 */
class GMAN_EXPORT Tracer {
public:
  // Defined out of line, in libgman/gmanshading.cpp.
  virtual ~Tracer();

  // R need not be unit length -- GMANRay's constructor normalizes. Returns
  // whatever colour the cast ray finds: a shaded hit, a renderer's own
  // background on a miss or at its own recursion limit.
  virtual GMANColor trace(GMANPoint const& P, GMANVector const& R, GMANVector const& Ng) const = 0;
};

} // namespace gman
