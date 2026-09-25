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

#include "gmandefaults.h"
#include "gmanlog.h"
#include "gmanvertex.h"
#include "ri.h"

/*
 * RenderMan API GMANVertex
 *
 */

// default constructor
//
// alpha was never initialized here (unlike the other constructor, which
// defaults it to DefaultAlpha) -- harmless while nothing read a
// newly-tessellated vertex's alpha, but GMANClipEdge::intersect's color
// blend (GMANCombine, weighted by e.getAlpha()) reads it as soon as the
// clipper actually runs, turning the garbage into an out-of-range color
// that later corrupts gamma/quantization.
GMANVertex::GMANVertex()
    : location(0.0, 0.0, 0.0), normal(0.0, 0.0, 0.0), color(DefaultBGColor), alpha(DefaultAlpha), next(NULL) {};

// default destructor
GMANVertex::~GMANVertex() {};
