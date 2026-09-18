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
#include "gmantypes.h"
#include "ri.h"

// Corrects color in place: channel' = pow(gain * channel, 1 / gamma), per
// channel. Runs on the float pixel, ahead of narrowing to bytes, so a
// channel below 1/255 can still gamma-lift into a nonzero byte.
inline RtVoid gmanGammaCorrect(GMANColor& color, RtFloat gain, RtFloat gamma) {
  if (gain == 1 && gamma == 1) {
    return;
  }

  const RtFloat exponent = 1 / gamma;
  color.setRed(std::pow(gain * color.getRed(), exponent));
  color.setGreen(std::pow(gain * color.getGreen(), exponent));
  color.setBlue(std::pow(gain * color.getBlue(), exponent));
}

#endif
