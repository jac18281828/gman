/* SPDX-License-Identifier: LGPL-2.1-or-later */

/* This is part of GMAN, a RenderMan-compatible renderer.
 *
 * Copyright (c) 2002, 2001, 2000, 1999  John Cairns 
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

/* System Headers */

/* Local Headers */
#include "ri.h"      /* RenderMan Interface */
#include "gmanlog.h"
#include "gmanquantize.h" /* Declaration Header */


/*
 * RenderMan API GMANQuantize
 *
 */

// default constructor
GMANQuantize::GMANQuantize(DisplayMode md,
			   RtInt oneMap,
			   RtInt mn,
			   RtInt mx,
			   RtFloat ditheramp) : mode(md),
    one(oneMap),
    minVal(mn),
    maxVal(mx),
    ditherAmplitude(ditheramp) { 
};


// default destructor 
GMANQuantize::~GMANQuantize() { };


namespace {

// Shared by both doColor overloads so "warn once per process" holds no
// matter which overload a caller reaches first.
bool quantizeWarned = false;

void warnQuantizeUnimplemented() {
    if (!quantizeWarned) {
	error("Color quantization not currently implemented.");
	quantizeWarned = true;
    }
}

} // namespace


GMANColor &GMANQuantize::doColor(GMANColor &col) {
    warnQuantizeUnimplemented();
    return col;
}


GMANColorRGB &GMANQuantize::doColor(GMANColorRGB &col) {
    warnQuantizeUnimplemented();
    return col;
}
