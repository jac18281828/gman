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

/*
 * Pins CMAKE_CXX_VISIBILITY_PRESET hidden: a declaration tagged GMAN_EXPORT
 * stays in the dynamic symbol table, an untagged one does not. Each
 * assertion is a real consumer's need -- an ri.h data token, a shader
 * plugin's dlsym'd entry points, and the two undocumented Ri* C entry
 * points no in-tree caller links against, so only this test would notice
 * their loss. GMANTIFFWriter::isOpen() const stands for every internal:
 * defined out of line in libgman/gmantiff.cpp and declared untagged in
 * libgman/gmantiff.h, its mangled name holds no standard-library type, so
 * it is identical under libc++ and libstdc++.
 */

#include <cstdio>
#include <string>

#include <dlfcn.h>

#include "check.h"

namespace {

struct Plugin {
  char const* path;
  char const* name;
};

void* openOrFail(char const* path, std::string const& what) {
  void* handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
  check(handle != nullptr, what + ": dlopen succeeds");
  return handle;
}

} // namespace

int main(int argc, char** argv) {
  if (argc != 8) {
    std::fprintf(stderr,
                 "usage: %s <gman_core> <matte> <metal> <paintedplastic> <plastic> "
                 "<shinymetal> <camerashader>\n",
                 argv[0]);
    return 1;
  }

  char const* corePath = argv[1];
  Plugin const plugins[] = {
      {argv[2], "matte"},   {argv[3], "metal"},      {argv[4], "paintedplastic"},
      {argv[5], "plastic"}, {argv[6], "shinymetal"}, {argv[7], "camerashader"},
  };

  if (void* core = openOrFail(corePath, "gman_core")) {
    check(dlsym(core, "RI_P") != nullptr, "ri.h token RI_P exported");
    check(dlsym(core, "RiBezierBasis") != nullptr, "ri.h basis RiBezierBasis exported");
    check(dlsym(core, "_ZNK14GMANTIFFWriter6isOpenEv") == nullptr, "GMANTIFFWriter::isOpen hidden");
    check(dlsym(core, "RiIlluminate") != nullptr, "RiIlluminate stays exported");
    check(dlsym(core, "RiTransformEnd") != nullptr, "RiTransformEnd stays exported");
  }

  for (auto const& plugin : plugins) {
    if (void* handle = openOrFail(plugin.path, plugin.name)) {
      check(dlsym(handle, "GMANGetLoadableInfo") != nullptr,
            std::string(plugin.name) + ": GMANGetLoadableInfo exported");
      check(dlsym(handle, "GMANLoadShader") != nullptr, std::string(plugin.name) + ": GMANLoadShader exported");
    }
  }

  return checkSummary("visibility: hidden by default, GMAN_EXPORT the only gate");
}
