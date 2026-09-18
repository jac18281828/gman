/* SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Copyright (c) 2026 John Cairns <john@2ad.com>
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

/*
 * Test-only surface shader for environment_test.cpp's "the matrix reaches
 * the shader" proof. A dlopen'd module and its caller share nothing but
 * computeCi's own return value, so this shader encodes three of
 * GMANSurfaceEnv::cameraToWorld's entries -- [0][0], [0][2], [2][0], which
 * a rotation about y leaves distinguishable from identity -- into Ci. Not a
 * shipped shader: dlopened only by environment_test.cpp, never by
 * RiSurface in a real scene.
 */

#include "gmanloadable.h"
#include "gmansurfaceshader.h"

namespace gmanshader {

class camerashader : public GMANSurfaceShader {
public:
  const GMANColor& computeCi(GMANSurfaceEnv& se);
  const GMANColor& computeOi(GMANSurfaceEnv& se);
};

const GMANColor& camerashader::computeCi(GMANSurfaceEnv& se) {
  static GMANColor ci;
  ci = GMANColor(se.cameraToWorld[0][0], se.cameraToWorld[0][2], se.cameraToWorld[2][0]);
  return ci;
}

const GMANColor& camerashader::computeOi(GMANSurfaceEnv& se) {
  static GMANColor oi;
  oi = se.Os;
  return oi;
}

} // namespace gmanshader

static GMANLoadableObjectInfo loadableInfo = {
    "Camera-to-world probe (test-only)",
    "John Cairns <john@2ad.com>",
    "Test-only: encodes GMANSurfaceEnv::cameraToWorld's [0][0], [0][2] and "
    "[2][0] entries into Ci so environment_test.cpp can read camera-to-world "
    "back out of a real shading call. Never dlopened outside the test suite.",
};

static gmanshader::camerashader shader;

extern "C" GMAN_EXPORT GMANLoadableObjectInfo* GMANGetLoadableInfo(void) { return &loadableInfo; }

extern "C" GMAN_EXPORT GMANShader* GMANLoadShader(void) { return &shader; }
