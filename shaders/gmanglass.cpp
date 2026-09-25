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

#include "gmanloadable.h"
#include "gmansurfaceshader.h"

/*
 * surface glass()
 * {
 *   normal Nf = faceforward(normalize(N), I);
 *   float eta = (Ng . I < 0) ? 1/ior : ior;
 *   vector Rr = reflect(I, Nf);
 *   vector Rt = refract(I, Nf, eta);
 *   float kr, kt;
 *   fresnel(I, Nf, eta, kr, kt);   // kr=1, kt=0 at TIR, once fixed
 *   Oi = Os;
 *   Ci = Os * (kr*trace(P, Rr) + kt*trace(P, Rt));
 * }
 *
 * No branch for total internal reflection: fresnel() already returns
 * kr=1, kt=0 there, so kt*trace(Rt) contributes exactly zero regardless
 * of what refract()'s own zero-vector return finds.
 */
namespace gmanshader {

// A RIB-side "ior" parameter would need a standard dictionary token
// that does not exist yet (gmanshaderparams.h).
constexpr RtFloat kIor = (RtFloat)1.5;

class glass : public GMANSurfaceShader {
public:
  GMANColor computeCi(GMANSurfaceEnv const& se) const;
  GMANColor computeOi(GMANSurfaceEnv const& se) const;
};

GMANColor glass::computeCi(GMANSurfaceEnv const& se) const {
  GMANVector n(se.N.getX(), se.N.getY(), se.N.getZ());
  n.normalize();
  GMANVector const nf = se.faceforward(n, se.I, se.Ng);

  GMANVector const ng(se.Ng.getX(), se.Ng.getY(), se.Ng.getZ());
  RtFloat const eta = (ng.dot(se.I) < (RtFloat)0.0) ? ((RtFloat)1.0 / kIor) : kIor;

  GMANVector const rr = se.reflect(se.I, nf);
  GMANVector const rt = se.refract(se.I, nf, eta);

  RtFloat kr = 0.0, kt = 0.0;
  se.fresnel(se.I, nf, eta, kr, kt);

  GMANColor const tracedR = se.trace(rr);
  GMANColor const tracedT = se.trace(rt);

  GMANColor const combined(kr * tracedR.getRed() + kt * tracedT.getRed(),
                           kr * tracedR.getGreen() + kt * tracedT.getGreen(),
                           kr * tracedR.getBlue() + kt * tracedT.getBlue());
  return GMANColor(se.Os.getRed() * combined.getRed(), se.Os.getGreen() * combined.getGreen(),
                   se.Os.getBlue() * combined.getBlue());
}

GMANColor glass::computeOi(GMANSurfaceEnv const& se) const { return se.Os; }

} // namespace gmanshader

static GMANLoadableObjectInfo loadableInfo = {
    "Glass surface shader",
    "John Cairns <john@2ad.com>",
    "A GMAN SurfaceShader for real refraction and reflection: fresnel() "
    "weights trace() along the reflected and refracted rays, ior fixed at "
    "1.5.",
};

extern "C" GMAN_EXPORT GMANLoadableObjectInfo* GMANGetLoadableInfo(void) { return &loadableInfo; }

// glass reads no parameter (see its own header comment).
extern "C" GMAN_EXPORT GMANShader* GMANLoadShader(GMANParameterList const& /*parameters*/) {
  return new gmanshader::glass();
}

extern "C" GMAN_EXPORT void GMANDestroyShader(GMANShader* shader) { delete shader; }
