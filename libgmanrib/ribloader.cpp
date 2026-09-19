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

#include "gmanascii.h"
#include "gmanloadable.h"
#include "gmanrenderman.h"
#include "ri.h"

/* Global static data */

static GMANLoadableObjectInfo loadableInfo = {
    "RIB Writer",
    "John Cairns <john@2ad.com> ",
    "A GMAN RenderMan implementation that writes RIB text.",
};

extern "C" GMAN_EXPORT GMANLoadableObjectInfo* GMANGetLoadableInfo(void) { return &loadableInfo; }

// A new instance per call: each context writes its own file.
extern "C" GMAN_EXPORT GMANRenderMan* GMANLoadRenderMan(void) { return new GMANASCII; }
