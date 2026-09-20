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
 * R-transparency proof, check 5: a stack of Opacity 0.05 disks along the
 * view axis, one hit each, transparent enough that transmission never
 * crosses the composite loop's 1/255 cutoff within 20 layers (0.95^20 is
 * still far above it) -- only the 16-layer cap can end the loop early.
 * Rendering 20 and 16 such disks therefore produces bit-identical images
 * (the 20-stack's 17th disk onward is never reached), while 8 disks --
 * fewer than the cap -- leaves more transmission for the white
 * background, reading visibly brighter.
 *
 * Disks, not spheres: a solid sphere gives a ray two hits of its own (the
 * near and far side), which this design already composites as two
 * layers -- correct once refraction exists, but not what this check
 * means by "one hit each" while pinning the cap alone. A disk's flat
 * cross section gives exactly one. Each disk's radius covers the whole
 * frame at every depth this stack reaches, and the frame buffer's own
 * pixel filter is a 1x1 box, so every sample -- not only the centre --
 * sees the identical stack and the resolved centre pixel is not a blend
 * across samples that saw different depths.
 *
 * Driven through the public render(), as raydepth_test.cpp is:
 * shadeSample and nearestHit are private, and no RIB parse is needed to
 * build the GMANOptions/viewing system/frame buffer a renderer takes.
 */

#include <cmath>
#include <cstdio>

#include "check.h"
#include "gmanattributes.h"
#include "gmanframebuffer.h"
#include "gmanobjectmanager.h"
#include "gmanoptions.h"
#include "gmanparameterlist.h"
#include "gmanpoint.h"
#include "gmanraytracerenderer.h"
#include "gmantransform.h"
#include "gmanvector.h"
#include "gmanvsperspective.h"
#include "ri.h"

namespace {

constexpr RtInt kRes = 21;
constexpr RtFloat kDiskRadius = 100.0f; // covers the whole frame at every z this stack reaches
constexpr RtFloat kLayerSpacing = 1.0f; // well past the 0.1 composite-gap floor AGENTS.md's own fixtures rely on
constexpr RtFloat kFirstLayerZ = 5.0f;

GMANTransform makeTransform(GMANMatrix4 matrix) {
  GMANOneMatrix storage(matrix);
  return GMANTransform(storage);
}

GMANOptions::ScreenWindowStruct squareScreenWindow() {
  GMANOptions::ScreenWindowStruct sw;
  sw.left = -1.0;
  sw.right = 1.0;
  sw.bottom = -1.0;
  sw.top = 1.0;
  return sw;
}

// Renders layerCount stacked, camera-facing, Opacity-0.05 disks and
// returns the centre pixel's colour.
GMANColor renderStack(int layerCount) {
  GMANOptions options;
  options.setFormat(kRes, kRes, 1.0);
  options.setPixelSamples(1.0, 1.0);
  // A 1x1 box: the resolved pixel is exactly its own single sample, never
  // a blend with a neighbour whose off-axis ray could clear a farther
  // disk's edge and see fewer layers.
  options.setPixelFilter(RiBoxFilter, 1.0, 1.0);

  GMANMatrix4 const identity;
  gman::VSPerspective viewingSys(kRes, kRes, squareScreenWindow(), identity, 90.0, 0.5, 50.0);

  GMANRaytraceRenderer renderer;
  GMANAttributes attr;
  RtColor opacity = {0.05f, 0.05f, 0.05f};
  attr.setOpacity(opacity);

  for (int layer = 0; layer < layerCount; ++layer) {
    RtFloat const z = kFirstLayerZ + (RtFloat)layer * kLayerSpacing;
    GMANMatrix4 place;
    place.trans(0.0, 0.0, z);
    GMANTransform transform = makeTransform(place);
    GMANPrimitive* disk = renderer.getObjectManager()->getRSDisk(0.0, kDiskRadius, 360.0, GMANParameterList(), &options,
                                                                 &attr, &transform);
    renderer.getWorldManager()->add(disk);
  }

  GMANColor const background(1.0f, 1.0f, 1.0f);
  GMANFrameBuffer frameBuffer(kRes, kRes, background);
  renderer.render(&frameBuffer, &viewingSys, options, attr);
  return frameBuffer.getPixel(kRes / 2, kRes / 2);
}

bool colorsEqual(GMANColor const& a, GMANColor const& b, RtFloat tolerance) {
  return std::fabs(a.getRed() - b.getRed()) <= tolerance && std::fabs(a.getGreen() - b.getGreen()) <= tolerance &&
         std::fabs(a.getBlue() - b.getBlue()) <= tolerance;
}

} // namespace

int main() {
  GMANColor const stack8 = renderStack(8);
  GMANColor const stack16 = renderStack(16);
  GMANColor const stack20 = renderStack(20);

  std::printf("centre pixel: 8 layers (%g,%g,%g), 16 layers (%g,%g,%g), 20 layers (%g,%g,%g)\n", stack8.getRed(),
              stack8.getGreen(), stack8.getBlue(), stack16.getRed(), stack16.getGreen(), stack16.getBlue(),
              stack20.getRed(), stack20.getGreen(), stack20.getBlue());

  // Pins the cap at no more than 16: an intentionally over-deep 20-layer
  // stack still stops after the same 16 layers, so its render is
  // bit-identical to the 16-layer one (the 17th disk onward is never
  // reached).
  check(colorsEqual(stack20, stack16, 1e-6f),
        "cap: a 20-layer stack renders identically to a 16-layer one -- only the first 16 are ever composited");

  // Pins the cap at no less than nine: if the cap were 8 or fewer, an
  // 8-layer stack would already be truncated the same way the 16-layer
  // one is, and the two would match. They read clearly apart instead --
  // more transmission survives to the white background at 8 layers.
  check(!colorsEqual(stack8, stack16, 0.02f),
        "cap: an 8-layer stack (below the cap) renders differently from a 16-layer one");

  return checkSummary("R-transparency's layer cap: composite stops at 16 layers, not before and not much beyond");
}
