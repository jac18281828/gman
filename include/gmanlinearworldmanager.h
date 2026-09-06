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
 

#ifndef __GMAN_GMANLINEARWORLDMANAGER_H
#define __GMAN_GMANLINEARWORLDMANAGER_H 1


/* Headers */

// STL
#include <list>
#include <map>
#include <stack>
#include <string>

// the renderman interface
#include "ri.h"
// logging
#include "gmanlog.h"
// Our parent class
#include "gmanworldmanager.h"
// GMANRay def
#include "gmansegment.h"

/*
 * RenderMan API GMANLinearWorldManager
 *
 */

class GMAN_EXPORT GMANLinearWorldManager : public GMANWorldManager {
public:
  // public types
  typedef GMANPrimitive*	ObjectPtr;

  typedef std::list<ObjectPtr>	ObjectList;

private:
  ObjectList			objects;

  ObjectList::iterator		current;
public:
  GMANLinearWorldManager(); // default constructor

  virtual ~GMANLinearWorldManager(); // default destructor
  // Add object
  virtual RtVoid add(ObjectPtr prim);

  // find first
  virtual ObjectPtr getFirst(RtVoid);

  // find each subsequent object until end and then return NULL
  virtual ObjectPtr getNext(RtVoid);

};


#endif

