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

#pragma once

#include "gman/export.h"
#include "gmancolor.h"
#include "gmanshader.h"
#include "gmanshaderenvironment.h"

/*
 * RenderMan API GMANImagerShader
 *
 * A shader for use in generating an image backdrop
 *
 * The renderer runs a bound imager once per pixel after exposure and before
 * quantization. Ci arrives premultiplied by coverage and exposed, Oi is the
 * pixel's stored opacity and alpha its coverage. It calls computeCi, then
 * computeOi, so computeCi sees the alpha the pixel arrived with; the alpha the
 * env holds after both calls becomes the pixel's coverage. Both methods are
 * const: a shader holds no state, and the env is how a result comes back.
 *
 */

class GMAN_EXPORT GMANImagerShader : public GMANShader {
public:
  GMANImagerShader(); // default constructor

  virtual ~GMANImagerShader(); // default destructor

  /*
   * output of imager shader */

  virtual const GMANColor& computeCi(GMANImagerEnv& ie) const = 0;
  virtual const GMANColor& computeOi(GMANImagerEnv& ie) const = 0;
};
