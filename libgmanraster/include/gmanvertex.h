/* SPDX-License-Identifier: LGPL-2.1-or-later */

/* This is part of GMAN, a RenderMan-compatible renderer.
 *
 * Copyright (c) 2001, 2000, 1999 by John Cairns
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

#include <list>
#include <map>
#include <stack>
#include <string>

#include "gmancolor.h"
#include "gmandefaults.h"
#include "gmanlog.h"
#include "gmanpoint.h"
#include "gmantypes.h"
#include "gmanvector.h"
#include "ri.h"

/*
 * RenderMan API GMANVertex
 *
 */

class GMANVertex {
private:
  GMANPoint location;

  GMANVector normal;

  GMANColor color;

  GMANAlpha alpha;

  GMANVertex* next; // next vertex

public:
  // default constructor
  GMANVertex();

  ~GMANVertex(); // default destructor

  // set location
  RtVoid setLocation(const GMANPoint& p) { location = p; };
  // get location
  const GMANPoint& getLocation(RtVoid) const { return location; };

  // set color
  RtVoid setColor(const GMANColor& c) { color = c; };
  // get color
  const GMANColor& getColor(RtVoid) const { return color; };

  // set alpha
  RtVoid setColor(const GMANAlpha& a) { alpha = a; };
  // get alpha
  const GMANAlpha& getAlpha(RtVoid) const { return alpha; };

  // set the shading normal
  RtVoid setNormal(const GMANVector& n) { normal = n; };
  // return the normal
  const GMANVector& getNormal(RtVoid) const { return normal; };
  // set next vertex
  RtVoid setNext(GMANVertex* n) { next = n; };
  // return next vertex
  GMANVertex* getNext(RtVoid) { return next; };
};
