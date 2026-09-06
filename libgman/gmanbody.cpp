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
#include "gmanbody.h" /* Declaration Header */
#include "gmansurface.h"


/*
 * RenderMan API GMANBody
 *
 */

// default constructor
GMANBody::GMANBody(const GMANColor &ref, const GMANColor &emit) { 
  reflectance = ref;
  emittance = emit;

  surfaceRoot = NULL;
  next        = NULL;
};


// default destructor 
GMANBody::~GMANBody() { 
  
  GMANSurface *surface = surfaceRoot;
  GMANSurface *nextSurf;

  while(surface != NULL) // delete all
    {
      // nextSurf must be read before delete: it used to be assigned
      // `surface` itself, which reassigned the freed pointer right back to
      // `surface`, so any body with more than one surface deleted the same
      // freed GMANSurface forever -- a use-after-free on every teardown.
      nextSurf = surface->getNext();
      delete surface;
      surface = nextSurf;
    }
  
};

