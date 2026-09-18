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

#include <vector>

#include "gmanface.h"
#include "gmanlog.h"
#include "gmanpoint.h"
#include "gmansegment.h"
#include "ri.h"

namespace gman {

// types
typedef std::vector<GMANPoint> PointVector;

/*
 * RenderMan API gman::Polygon
 *
 * A polygon defined by a series of points in space.
 *
 */

class Polygon {
  PointVector points;

public:
  Polygon(); // default constructor

  // create a polygon with n points
  Polygon(int n);

  ~Polygon(); // default destructor

  // add a point to the polygon.
  RtVoid addPoint(const GMANPoint& point);

  // set a point a position n
  RtVoid setPoint(int n, const GMANPoint& point);

  // reveal a point
  GMANPoint& operator[](int n) { return points[n]; };

  // get number of points
  int getNPoints(RtVoid) { return points.size(); };
};

} // namespace gman
