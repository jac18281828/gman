/* SPDX-License-Identifier: LGPL-2.1-or-later */

/* This is part of GMAN, a RenderMan-compatible renderer.
 *
 * Copyright (c) 2001, 2000, 1999 John Cairns
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

#ifndef __GMAN_TYPES_H
#define __GMAN_TYPES_H 1

#include <list>

#include "ri.h"

/* useful values */
constexpr auto GMAN_BYTEMAX = 255;

/* type declaration */
using GMANByte = unsigned char;

using GMANUInt = unsigned int;

using GMANInt = int;

/* Color sample type */
using GMANColorSample = RtFloat;

/* forward declarations for lists */
class GMANFace;
class GMANSurface;

using GMANFacePtr = GMANFace*;
using GMANSurfacePtr = GMANSurface*;

using GMANFaceList = std::list<GMANFacePtr>;
using GMANSurfaceList = std::list<GMANSurfacePtr>;

#endif
