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
 * Test-only volume shader whose instances keep a live count, read back
 * through GMANCountingVolumeLiveCount, this plugin's own exported function:
 * shadermodulenames_test.cpp uses it to prove that a failed
 * GMANAttributes::setAtmosphere releases the module it replaces, not only
 * the typed pointer bound to it. Never dlopened outside the test suite.
 */

#include "gmanloadable.h"
#include "gmanvolumeshader.h"

namespace gmanshader {

int& countingVolumeLiveCount() {
  static int count = 0;
  return count;
}

class countingvolume : public GMANVolumeShader {
public:
  explicit countingvolume(GMANParameterList const& /*parameters*/) { ++countingVolumeLiveCount(); }
  ~countingvolume() override { --countingVolumeLiveCount(); }

  ShaderType getType(RtVoid) const override { return VOLUME; }
  GMANColor const& computeCi(GMANVolumeEnv& ve) override { return ve.Ci; }
  GMANColor const& computeOi(GMANVolumeEnv& ve) override { return ve.Oi; }
};

} // namespace gmanshader

static GMANLoadableObjectInfo loadableInfo = {
    "Live-instance-counting volume probe (test-only)",
    "John Cairns <john@2ad.com>",
    "Test-only: counts its own live instances so shadermodulenames_test.cpp can "
    "prove a failed setAtmosphere releases the module it replaces. Never "
    "dlopened outside the test suite.",
};

extern "C" GMAN_EXPORT GMANLoadableObjectInfo* GMANGetLoadableInfo(void) { return &loadableInfo; }

extern "C" GMAN_EXPORT GMANShader* GMANLoadShader(GMANParameterList const& parameters) {
  return new gmanshader::countingvolume(parameters);
}

extern "C" GMAN_EXPORT void GMANDestroyShader(GMANShader* shader) { delete shader; }

extern "C" GMAN_EXPORT int GMANCountingVolumeLiveCount() { return gmanshader::countingVolumeLiveCount(); }
