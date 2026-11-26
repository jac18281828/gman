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

#include "gmanbitmap.h"
#include "gmanimagershader.h"
#include "gmanoptions.h"

namespace gman {

// Runs imager once per pixel over image, after exposure: each pixel's stored
// colour passes through the options' gain and gamma, then through computeCi
// and computeOi with its premultiplied colour, stored opacity, coverage (the
// mean of its alpha channels), raster position and shutter times. The
// returned colour replaces the pixel's colour, exposed, a NaN or infinite
// channel of it read as 0. A NaN or infinite channel of the exposed colour
// reaches the imager as 0, which a negative channel under a gamma other than 1
// would otherwise make NaN. The returned alpha replaces the pixel's alpha in
// all three channels where it differs from the coverage the pixel arrived
// with, and leaves the stored alpha untouched elsewhere.
void runImager(GMANBitmap& image, GMANImagerShader const& imager, GMANOptions const& options);

} // namespace gman
