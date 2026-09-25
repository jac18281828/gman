/* SPDX-License-Identifier: LGPL-2.1-or-later */

/* This is part of GMAN, a RenderMan-compatible renderer.
 *
 * Copyright (c) 2001 Ken Geis
 *
 * Author: Ken Geis
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

#include "gmanbsdf.h"
#include "gmanloadable.h"
#include "gmanshaderparams.h"
#include "gmansurfaceshader.h"

/*
 * surface matte(float Ka = 1, Kd = 1)
 * {
 *   normal Nf = faceforward(normalize(N), I);
 *   Oi = Os;
 *   Ci = Os * Cs * (Ka*ambient() + Kd*diffuse(Nf));
 * }
 *
 * The RISpec's own default surface shader; also this renderer's fallback
 * when RiSurface was never called (GMANAttributes::setSurface's
 * fallback).
 */
namespace gmanshader {

class matte : public GMANSurfaceShader {
public:
  explicit matte(GMANParameterList const& parameters)
      : ka(getFloatParam(parameters, RI_KA, 1.0)), kd(getFloatParam(parameters, RI_KD, 1.0)) {}

  RtVoid illuminance(RtInt i, GMANVector L, GMANColor Cl, GMANColor Ol);

  /*
   * Output of a surface shader
   */

  GMANColor computeCi(GMANSurfaceEnv const& se) const override;
  GMANColor computeOi(GMANSurfaceEnv const& se) const override;
  gman::BSDF bsdf(GMANSurfaceEnv const& se) const override;

private:
  RtFloat const ka;
  RtFloat const kd;
};

RtVoid matte::illuminance(RtInt /*i*/, GMANVector /*L*/, GMANColor /*Cl*/, GMANColor /*Ol*/) {
  // Unused: computeCi below sums lights itself via env.ambient()/
  // diffuse(), the C++-shader equivalent of an SL illuminance() loop.
}

GMANColor matte::computeCi(GMANSurfaceEnv const& se) const {
  GMANVector nf = se.faceforward(se.N, se.I, se.Ng);

  GMANColor lit = se.ambient();
  lit.scale(ka);
  GMANColor diff = se.diffuse(nf);
  diff.scale(kd);
  lit += diff;

  return GMANColor(se.Cs.getRed() * se.Os.getRed() * lit.getRed(), se.Cs.getGreen() * se.Os.getGreen() * lit.getGreen(),
                   se.Cs.getBlue() * se.Os.getBlue() * lit.getBlue());
}

GMANColor matte::computeOi(GMANSurfaceEnv const& se) const { return se.Os; }

// One Lambert lobe of Kd * Cs. Ka and Os stay out: ambient light has no
// place in a closure, and Os attenuates visibility, not scattering.
gman::BSDF matte::bsdf(GMANSurfaceEnv const& se) const {
  gman::BSDF closure(se.N);
  closure.addLambert(GMANColor(kd * se.Cs.getRed(), kd * se.Cs.getGreen(), kd * se.Cs.getBlue()));
  return closure;
}

} // namespace gmanshader

static GMANLoadableObjectInfo loadableInfo = {
    "Matte surface shader",
    "Ken Geis",
    "A GMAN SurfaceShader for matte surfaces.",
};

extern "C" GMAN_EXPORT GMANLoadableObjectInfo* GMANGetLoadableInfo(void) { return &loadableInfo; }

extern "C" GMAN_EXPORT GMANShader* GMANLoadShader(GMANParameterList const& parameters) {
  return new gmanshader::matte(parameters);
}

extern "C" GMAN_EXPORT void GMANDestroyShader(GMANShader* shader) { delete shader; }
