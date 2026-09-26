/* SPDX-License-Identifier: LGPL-2.1-or-later
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

/*
 * Test-only indirect-light pass whose GMANCreateIndirectPass always
 * returns null, pinning loadIndirectPass's warning on that failure. Never
 * dlopened outside the test suite.
 */

#include "gmanindirectpass.h"
#include "gmanloadable.h"

static GMANLoadableObjectInfo loadableInfo = {
    "Null-creating indirect-light probe (test-only)",
    "John Cairns <john@2ad.com>",
    "Test-only: GMANCreateIndirectPass always returns null. Never dlopened outside the test suite.",
};

extern "C" GMAN_EXPORT GMANLoadableObjectInfo* GMANGetLoadableInfo(void) { return &loadableInfo; }

extern "C" GMAN_EXPORT gman::IndirectPass* GMANCreateIndirectPass(void) { return nullptr; }

extern "C" GMAN_EXPORT void GMANDestroyIndirectPass(gman::IndirectPass* pass) { delete pass; }
