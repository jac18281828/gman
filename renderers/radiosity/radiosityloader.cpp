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

#include "gmanindirectpass.h"
#include "gmanloadable.h"
#include "gmanradiositypass.h"

/* Global static data */

static GMANLoadableObjectInfo loadableInfo = {
    "Radiosity indirect-light pass",
    "John Cairns <john@2ad.com>",
    "Dices the ray tracer's world into a radiosity mesh, solves once for each element's indirect "
    "irradiance, and answers it at a hit through gman::IndirectPass.",
};

extern "C" GMAN_EXPORT GMANLoadableObjectInfo* GMANGetLoadableInfo(void) { return &loadableInfo; }

extern "C" GMAN_EXPORT gman::IndirectPass* GMANCreateIndirectPass(void) { return new GMANRadiosityPass(); }

extern "C" GMAN_EXPORT void GMANDestroyIndirectPass(gman::IndirectPass* pass) { delete pass; }
