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

#ifndef __GMAN_GAMMA_H
#define __GMAN_GAMMA_H 1

#include <cmath>

#include "gmancolor.h"
#include "ri.h"

namespace gman {

// channel' = pow(gain * channel, 1 / gamma), per channel. Runs on the float
// pixel, ahead of narrowing to bytes, so a channel below 1/255 can still
// gamma-lift into a nonzero byte. GMANColor is trivially copyable and three
// floats wide, so it is taken and returned in registers.
inline GMANColor gammaCorrected(GMANColor color, RtFloat gain, RtFloat gamma) {
  static_assert(GMANColor::hasFloatingPointSamples, "gamma correction designed for normalized floating point math");

  if (gain == 1.0f && gamma == 1.0f) {
    return color;
  }

  const RtFloat exponent = 1.0f / gamma;
  color.setRed(static_cast<GMANColor::ColorSampleType>(std::pow(gain * color.getRed(), exponent)));
  color.setGreen(static_cast<GMANColor::ColorSampleType>(std::pow(gain * color.getGreen(), exponent)));
  color.setBlue(static_cast<GMANColor::ColorSampleType>(std::pow(gain * color.getBlue(), exponent)));
  return color;
}

} // namespace gman

#endif
