/* SPDX-License-Identifier: LGPL-2.1-or-later */

/* This is part of GMAN, a RenderMan-compatible renderer.
 *
 * Copyright (c) 2001, 2000, 1999  John Cairns
 *
 * Author: John Cairns <john@2ad.com>
 */

/* LJL - March 2001 - Illuminance added */

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
#include "gmancolor.h"
#include "gmanmath.h"
#include "gmanshader.h"
#include "gmanshaderenvironment.h"
#include "gmansurfaceshader.h"
#include "gmanvector.h"
#include "ri.h"
#ifdef DEBUG
#include "gmanerror.h"
#endif

/*
 * RenderMan API GMANSurfaceShader
 *
 */

// default constructor
GMANSurfaceShader::GMANSurfaceShader() : GMANShader() {};

// default destructor
GMANSurfaceShader::~GMANSurfaceShader() {};

RtVoid GMANSurfaceShader::illuminance(RtInt i, GMANVector L, GMANColor Cl, GMANColor Ol) {
#ifdef DEBUG
  if (i >= istmt.size()) {
    throw GMANError(RIE_BUG, RIE_SEVERE, "bad illuminance function asked");
  }
#endif
  istmt[i](L, Cl, Ol);
}

GMANColor GMANSurfaceShader::albedo(GMANSurfaceEnv const& se) const {
  GMANColor const rho = bsdf(se).rhoD();
  return GMANColor(GMANClamp(rho.getRed(), 0.0f, 1.0f), GMANClamp(rho.getGreen(), 0.0f, 1.0f),
                   GMANClamp(rho.getBlue(), 0.0f, 1.0f));
}

gman::BSDF GMANSurfaceShader::bsdf(GMANSurfaceEnv const& se) const {
  gman::BSDF closure(se.N);
  closure.addLambert(se.Cs);
  return closure;
}
