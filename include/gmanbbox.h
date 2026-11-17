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

#pragma once

#include <cmath>

#include "gmanlog.h"
#include "gmanmath.h"
#include "gmanpoint.h"
#include "ri.h"

/*
 * RenderMan API GMANBBox
 *
 * A simple bounding box
 *
 */

class GMAN_EXPORT GMANBBox {
private:
  RtBound box;

public:
  GMANBBox();
  GMANBBox(RtBound bb);

  // The box spanning min and max, componentwise.
  GMANBBox(GMANPoint const& min, GMANPoint const& max);

  GMANBBox(const GMANBBox& bb) = default;

  GMANBBox& operator=(const GMANBBox& bb);

  GMANBBox& operator=(const RtBound bb);

  operator RtBound&() { return box; };

  GMANPoint getMin() const;
  GMANPoint getMax() const;
};

namespace gman {

// The largest absolute coordinate over box's own six coordinates, 0 for a box
// still at its default +/-RI_INFINITY (a primitive whose own bbox was
// never assigned) -- hitSurfacePoint's own M, gman::SurfacePoint's
// surfaceMagnitude field.
inline RtFloat primitiveMagnitude(GMANBBox const& box) {
  GMANPoint const boxMin = box.getMin();
  GMANPoint const boxMax = box.getMax();
  RtFloat const coords[6] = {boxMin.getX(), boxMin.getY(), boxMin.getZ(), boxMax.getX(), boxMax.getY(), boxMax.getZ()};
  RtFloat magnitude = 0.0;
  for (RtFloat const coord : coords) {
    if (std::fabs(coord) >= RI_INFINITY) {
      return 0.0;
    }
    magnitude = GMANMax(magnitude, (RtFloat)std::fabs(coord));
  }
  return magnitude;
}

} // namespace gman
