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

#pragma once

/*
 * Shared across the path tracer's own area-light test files, built on
 * tests/pathtracerscene.h's own floor scene.
 */

#include "gmanmatrix4.h"
#include "gmantransform.h"
#include "ri.h"

// An emitting sphere or disk's own radiance in a pathtracer area-light test.
inline constexpr RtFloat kAreaLe = 10.0f;

// Wraps matrix as a shutter-open-only placement, for a hand-built
// primitive's own object-to-camera transform.
inline GMANTransform makeTransform(GMANMatrix4 matrix) {
  GMANOneMatrix storage(matrix);
  return GMANTransform(storage);
}
