/* SPDX-License-Identifier: LGPL-2.1-or-later */

/* This is part of GMAN, a RenderMan-compatible renderer.
 *
 * Copyright (c) 2026 John Cairns
 *
 * Author: John Cairns <john@2ad.com>
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

#include "gman/export.h"
#include "gmancolor.h"
#include "gmanimagershader.h"
#include "gmanloadable.h"
#include "gmanparameterlist.h"
#include "gmanshader.h"
#include "gmanshaderenvironment.h"
#include "gmanshaderparams.h"
#include "ri.h"

/**
 * imager background(color bgcolor = 1, background = 1)
 * {
 *   Ci += (1-alpha) * (bgcolor * background);
 *   Oi = 1;
 *   alpha = 1;
 * }
 *
 * RISpec 3.2's standard background imager, which takes `background` alone,
 * with the `bgcolor` parameter of BMRT's background imager multiplied in.
 * Both default to 1, so a scene passing neither gets a white backdrop.
 */
namespace gmanshader {

namespace {

GMANColor product(GMANColor const& a, GMANColor const& b) {
  return GMANColor(a.getRed() * b.getRed(), a.getGreen() * b.getGreen(), a.getBlue() * b.getBlue());
}

} // namespace

class background : public GMANImagerShader {
public:
  explicit background(GMANParameterList const& parameters)
      : backdrop(product(getColorParam(parameters, RI_BACKGROUND, GMANColor(1.0)),
                         getColorParam(parameters, "bgcolor", GMANColor(1.0)))) {}

  ShaderType getType(RtVoid) const override { return IMAGER; }

  GMANColor const& computeCi(GMANImagerEnv& ie) const override;
  GMANColor const& computeOi(GMANImagerEnv& ie) const override;

private:
  GMANColor const backdrop;
};

GMANColor const& background::computeCi(GMANImagerEnv& ie) const {
  GMANColor uncovered = backdrop;
  uncovered.scale(1 - ie.alpha);
  ie.Ci += uncovered;
  return ie.Ci;
}

GMANColor const& background::computeOi(GMANImagerEnv& ie) const {
  ie.Oi = GMANColor(1.0);
  ie.alpha = 1;
  return ie.Oi;
}

} // namespace gmanshader

static GMANLoadableObjectInfo loadableInfo = {
    "Background imager",
    "John Cairns <john@2ad.com>",
    "A GMAN ImagerShader that places a colour behind each pixel's uncovered part.",
};

extern "C" GMAN_EXPORT GMANLoadableObjectInfo* GMANGetLoadableInfo(void) { return &loadableInfo; }

extern "C" GMAN_EXPORT GMANShader* GMANLoadShader(GMANParameterList const& parameters) {
  return new gmanshader::background(parameters);
}

extern "C" GMAN_EXPORT void GMANDestroyShader(GMANShader* shader) { delete shader; }
