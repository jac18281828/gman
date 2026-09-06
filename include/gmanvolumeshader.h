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
 

#ifndef __GMAN_GMANVOLUMESHADER_H
#define __GMAN_GMANVOLUMESHADER_H 1


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
#include "gmanshader.h"

#include "gmanshaderenvironment.h"

/*
 * RenderMan API GMANVolumeShader
 *
 * The volume shader implementation
 *
 */

class GMAN_EXPORT  GMANVolumeShader : public GMANShader
{
public:
  GMANVolumeShader(); // default constructor

  virtual ~GMANVolumeShader(); // default destructor

   /* output of volume shader */
  virtual const GMANColor &computeCi(GMANVolumeEnv &ve)=0; 
  virtual const GMANColor &computeOi(GMANVolumeEnv &ve)=0;
};


#endif

