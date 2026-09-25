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
 * Test-only imager shader whose instances keep a live count, read back
 * through GMANCountingImagerLiveCount, this plugin's own exported function:
 * imagerownership_test.cpp and shadermodulenames_test.cpp use it to prove
 * GMANOptions::imagerModule's ownership from outside. Never dlopened outside
 * the test suite.
 */

#include "gmanimagershader.h"
#include "gmanloadable.h"

namespace gmanshader {

int& countingImagerLiveCount() {
  static int count = 0;
  return count;
}

class countingimager : public GMANImagerShader {
public:
  explicit countingimager(GMANParameterList const& /*parameters*/) { ++countingImagerLiveCount(); }
  ~countingimager() override { --countingImagerLiveCount(); }

  ShaderType getType(RtVoid) const override { return IMAGER; }
  const GMANColor& computeCi(GMANImagerEnv& ie) override { return ie.Ci; }
  const GMANColor& computeOi(GMANImagerEnv& ie) override { return ie.Oi; }
};

} // namespace gmanshader

static GMANLoadableObjectInfo loadableInfo = {
    "Live-instance-counting imager probe (test-only)",
    "John Cairns <john@2ad.com>",
    "Test-only: counts its own live instances so imagerownership_test.cpp can "
    "prove GMANOptions::imagerModule's ownership from outside. Never dlopened "
    "outside the test suite.",
};

extern "C" GMAN_EXPORT GMANLoadableObjectInfo* GMANGetLoadableInfo(void) { return &loadableInfo; }

extern "C" GMAN_EXPORT GMANShader* GMANLoadShader(GMANParameterList const& parameters) {
  return new gmanshader::countingimager(parameters);
}

extern "C" GMAN_EXPORT void GMANDestroyShader(GMANShader* shader) { delete shader; }

extern "C" GMAN_EXPORT int GMANCountingImagerLiveCount() { return gmanshader::countingImagerLiveCount(); }
