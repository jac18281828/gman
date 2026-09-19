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
#include "gmanerror.h"
#include "gmanloadable.h"
#include "gmanloadablerenderman.h"
#include "gmanrenderman.h"
#include "ri.h"

namespace gman {

namespace {

// GMANLoadable::loadSymbol is protected; this type exists only to reach it
// and is not itself a GMANRenderMan.
class RenderManLoader : public GMANLoadable {
public:
  using GMANLoadable::GMANLoadable;

  GMANRenderMan* load() {
    typedef GMANRenderMan* (*LoadRenderManFnc)(RtVoid);
    LoadRenderManFnc loadRenderManFnc = reinterpret_cast<LoadRenderManFnc>(loadSymbol("GMANLoadRenderMan"));

    if (loadRenderManFnc == nullptr) {
      throw GMANError(RIE_SYSTEM, RIE_SEVERE, "Loadable module missing GMANLoadRenderMan.");
    }

    GMANRenderMan* renderMan = loadRenderManFnc();
    if (renderMan == nullptr) {
      throw GMANError(RIE_SYSTEM, RIE_SEVERE, "GMANLoadRenderMan returned no RenderMan instance.");
    }
    return renderMan;
  }
};

} // namespace

GMANRenderMan* loadRenderMan(const char* path) {
  RenderManLoader loader(path);
  return loader.load();
}

} // namespace gman
