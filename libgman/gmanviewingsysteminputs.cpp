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

#include <memory>

#include "gmandictionary.h"
#include "gmanmatrix4.h"
#include "gmanoptions.h"
#include "gmanviewingsystem.h"
#include "gmanviewingsysteminputs.h"
#include "gmanvsorthographic.h"
#include "gmanvsperspective.h"
#include "ri.h"

namespace gman {

ViewingSystemInputs resolveViewingSystemInputs(GMANOptions const& opt, GMANDictionary& dictionary) {
  ViewingSystemInputs vsi;
  vsi.projectionName = opt.getProjection().name;

  GMANTokenId const fovTok = dictionary.getTokenId(RI_FOV);
  RtFloat const* param = (RtFloat const*)opt.getProjection().pl.getPointer(fovTok);
  vsi.fov = param ? param[0] : (RtFloat)0.0;
  vsi.fovDefaulted = (vsi.fov == (RtFloat)0.0);
  if (vsi.fovDefaulted) {
    vsi.fov = (RtFloat)90.0;
  }

  vsi.screenWindow = opt.getScreenWindow();
  vsi.clipping = opt.getClipping();

  GMANOptions::RasterInfo const ri = opt.getRasterInfo();
  vsi.xres = ri.xres;
  vsi.yres = ri.yres;

  return vsi;
}

std::unique_ptr<GMANViewingSystem> makeViewingSystem(ViewingSystemInputs const& inputs,
                                                     GMANMatrix4 const& worldToCamera) {
  if (inputs.projectionName == "orthographic") {
    return std::make_unique<VSOrthographic>(inputs.xres, inputs.yres, inputs.screenWindow, worldToCamera,
                                            inputs.clipping.nearDist, inputs.clipping.farDist);
  }
  return std::make_unique<VSPerspective>(inputs.xres, inputs.yres, inputs.screenWindow, worldToCamera, inputs.fov,
                                         inputs.clipping.nearDist, inputs.clipping.farDist);
}

} // namespace gman
