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
#include "gmanpoint.h"
#include "gmanvector4.h"
#include "ri.h"

class GMANVertex4 {
private:
  GMANColor color;
  GMANAlpha alpha;
  GMANVector4 coord;

public:
  GMANColor const& getColor() const { return color; }

  GMANAlpha const& getAlpha() const { return alpha; }

  GMANVector4 const& getCoord() const { return coord; }

  RtVoid set(GMANPoint const& p, GMANColor const& c, GMANAlpha const& a, RtMatrix const& ptm) {
    coord.projTransform(p, ptm);

    color = c;
    alpha = a;
  }

  RtVoid set(GMANVector4 const& v, GMANColor const& c, GMANAlpha const& a) {
    coord = v;
    color = c;
    alpha = a;
  }
};
