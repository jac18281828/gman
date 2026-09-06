/* SPDX-License-Identifier: LGPL-2.1-or-later */

/* This is part of GMAN, a RenderMan-compatible renderer.
 *
 * Copyright (c) 2001, 2000, 1999  John Cairns 
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

/* Local Headers */
#include "ri.h"      /* RenderMan Interface */
#include "gmanlog.h"
#include "gmanface.h" /* Declaration Header */
#include "gmansurface.h"
#include "gmanvector.h"

/*
 * RenderMan API GMANFace
 *
 */

// default constructor
GMANFace::GMANFace(GMANVertex **verts, GMANSurface *p) : color() {
  parentSurf = p;
  area = 0.0f;

  // RenderMan default: both sides visible until RiSides/RiOrientation say
  // otherwise (matches GMANAttributes' own defaults).
  sides = 2;
  orientation = RI_OUTSIDE;

  next = NULL;

  for(int i=0; i<GMAN_NFACE_VERTS; i++) {
    vertices[i] = verts[i];
  };
};


// default destructor 
GMANFace::~GMANFace() { };

RtVoid GMANFace::calcArea(RtVoid) {
  GMANVector result;

  GMANVector va(vertices[0]->getLocation(), vertices[1]->getLocation());
  GMANVector vb(vertices[0]->getLocation(), vertices[2]->getLocation());
  GMANVector vc(vertices[3]->getLocation(), vertices[0]->getLocation());

  result = va.cross(vb);
  area = result.magnitude() / 2.0;
  
  result = vb.cross(vc);
  area += result.magnitude()/2.0;
  
}

RtVoid GMANFace::calcNormal(RtVoid) 
{
  GMANVector va(vertices[0]->getLocation(), vertices[1]->getLocation());
  GMANVector vb(vertices[0]->getLocation(), vertices[2]->getLocation());

  normal = va.cross(vb);

  normal.normalize();

}
