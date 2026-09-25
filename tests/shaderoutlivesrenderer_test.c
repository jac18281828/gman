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
 * A plugin's module outlives the RenderMan context that built an instance
 * from it: dlopen'd once, it is never unloaded, so a second RiBegin/RiEnd
 * cycle's own RiSurface resolves the same GMANLoadShader/GMANDestroyShader
 * symbols again and builds a fresh instance through them, and process exit
 * reaches the dlopen'd module once more after main returns. Either path is
 * a defect if it dies; the test's only assertion is that it does not.
 */

#include <stdio.h>

#include "ri.h"

int main(void) {
  for (int cycle = 0; cycle < 2; cycle++) {
    RtFloat ka = 1.0f;

    RiBegin(RI_NULL);
    RiDisplay("shaderoutlivesrenderer.tif", RI_FILE, RI_RGBA, RI_NULL);
    RiWorldBegin();
    RiSurface(RI_PLASTIC, RI_KA, &ka, RI_NULL);
    RiWorldEnd();
    RiEnd();
  }

  printf("two RiBegin/RiEnd cycles with a parameterized shader survived\n");
  return 0;
}
