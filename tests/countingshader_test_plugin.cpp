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
 * Test-only surface shader whose instances keep a live count, read back
 * through GMANCountingShaderLiveCount, this plugin's own exported
 * function: shaderinstance_test.cpp uses it to prove GMANLoadableShader's
 * ownership from outside -- one instance per Surface call, freed exactly
 * when the last Appearance sharing it releases it. Never dlopened outside
 * the test suite.
 */

#include "gmanloadable.h"
#include "gmansurfaceshader.h"

namespace gmanshader {

int& countingShaderLiveCount() {
  static int count = 0;
  return count;
}

class countingshader : public GMANSurfaceShader {
public:
  explicit countingshader(GMANParameterList const& /*parameters*/) { ++countingShaderLiveCount(); }
  ~countingshader() override { --countingShaderLiveCount(); }

  GMANColor computeCi(GMANSurfaceEnv const& se) const override { return se.Cs; }
  GMANColor computeOi(GMANSurfaceEnv const& se) const override { return se.Os; }
};

} // namespace gmanshader

static GMANLoadableObjectInfo loadableInfo = {
    "Live-instance-counting probe (test-only)",
    "John Cairns <john@2ad.com>",
    "Test-only: counts its own live instances so shaderinstance_test.cpp can prove "
    "GMANLoadableShader's ownership from outside. Never dlopened outside the test suite.",
};

extern "C" GMAN_EXPORT GMANLoadableObjectInfo* GMANGetLoadableInfo(void) { return &loadableInfo; }

extern "C" GMAN_EXPORT GMANShader* GMANLoadShader(GMANParameterList const& parameters) {
  return new gmanshader::countingshader(parameters);
}

extern "C" GMAN_EXPORT void GMANDestroyShader(GMANShader* shader) { delete shader; }

extern "C" GMAN_EXPORT int GMANCountingShaderLiveCount() { return gmanshader::countingShaderLiveCount(); }
