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

/* Local Headers */
#include "ri.h"      /* RenderMan Interface */
#include "gmanrenderer.h" /* Super class */
#include "gmanloadable.h"
#include "gmanloadablerenderer.h"
#include "gmanraytracerenderer.h" /* Declaration Header */

/* Global static data */

static GMANLoadableObjectInfo loadableInfo = {
  "Ray Traced Lighting Model",
  "John Cairns <john@2ad.com>",
  "Copyright (c) 2001, 2000, 1999 John Cairns, Licensed under the GNU Lesser General Public License v2.1 or later, https://www.gnu.org/licenses/",
  "A GMAN Renderer based on the ray tracing lighting model simulation.",
};

static GMANRaytraceRenderer	renderer;


extern "C" GMAN_EXPORT GMANLoadableObjectInfo *GMANGetLoadableInfo(void) {
  return &loadableInfo;
}

extern "C" GMAN_EXPORT GMANRenderer *GMANLoadRenderer(void) {
  return &renderer;
}


