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
 * Test-only surface shader whose Ci and Oi are each either finite (Cs and
 * Os) or a chosen non-finite value, named by its "string ci" and "string oi"
 * parameters: "nan", "inf" or "-inf" in every channel. The opacity also takes
 * "extreme": a finite 3e38 whose sign alternates with u and v, so that a
 * renderer's interpolation between two vertices overflows to infinity and then
 * NaN.
 */

#include <cmath>
#include <limits>
#include <string>

#include "gmanloadable.h"
#include "gmanparameterlist.h"
#include "gmanshaderparams.h"
#include "gmansurfaceshader.h"

namespace gmanshader {

class nonfinitesurface : public GMANSurfaceShader {
public:
  explicit nonfinitesurface(GMANParameterList const& parameters)
      : ciMode(getStringParam(parameters, "ci", "finite")), oiMode(getStringParam(parameters, "oi", "finite")) {}

  GMANColor computeCi(GMANSurfaceEnv const& se) const override { return answer(ciMode, se.Cs); }
  GMANColor computeOi(GMANSurfaceEnv const& se) const override {
    if (oiMode == "extreme") {
      return extremeOpacity(se.u, se.v);
    }
    return answer(oiMode, se.Os);
  }

private:
  static GMANColor extremeOpacity(RtFloat u, RtFloat v) {
    constexpr RtFloat kMagnitude = 3e38f;
    RtFloat value = kMagnitude;
    if ((std::lround(u * 16) + std::lround(v * 16)) % 2 != 0) {
      value = -kMagnitude;
    }
    return GMANColor(value, value, value);
  }

  static GMANColor answer(std::string const& mode, GMANColor const& finite) {
    RtFloat value;
    if (mode == "nan") {
      value = std::numeric_limits<RtFloat>::quiet_NaN();
    } else if (mode == "inf") {
      value = std::numeric_limits<RtFloat>::infinity();
    } else if (mode == "-inf") {
      value = -std::numeric_limits<RtFloat>::infinity();
    } else {
      return finite;
    }
    return GMANColor(value, value, value);
  }

  std::string const ciMode;
  std::string const oiMode;
};

} // namespace gmanshader

static GMANLoadableObjectInfo loadableInfo = {
    "Non-finite Ci and Oi probe (test-only)",
    "John Cairns <john@2ad.com>",
    "Test-only: answers a NaN or infinite Ci or Oi on request so a render can prove a renderer's guard.",
};

extern "C" GMAN_EXPORT GMANLoadableObjectInfo* GMANGetLoadableInfo(void) { return &loadableInfo; }

extern "C" GMAN_EXPORT GMANShader* GMANLoadShader(GMANParameterList const& parameters) {
  return new gmanshader::nonfinitesurface(parameters);
}

extern "C" GMAN_EXPORT void GMANDestroyShader(GMANShader* shader) { delete shader; }
