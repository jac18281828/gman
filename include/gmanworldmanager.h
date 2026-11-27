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

#include "gman/export.h"
#include "gmanowned.h"
#include "gmanprimitive.h"
#include "ri.h"

/*
 * RenderMan API GMANWorldManager
 *
 */

class GMAN_EXPORT GMANWorldManager {
public:
  GMANWorldManager(); // default constructor

  virtual ~GMANWorldManager(); // default destructor

  // Takes ownership of primitive; the world destroys it with itself. An empty owner adds nothing.
  virtual RtVoid add(gman::Owned<GMANPrimitive> primitive) = 0;

  virtual GMANPrimitive* getFirst(RtVoid) = 0; // get the first object

  // get the next object until end (return NULL)
  virtual GMANPrimitive* getNext(RtVoid) = 0;
};
