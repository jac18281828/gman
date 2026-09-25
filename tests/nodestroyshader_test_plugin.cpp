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
 * Test-only surface shader that defines GMANGetLoadableInfo and
 * GMANLoadShader but no GMANDestroyShader: stands in for a plugin built
 * before GMANDestroyShader existed. GMANLoadShader returns a real
 * instance, never null, as an old plugin's static return always was --
 * the loader already refuses a null return on its own, so a null here
 * would let the missing-GMANDestroyShader refusal go unexercised. Never
 * dlopened outside the test suite.
 */

#include "gmanloadable.h"
#include "gmansurfaceshader.h"

namespace gmanshader {

class nodestroyshader : public GMANSurfaceShader {
public:
  GMANColor computeCi(GMANSurfaceEnv const& se) const override { return se.Cs; }
  GMANColor computeOi(GMANSurfaceEnv const& se) const override { return se.Os; }
};

} // namespace gmanshader

static GMANLoadableObjectInfo loadableInfo = {
    "No-GMANDestroyShader probe (test-only)",
    "John Cairns <john@2ad.com>",
    "Test-only: defines GMANLoadShader but not GMANDestroyShader, standing in for a "
    "plugin built before this change. Never dlopened outside the test suite.",
};

extern "C" GMAN_EXPORT GMANLoadableObjectInfo* GMANGetLoadableInfo(void) { return &loadableInfo; }

extern "C" GMAN_EXPORT GMANShader* GMANLoadShader(GMANParameterList const& /*parameters*/) {
  return new gmanshader::nodestroyshader();
}
