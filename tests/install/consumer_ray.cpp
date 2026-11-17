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
 * Proves an installed gman_ray links from its own CMake package with no
 * other gman target on the link line: <gman/gmanrayoccluder.h> resolves
 * through gman::gman_ray's include path, and gman::oneMinus, defined in
 * libgman_ray, returns a GMANColor of gman_core's. The include directory,
 * the core link and the C++ standard all reach this program through
 * gman::gman_ray's public dependency on gman::gman_core.
 */

#include <gman/gmancolor.h>
#include <gman/gmanrayoccluder.h>

int main() {
  GMANColor const opacity(0.25f, 0.5f, 1.0f);
  GMANColor const transmission = gman::oneMinus(opacity);
  if (transmission.getRed() != 0.75f || transmission.getGreen() != 0.5f || transmission.getBlue() != 0.0f) {
    return 1;
  }
  return 0;
}
