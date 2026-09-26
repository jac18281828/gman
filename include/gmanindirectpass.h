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

#include <memory>
#include <string>

#include "gmancolor.h"
#include "gmanoptions.h"
#include "gmanray.h"
#include "gmanrayoccluder.h"
#include "gmanvector.h"
#include "gmanworldmanager.h"

namespace gman {

/*
 * Indirect light at a hit, independent of the host that shades it. A host
 * calls irradiance() and hands the value to shading; ambient() adds it
 * and calls nothing. One instance holds one frame's solution, so a host
 * loads a fresh one per render() rather than keeping one across calls.
 *
 * irradiance() reports indirect light alone, since the host computes
 * direct light itself, in the shading units GMANSurfaceEnv::diffuse
 * returns -- the units a GMANRadiositySolver reports H_ind in. It is
 * const and safe to call concurrently once prepare() has returned.
 */
class GMAN_EXPORT IndirectPass {
public:
  // Defined out of line, in libgman/gmanindirectpass.cpp, so libgman
  // emits this class's vtable and typeinfo.
  virtual ~IndirectPass();

  // Readies this pass over world, ahead of any irradiance() call. world is
  // non-const: GMANWorldManager::getFirst/getNext iterate through a
  // cursor, and a pass adds, removes and reorders nothing there. occluder
  // carries the host's BVH; options lets a pass read its settings without
  // a further interface change.
  virtual void prepare(GMANWorldManager& world, GMANRayOccluder const& occluder, GMANOptions const& options) = 0;

  // Indirect light reaching hit from the side I arrives on -- the side
  // facing -I, since a two-sided element can face either way and hit
  // alone does not say which side a ray struck.
  virtual GMANColor irradiance(GMANHit const& hit, GMANVector const& I) const = 0;
};

// The owning pointer loadIndirectPass returns, named once so a host binds
// one type instead of repeating the deleter's signature.
using IndirectPassPtr = std::unique_ptr<IndirectPass, void (*)(IndirectPass*)>;

// Loads name's module (lib<name>.so, as Surface "plastic" loads
// libplastic.so) and returns a new IndirectPass through its
// GMANCreateIndirectPass, deleted through that same module's
// GMANDestroyIndirectPass -- an instance, not a process singleton, freed
// by the module that allocated it. Returns null, after logging one
// warning naming the library and the reason, when the module fails to
// open, lacks either entry point, or its GMANCreateIndirectPass itself
// returns null: a missing pass costs the indirect light, never the frame.
GMAN_EXPORT IndirectPassPtr loadIndirectPass(std::string const& name);

} // namespace gman
