/* SPDX-License-Identifier: LGPL-2.1-or-later */

/* This is part of GMAN, a RenderMan-compatible renderer.
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
 * Test-only loadable shader whose getType() reports GMANShader::DISPLACEMENT
 * and leaves position and normal unchanged: a correctly typed, real,
 * loadable displacement plugin for shadermodulenames_test.cpp's success
 * check. Never dlopened outside the test suite.
 */

#include "gmandisplacementshader.h"
#include "gmanloadable.h"

namespace gmanshader {

class identitydisplacement : public GMANDisplacementShader {
public:
  ShaderType getType(RtVoid) const override { return DISPLACEMENT; }
  const GMANPoint& computeP(GMANDisplacementEnv& de) override { return de.P; }
  const GMANNormal& computeN(GMANDisplacementEnv& de) override { return de.N; }
};

} // namespace gmanshader

static GMANLoadableObjectInfo loadableInfo = {
    "Identity displacement probe (test-only)",
    "John Cairns <john@2ad.com>",
    "Test-only: a correctly typed, real, loadable displacement shader, so "
    "shadermodulenames_test.cpp's success check has a plugin to load. Never "
    "dlopened outside the test suite.",
};

extern "C" GMAN_EXPORT GMANLoadableObjectInfo* GMANGetLoadableInfo(void) { return &loadableInfo; }

extern "C" GMAN_EXPORT GMANShader* GMANLoadShader(GMANParameterList const& /*parameters*/) {
  return new gmanshader::identitydisplacement();
}

extern "C" GMAN_EXPORT void GMANDestroyShader(GMANShader* shader) { delete shader; }
