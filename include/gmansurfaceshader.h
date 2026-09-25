/* SPDX-License-Identifier: LGPL-2.1-or-later */

/* This is part of GMAN, a RenderMan-compatible renderer.
 *
 * Copyright (c) 2001, 2000, 1999 John Cairns
 *
 * Author: John Cairns <john@2ad.com>
 */

/* LJL - March 2001 - Illuminance added */

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

#include <list>
#include <map>
#include <stack>
#include <string>
#include <vector>

#include "gmanlog.h"
#include "gmanshader.h"
#include "gmanshaderenvironment.h"
#include "ri.h"

/*
 * RenderMan API GMANSurfaceShader
 *
 * A surface shader, associated with a polygon,
 * the surface shader provides a run-time, user
 * configurable means of creating object textures.
 *
 */

class GMAN_EXPORT GMANSurfaceShader : public GMANShader {
protected:
  typedef RtVoid (*illuminanceFunc)(GMANVector L, GMANColor Cl, GMANColor Ol);

  std::vector<illuminanceFunc> istmt;

public:
  GMANSurfaceShader(); // default constructor

  virtual ~GMANSurfaceShader(); // default destructor

  ShaderType getType(RtVoid) const { return SURFACE; }

  RtVoid illuminance(RtInt i, GMANVector L, GMANColor Cl, GMANColor Ol);

  /*
   * Output of a surface shader. const: a shader instance is built fresh
   * per Surface call and holds its own parameters, so shading never
   * writes through it.
   */

  virtual GMANColor computeCi(GMANSurfaceEnv const& se) const = 0;
  virtual GMANColor computeOi(GMANSurfaceEnv const& se) const = 0;

  // The diffuse reflectance rho_d, specular and Os excluded, each channel
  // in [0, 1]. Answered per hit, since a texture can vary it across one
  // surface. The default reports Cs, clamped to [0, 1].
  virtual GMANColor albedo(GMANSurfaceEnv const& se) const;
};
