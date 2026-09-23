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

/**
 ** This code based on source presented in Radiosity, A Programmer's
 ** Perspective by Ian Ashdown.
 **/

#include <vector>

#include "gmancolor.h"
#include "gmanlog.h"
#include "gmanpoint.h"
#include "gmanvertex.h"
#include "gmanvertex4.h"
#include "ri.h"

/*
 * RenderMan API GMANOutputPolygon
 *
 */

class GMANOutVertex // Output Vertex
{
private:
  GMANColor color;
  GMANAlpha alpha;
  GMANPoint posn;

public:
  bool operator<(const GMANOutVertex& vert) const {
    GMANVector posVec(posn);
    GMANVector vertPos(vert.posn);
    return ((color < vert.color) && (posVec < vertPos));
  }

  const GMANPoint& getPosn(RtVoid) const { return posn; }
  const GMANColor& getColor(RtVoid) const { return color; }
  const GMANAlpha& getAlpha(RtVoid) const { return alpha; }

  RtVoid set(const GMANVertex4& v) {
    GMANVector4 c = v.getCoord();
    c.perspective(posn);
    color = v.getColor();
    alpha = v.getAlpha();
  }
};

class GMAN_EXPORT GMANOutputPolygon {
private:
  std::vector<GMANOutVertex> vertexVec; // output array

public:
  GMANOutputPolygon(); // default constructor

  ~GMANOutputPolygon(); // default destructor

  int getNumVert(RtVoid) { return vertexVec.size(); };

  const GMANPoint& getVertexPosn(int n) { return vertexVec[n].getPosn(); };

  const GMANColor& getVertexColor(int n) { return vertexVec[n].getColor(); };

  const GMANAlpha& getVertexAlpha(int n) { return vertexVec[n].getAlpha(); };

  RtVoid addVertex(const GMANVertex4& v) {
    GMANOutVertex vert;

    vert.set(v);

    vertexVec.push_back(vert);
  };

  RtVoid reset(RtVoid) { vertexVec.clear(); };
};
