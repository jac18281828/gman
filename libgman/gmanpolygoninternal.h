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

#include "gmanpoint.h"
#include "ri.h"

// Shared by gmanpolygon.cpp and gmanpatchpolyobjectmanager.cpp, both built
// into gman_core: internal to libgman, not installed and not GMAN_EXPORT,
// since nothing outside this library needs it.
namespace gman {

// The ring's largest bounding-box side, in whichever of x, y or z spans it
// widest. isDegeneratePolygon and gmanpatchpolyobjectmanager.cpp's own
// coincidence tolerances judge a ring's area or an edge length against
// this extent rather than against an absolute constant, so a sliver a
// million times longer than it is wide reads the same way at any scale.
RtFloat boundingBoxExtent(std::vector<GMANPoint> const& ring);

} // namespace gman
