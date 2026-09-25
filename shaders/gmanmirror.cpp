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
#include "gmanshaderparams.h"
#include "gmansurfaceshader.h"

/*
 * surface mirror(float Kr=1)
 * {
 *   normal Nf = faceforward(normalize(N), I);
 *   vector R = reflect(I, Nf);
 *   Oi = Os;
 *   Ci = Os * Cs * Kr * trace(P, R);
 * }
 *
 * A real reflection, through trace() (gmantrace.h), rather than
 * shinymetal's texture environment-map lookup. Kr reuses the existing
 * standard token; see gmanshaderparams.h's own note on why a RIB-side
 * "ior" is not similarly free for glass alongside this shader.
 */
namespace gmanshader {

class mirror : public GMANSurfaceShader {
public:
  GMANColor computeCi(GMANSurfaceEnv const& se) const;
  GMANColor computeOi(GMANSurfaceEnv const& se) const;
};

GMANColor mirror::computeCi(GMANSurfaceEnv const& se) const {
  // Read before the first (only) trace() call: a nested shade() call
  // trace() makes may rebind pl before this call returns (gmanshading.cpp).
  RtFloat const kr = getFloatParam(pl, RI_KR, 1.0);

  GMANVector n(se.N.getX(), se.N.getY(), se.N.getZ());
  n.normalize();
  GMANVector const nf = se.faceforward(n, se.I, se.Ng);
  GMANVector const r = se.reflect(se.I, nf);
  GMANColor const traced = se.trace(r);

  return GMANColor(se.Os.getRed() * se.Cs.getRed() * kr * traced.getRed(),
                   se.Os.getGreen() * se.Cs.getGreen() * kr * traced.getGreen(),
                   se.Os.getBlue() * se.Cs.getBlue() * kr * traced.getBlue());
}

GMANColor mirror::computeOi(GMANSurfaceEnv const& se) const { return se.Os; }

} // namespace gmanshader

static GMANLoadableObjectInfo loadableInfo = {
    "Mirror surface shader",
    "John Cairns <john@2ad.com>",
    "A GMAN SurfaceShader for a real reflection: Kr scales the colour "
    "trace() finds along the reflected ray, rather than shinymetal's "
    "texture environment-map lookup.",
};

static gmanshader::mirror shader;

extern "C" GMAN_EXPORT GMANLoadableObjectInfo* GMANGetLoadableInfo(void) { return &loadableInfo; }

extern "C" GMAN_EXPORT GMANShader* GMANLoadShader(void) { return &shader; }
