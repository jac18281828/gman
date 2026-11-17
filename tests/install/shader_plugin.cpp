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
 * A surface shader built outside the gman tree from the installed headers
 * alone, linking gman::gman_core and nothing else. It overrides computeCi and
 * bsdf, builds a Lambert lobe and exports the three entry points a shader
 * plugin defines. The module is built and linked, never loaded.
 */

#include <gman/gmanbsdf.h>
#include <gman/gmanloadable.h>
#include <gman/gmanshader.h>
#include <gman/gmansurfaceshader.h>

namespace gmanshader {

class flat : public GMANSurfaceShader {
public:
  GMANColor computeCi(GMANSurfaceEnv const& se) const override { return se.Cs; }
  GMANColor computeOi(GMANSurfaceEnv const& se) const override { return se.Os; }

  gman::BSDF bsdf(GMANSurfaceEnv const& se) const override {
    gman::BSDF closure(se.N);
    closure.addLambert(se.Cs);
    return closure;
  }
};

} // namespace gmanshader

static GMANLoadableObjectInfo loadableInfo = {
    "Flat surface shader",
    "install test",
    "A surface shader built against an installed gman.",
};

extern "C" GMAN_EXPORT GMANLoadableObjectInfo* GMANGetLoadableInfo(void) { return &loadableInfo; }

extern "C" GMAN_EXPORT GMANShader* GMANLoadShader(GMANParameterList const& /*parameters*/) {
  return new gmanshader::flat();
}

extern "C" GMAN_EXPORT void GMANDestroyShader(GMANShader* shader) { delete shader; }
