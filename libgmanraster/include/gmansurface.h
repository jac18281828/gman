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

#include <cstddef>

#include "gmanerror.h"
#include "gmanface.h"
#include "gmanpoint.h"
#include "gmanray.h"
#include "ri.h"

// forward declaration of body
class GMANBody;

/*
 * RenderMan API GMANSurface
 *
 * A surface is a list of primitive object types.
 *
 * A primitive is a face or quad.
 *
 */

class GMANSurface {
protected:
  GMANPoint center;

  GMANBody* parentBody;

  GMANFace* faceRoot; // faces

  GMANSurface* next; // next face pointer

public:
  // default constructor
  GMANSurface(GMANBody* p) {
    parentBody = p;
    faceRoot = NULL;
    next = NULL;
  };

  ~GMANSurface() {
    GMANFace* faceEle = faceRoot;
    GMANFace* faceNex;

    while (faceEle != NULL) {
      faceNex = faceEle->getNext();
      delete faceEle;
      faceEle = faceNex;
    }
  }; // default destructor

  // test all faces for intersection and return one if it does
  const GMANFace* intersects(const GMANRay& /*ray*/) {
    GMANFace* faceEle = faceRoot;
    while (faceEle != NULL) {
      // FIXME test face for intersection

      faceEle = faceEle->getNext();
    }

    return NULL;
  };

  // return unsent flux
  RtFloat getUnsentFlux(RtVoid) { throw GMANError(RIE_SYSTEM, RIE_ERROR, "Not implemented"); }

  // return face pointer
  GMANFace* getFace(RtVoid) { return faceRoot; };
  GMANFace const* getFace(RtVoid) const { return faceRoot; };
  // set face pointer
  RtVoid setFace(GMANFace* face) { faceRoot = face; };

  // return next surface
  GMANSurface* getNext(RtVoid) { return next; }
  GMANSurface const* getNext(RtVoid) const { return next; }
  // set next surface
  RtVoid setNext(GMANSurface* surface) { next = surface; };

  // return the parent body
  GMANBody* getParent(RtVoid) { return parentBody; };

  const GMANPoint& getCenter(RtVoid) const { return center; };
};
