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

#pragma once

#include <cmath>

#include "gmanbbox.h"
#include "gmanmath.h"
#include "gmanmatrix4.h"
#include "gmanpoint.h"

namespace gman {

// A box built from RtFloat corner transforms can, by a few ULP, fall just
// inside a hit a double-precision intersector computes (R5c's torus solves
// its quartic in double). Padding every assigned box outward by an epsilon
// scaled to its own largest coordinate magnitude -- with an absolute floor
// for a box near the origin -- means this rounding gap can never exclude a
// real hit. gmanraytracerenderer.cpp's kSelfShadowOffsetScale/
// kSelfShadowOffsetFloor are the precedent for this shape.
constexpr RtFloat kBBoxPadScale = (RtFloat)3.0e-5;
constexpr RtFloat kBBoxPadFloor = (RtFloat)1.0e-9;

// Pads a camera-space box (min, max already componentwise-ordered) outward
// by kBBoxPadScale/kBBoxPadFloor's own epsilon.
inline GMANBBox padBBox(GMANPoint const& min, GMANPoint const& max) {
  RtFloat const coords[6] = {min.getX(), min.getY(), min.getZ(), max.getX(), max.getY(), max.getZ()};
  RtFloat magnitude = 0.0;
  for (RtFloat const coord : coords) {
    magnitude = GMANMax(magnitude, (RtFloat)std::fabs(coord));
  }
  RtFloat const pad = GMANMax(kBBoxPadScale * magnitude, kBBoxPadFloor);
  return GMANBBox(GMANPoint(min.getX() - pad, min.getY() - pad, min.getZ() - pad),
                  GMANPoint(max.getX() + pad, max.getY() + pad, max.getZ() + pad));
}

// Refits an object-space box's eight corners through objectToCamera and
// pads the result (padBBox above): the standard refit, correct for any
// affine placement including a shear or a non-uniform scale, though not
// always the tightest possible box. objectToCamera must be affine -- a
// projective row is not handled here. Every ray primitive but the polygon
// (already camera space at construction) calls this from its own
// transform-taking constructor.
inline GMANBBox cameraSpaceBBox(GMANMatrix4 const& objectToCamera, GMANPoint const& objMin, GMANPoint const& objMax) {
  RtFloat const xs[2] = {objMin.getX(), objMax.getX()};
  RtFloat const ys[2] = {objMin.getY(), objMax.getY()};
  RtFloat const zs[2] = {objMin.getZ(), objMax.getZ()};

  GMANPoint camMin;
  GMANPoint camMax;
  bool first = true;
  for (RtFloat const x : xs) {
    for (RtFloat const y : ys) {
      for (RtFloat const z : zs) {
        GMANPoint const corner = gman::transformPoint(objectToCamera, GMANPoint(x, y, z));
        if (first) {
          camMin = corner;
          camMax = corner;
          first = false;
          continue;
        }
        camMin = GMANPoint(GMANMin(camMin.getX(), corner.getX()), GMANMin(camMin.getY(), corner.getY()),
                           GMANMin(camMin.getZ(), corner.getZ()));
        camMax = GMANPoint(GMANMax(camMax.getX(), corner.getX()), GMANMax(camMax.getY(), corner.getY()),
                           GMANMax(camMax.getZ(), corner.getZ()));
      }
    }
  }

  return padBBox(camMin, camMax);
}

} // namespace gman
