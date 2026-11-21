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

#include "gmancolor.h"
#include "gmandefaults.h"
#include "gmanpoint.h"
#include "gmanvector.h"
#include "ri.h"

class GMANVertex {
private:
  GMANPoint location{0.0, 0.0, 0.0};

  GMANVector normal{0.0, 0.0, 0.0};

  GMANColor color = DefaultBGColor;

  // Defaults to DefaultAlpha, opaque: GMANClipEdge::intersect blends the two endpoints' alpha by the clip
  // parameter, so a vertex whose alpha was never set must still hold opaque.
  GMANAlpha alpha = DefaultAlpha;

  GMANVertex* next = nullptr;

public:
  RtVoid setLocation(GMANPoint const& p) { location = p; }
  GMANPoint const& getLocation() const { return location; }

  RtVoid setColor(GMANColor const& c) { color = c; }
  GMANColor const& getColor() const { return color; }

  RtVoid setColor(GMANAlpha const& a) { alpha = a; }
  GMANAlpha const& getAlpha() const { return alpha; }

  RtVoid setNormal(GMANVector const& n) { normal = n; }
  GMANVector const& getNormal() const { return normal; }

  RtVoid setNext(GMANVertex* n) { next = n; }
  GMANVertex* getNext() { return next; }
};
