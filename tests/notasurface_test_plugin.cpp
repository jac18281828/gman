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
 * Test-only loadable shader whose getType() reports GMANShader::VOLUME.
 * Every shipped shader plugin is a surface, so nothing else in the tree
 * reaches GMANAttributes::setSurface's "loaded, but not a surface" branch.
 * Not a shipped shader: dlopened only by unknownsurface_test.cpp, never by
 * RiSurface in a real scene.
 */

#include "gmanloadable.h"
#include "gmanvolumeshader.h"

namespace gmanshader {

class notasurface : public GMANVolumeShader {
public:
  ShaderType getType(RtVoid) const { return VOLUME; }
  GMANColor const& computeCi(GMANVolumeEnv& ve);
  GMANColor const& computeOi(GMANVolumeEnv& ve);
};

GMANColor const& notasurface::computeCi(GMANVolumeEnv& ve) { return ve.Ci; }

GMANColor const& notasurface::computeOi(GMANVolumeEnv& ve) { return ve.Oi; }

} // namespace gmanshader

static GMANLoadableObjectInfo loadableInfo = {
    "Not-a-surface probe (test-only)",
    "John Cairns <john@2ad.com>",
    "Test-only: a volume shader loaded through RiSurface, so setSurface's "
    "\"loaded, but not a surface\" branch has a plugin to reach it with. "
    "Never dlopened outside the test suite.",
};

extern "C" GMAN_EXPORT GMANLoadableObjectInfo* GMANGetLoadableInfo(void) { return &loadableInfo; }

extern "C" GMAN_EXPORT GMANShader* GMANLoadShader(GMANParameterList const& /*parameters*/) {
  return new gmanshader::notasurface();
}

extern "C" GMAN_EXPORT void GMANDestroyShader(GMANShader* shader) { delete shader; }
