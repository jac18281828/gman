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
 * changing this signature. A renderer with nothing to say here -- the
 * z-buffer -- passes no Occluder at all rather than one that always
 * returns white.
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
  // Out-of-line (libgman/gmanshading.cpp): the one place this class's
  // vtable and typeinfo are emitted, so a renderer outside this tree gets
  // both from libgman rather than a weak copy of its own.
  virtual ~Occluder();

  // towardLight is unit length, from P toward light. distance is
  // RI_INFINITY when light has none to report (a distant light);
  // otherwise the length GMANLight::sample's own l carried before the
  // illuminance loop normalized it.
  virtual GMANColor transmission(GMANLight const& light, GMANPoint const& P, GMANVector const& towardLight,
                                 RtFloat distance) const = 0;
};

} // namespace gman
