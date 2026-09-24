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

#pragma once

#include <string>

#include "gmanoptions.h"
#include "ri.h"

namespace gman {

// The inputs GMANRenderManImpl::RiWorldBegin resolves before building its
// gman::VSPerspective or gman::VSOrthographic: the projection name, fov,
// screen window, clipping and raster resolution. A second caller building
// its own viewing system from the same GMANOptions gets the same inputs,
// and so projects exactly as RiWorldBegin's own viewing system does.
struct ViewingSystemInputs {
  std::string projectionName;
  RtFloat fov;
  bool fovDefaulted; // fov absent or explicitly 0: substituted with 90
  GMANOptions::ScreenWindowStruct screenWindow;
  GMANOptions::ClippingStruct clipping;
  RtInt xres;
  RtInt yres;
};

ViewingSystemInputs resolveViewingSystemInputs(GMANOptions const& opt);

} // namespace gman
