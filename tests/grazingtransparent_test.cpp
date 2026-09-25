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
 * A transparent sphere hit near its own silhouette, where the entry
 * point's Ng . D is small -- offsetting along the ray
 * direction there gives a perpendicular clearance of offset * |Ng . D|,
 * shrunk by that same small factor, where offsetting along Ng keeps the
 * full offset regardless of angle. A flat surface cannot exercise this:
 * a fixed-direction ray crosses a plane at most once, so no offset choice
 * ever recomposites it, which is why this check needs curvature rather
 * than a tilted polygon. Check 2's sphere is opaque, so its composite
 * loop breaks on transmissionNegligible before a second ray exists, and
 * check 6's pair is head-on (Ng . D close to 1), where the offset is
 * undiminished -- neither can stand in for this.
 *
 * The sphere carries no light, so a correct two-surface composite (the
 * ray's entry and exit through the shell) reads exactly
 * (1 - Os)^2 * background; an offset taken along the ray direction
 * instead of Ng can lose its clearance at the entry hit and let the
 * composite loop re-hit that same point, reading a third layer darker.
 * column 400 of the scan below was chosen empirically: at the base
 * commit's own along-ray-direction defect it reads a stable third layer,
 * while neighbouring columns flicker between two and three layers as the
 * ray's angle to the sphere drifts across the tangent -- the flicker
 * itself is the floating-point instability this check exists to pin
 * down, and column 400 is the one point in that scan where it reproduces
 * every time.
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
#include "gmanraysphere.h"
#include "gmanraytracerenderer.h"
#include "gmantransform.h"
#include "gmanvector.h"
#include "gmanvsperspective.h"
#include "ri.h"

namespace {

constexpr RtInt kResX = 801;
constexpr RtInt kResY = 1;
constexpr int kCheckColumn = 400;
constexpr RtFloat kOpacity = 0.3f;
constexpr RtFloat kSphereRadius = 5.0f;
constexpr RtFloat kSphereZ = 50.0f;

GMANTransform makeTransform(GMANMatrix4 matrix) {
  GMANOneMatrix storage(matrix);
  return GMANTransform(storage);
}

// A narrow, off-axis screen window: wide enough to carry the sphere's
// silhouette edge (near world x=5 at z=50, screen x =~ 0.1) across
// kResX columns, tall enough for one row through the sphere's own
// equator (y=0).
GMANOptions::ScreenWindowStruct silhouetteScreenWindow() {
  GMANOptions::ScreenWindowStruct sw;
  sw.left = 0.085;
  sw.right = 0.115;
  sw.bottom = -0.0003;
  sw.top = 0.0003;
  return sw;
}

} // namespace

int main() {
  GMANOptions options;
  options.setFormat(kResX, kResY, (double)kResX / (double)kResY);
  options.setPixelSamples(1.0, 1.0);
  options.setPixelFilter(RiBoxFilter, 1.0, 1.0);

  GMANMatrix4 const identity;
  gman::VSPerspective viewingSys(kResX, kResY, silhouetteScreenWindow(), identity, 90.0, 0.5, 100.0);

  GMANRaytraceRenderer renderer;
  GMANAttributes attr;
  RtColor opacity = {kOpacity, kOpacity, kOpacity};
  attr.setOpacity(opacity);

  GMANMatrix4 place;
  place.trans(0.0, 0.0, kSphereZ);
  GMANTransform transform = makeTransform(place);
  GMANRaySphere* sphere =
      new GMANRaySphere(kSphereRadius, -kSphereRadius, kSphereRadius, 360.0f, GMANParameterList(), transform);
  sphere->setAppearance(gman::appearanceOf(attr));
  renderer.getWorldManager()->add(sphere);

  GMANColor const background(1.0f, 1.0f, 1.0f);
  GMANFrameBuffer frameBuffer(kResX, kResY, background);
  renderer.render(&frameBuffer, &viewingSys, options, attr);
  GMANColor const pixel = frameBuffer.getPixel(kCheckColumn, 0);

  RtFloat const twoLayers = (1.0f - kOpacity) * (1.0f - kOpacity) * background.getRed();
  RtFloat const threeLayers = (1.0f - kOpacity) * (1.0f - kOpacity) * (1.0f - kOpacity) * background.getRed();
  std::printf("check 8: column %d red=%.6f two-layer prediction=%.6f three-layer prediction=%.6f\n", kCheckColumn,
              pixel.getRed(), twoLayers, threeLayers);

  check(std::fabs(pixel.getRed() - twoLayers) <= 1e-4f,
        "check 8: a near-silhouette entry composites the sphere's own two surfaces, not a third");

  return checkSummary("R7's transparency composite: a near-silhouette hit composites exactly its own two surfaces");
}
