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
 * Two camera-facing, Opacity-0.4 disks separated by 0.01, close enough
 * that a magnitude-scaled self-shadow offset at this depth could
 * plausibly swallow the gap between them -- must both composite. Driven
 * through the public render(), as compositelayercap_test.cpp is:
 * shadeSample and nearestHit are private, and no RIB parse is needed to
 * build the GMANOptions/viewing system/frame buffer a renderer takes.
 *
 * Neither disk carries a light, so each contributes nothing but its own
 * attenuation (Ci stays zero): the resolved centre pixel is exactly
 * (1 - Os1) * (1 - Os2) * background once both composite, against
 * (1 - Os1) * background alone if the second disk's tmin skipped it --
 * the two predictions are far enough apart that either observed value
 * settles which happened.
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
constexpr RtFloat kFirstLayerZ = 5.0f;
constexpr RtFloat kGap = 0.01f; // under 1e-2 * kFirstLayerZ, the old bias at this depth
constexpr RtFloat kOpacity = 0.4f;

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

} // namespace

int main() {
  GMANOptions options;
  options.setFormat(kRes, kRes, 1.0);
  options.setPixelSamples(1.0, 1.0);
  options.setPixelFilter(RiBoxFilter, 1.0, 1.0);

  GMANMatrix4 const identity;
  gman::VSPerspective viewingSys(kRes, kRes, squareScreenWindow(), identity, 90.0, 0.5, 50.0);

  GMANRaytraceRenderer renderer;
  GMANAttributes attr;
  RtColor opacity = {kOpacity, kOpacity, kOpacity};
  attr.setOpacity(opacity);

  for (RtFloat z : {kFirstLayerZ, kFirstLayerZ + kGap}) {
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
  GMANColor const centre = frameBuffer.getPixel(kRes / 2, kRes / 2);

  RtFloat const oneLayer = (1.0f - kOpacity) * background.getRed();
  RtFloat const twoLayers = (1.0f - kOpacity) * (1.0f - kOpacity) * background.getRed();
  std::printf("check 6: centre=%.6f one-layer prediction=%.6f two-layer prediction=%.6f\n", centre.getRed(), oneLayer,
              twoLayers);

  check(std::fabs(centre.getRed() - twoLayers) <= 1e-4f,
        "check 6: a 0.01 gap, under the old bias, still composites both disks");
  check(std::fabs(centre.getRed() - oneLayer) > 1e-3f,
        "check 6: the result is not what a tmin that skipped the second disk would read");

  return checkSummary("R7's transparency composite: a gap under the old bias still composites both surfaces");
}
