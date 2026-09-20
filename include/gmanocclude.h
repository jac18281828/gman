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
#include "gmanlightsourcemgr.h"
#include "gmanpoint.h"
#include "gmanvector.h"
#include "ri.h"

namespace gman {

/*
 * What a renderer answers when the illuminance loop
 * (GMANSurfaceEnv::diffuse/specular, gmanshaderenvironment.h) asks whether
 * light reaches a point. White is unblocked, black fully blocked; the
 * colour leaves room for a semi-transparent occluder's partial
 * transmission, and the light argument for a per-light shadow switch
 * (RenderMan's Attribute "light" "shadows") or an area light, both without
 * a further signature change. A renderer with nothing to say here -- the
 * z-buffer -- passes no Occluder at all rather than one that always
 * returns white.
 *
 * Ng, unlike the light argument above, already cost this signature a
 * change: offsetting a self-hit's origin is a ray tracer's own numerical
 * business, and P and towardLight alone say nothing about the surface the
 * offset must clear.
 *
 * A shader built outside this tree against an older header, before this
 * class existed, still links and runs: its own diffuse()/specular() were
 * inlined into its binary at that header's revision, so they never read
 * GMANSurfaceEnv's occluder at all -- not because the member defaults to
 * null, but because that shader's own compiled code predates the check
 * entirely. Rebuilding against this header picks it up.
 */
class GMAN_EXPORT Occluder {
public:
  // Defined out of line, in libgman/gmanshading.cpp.
  virtual ~Occluder();

  // towardLight and Ng are each unit length -- towardLight from P toward
  // light, Ng the geometric normal of the surface P sits on. A caller
  // shading a point with no surface passes a zero vector for Ng, which
  // degrades a ray tracer's own origin offset to none rather than a NaN.
  // distance is RI_INFINITY when light has none to report (a distant
  // light); otherwise the length GMANLight::sample's own l carried before
  // the illuminance loop normalized it.
  virtual GMANColor transmission(GMANLight const& light, GMANPoint const& P, GMANVector const& towardLight,
                                 GMANVector const& Ng, RtFloat distance) const = 0;
};

} // namespace gman
