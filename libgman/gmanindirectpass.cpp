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

#include <string>

#include "gmanerror.h"
#include "gmanindirectpass.h"
#include "gmanloadable.h"
#include "gmanlog.h"

namespace gman {

// Out of line, beside Occluder's and Tracer's (gmanshading.cpp), for the
// same reason: this header is included wherever a host binds a pass, and
// every one of those translation units would otherwise emit a copy of the
// vtable and typeinfo.
IndirectPass::~IndirectPass() = default;

namespace {

using CreateFn = IndirectPass* (*)(void);
using DestroyFn = void (*)(IndirectPass*);

constexpr char const* kCreateFnName = "GMANCreateIndirectPass";
constexpr char const* kDestroyFnName = "GMANDestroyIndirectPass";

void noopDestroy(IndirectPass*) {}

// GMANLoadable's dlopen/dlsym plumbing, protected there for its two
// derived loaders (GMANLoadableRenderer, GMANLoadableShader); loadSymbol
// is exposed here the same way, for an IndirectPass's two entry points
// instead of a shader's or renderer's.
class IndirectPassModule : public GMANLoadable {
public:
  explicit IndirectPassModule(char const* path) : GMANLoadable(path) {}
  using GMANLoadable::loadSymbol;
};

} // namespace

IndirectPassPtr loadIndirectPass(std::string const& name) {
  std::string const path = "lib" + name + ".so";
  try {
    IndirectPassModule module(path.c_str());
    auto const create = reinterpret_cast<CreateFn>(module.loadSymbol(kCreateFnName));
    auto const destroy = reinterpret_cast<DestroyFn>(module.loadSymbol(kDestroyFnName));
    if (create == nullptr) {
      warning("Indirect-light pass \"{}\": {} is missing {}.", name, path, kCreateFnName);
    } else if (destroy == nullptr) {
      warning("Indirect-light pass \"{}\": {} is missing {}.", name, path, kDestroyFnName);
    } else if (IndirectPass* const pass = create(); pass != nullptr) {
      return {pass, destroy};
    } else {
      warning("Indirect-light pass \"{}\": {}'s {} returned null.", name, path, kCreateFnName);
    }
  } catch (GMANError const& error) {
    warning("Indirect-light pass \"{}\": {} ({}).", name, path, error.getMessage());
  }
  return {nullptr, noopDestroy};
}

} // namespace gman
