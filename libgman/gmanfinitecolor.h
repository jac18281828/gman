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

#include <cmath>

#include "gmancolor.h"

namespace gman {

// color with every NaN or infinite channel replaced by 0 and every finite
// channel unchanged.
inline GMANColor finiteChannels(GMANColor color) {
  auto const finiteOrZero = [](GMANColor::ColorSampleType channel) {
    return std::isfinite(channel) ? channel : static_cast<GMANColor::ColorSampleType>(0.0);
  };
  return GMANColor(finiteOrZero(color.getRed()), finiteOrZero(color.getGreen()), finiteOrZero(color.getBlue()));
}

} // namespace gman
