/* SPDX-License-Identifier: LGPL-2.1-or-later
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
 * Proves an installed gman compiles and links from its own CMake package:
 * <gman/ri.h> resolves through the installed interface, gman::gman_core
 * supplies RiBegin/RiEnd, gmanlog.h's <format> use compiles without this
 * project setting its own C++ standard -- gman::gman_core's cxx_std_20
 * usage requirement is what supplies it -- RiTransformEnd and
 * RiIlluminate are declared and callable, and <gman/gmantexture.h>
 * resolves and links gman::TextureCache from the installed tree.
 */

#include <gman/gmanlog.h>
#include <gman/gmantexture.h>
#include <gman/ri.h>

namespace {

bool errored() { return RiLastError != RIE_NOERROR; }

} // namespace

int main() {
  RiBegin(RI_NULL);

  RiTransformBegin();
  if (errored()) {
    return 1;
  }
  RiTransformEnd();
  if (errored()) {
    return 1;
  }

  RtLightHandle const light = RiLightSource("ambientlight", RI_NULL);
  if (errored()) {
    return 1;
  }
  RiIlluminate(light, RI_TRUE);
  if (errored()) {
    return 1;
  }

  RiEnd();

  gman::TextureCache cache;
  GMANColor const black = cache.sample("consumer_test_missing_e8b2f1.tif", 0.5f, 0.5f, gman::TEXTURE_CLAMP);
  if (black.getRed() != 0.0f || black.getGreen() != 0.0f || black.getBlue() != 0.0f) {
    return 1;
  }

  return 0;
}
