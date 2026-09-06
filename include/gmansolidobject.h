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
 

#ifndef __GMAN_GMANSOLIDOBJECT_H
#define __GMAN_GMANSOLIDOBJECT_H 1


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
#include "gmanobject.h"

/*
 * RenderMan API GMANSolidObject
 *
 * An implementation of solid object operations
 * including union, difference, etc.
 *
 */

class GMAN_EXPORT  GMANSolidObject : public GMANObject {
public:
  GMANSolidObject(); // default constructor

  ~GMANSolidObject(); // default destructor
};


#endif

