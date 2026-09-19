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
 * R4 proof: GMANRaytraceRenderer::getDepth returns camera-space z of the
 * nearest hit, RI_INFINITY where none hit. A 41x41 (odd) Format at
 * PixelSamples 1 1 puts the single sample of the centre pixel exactly on
 * the view axis, so a unit sphere at camera-space z=5 pins its depth to
 * 4.0 (the near side of the sphere); a corner pixel's ray, well outside
 * the sphere's small angular radius, pins RI_INFINITY.
 *
 * Built from the plugin's own sources, as raysphere_test.cpp is: the
 * viewing system is built directly, as cameraray_test.cpp does, rather
 * than through a RIB parse.
 */

#include <cmath>

#include "check.h"
#include "gmanattributes.h"
#include "gmanframebuffer.h"
#include "gmanobjectmanager.h"
#include "gmanoptions.h"
#include "gmanparameterlist.h"
#include "gmanraytracerenderer.h"
#include "gmantransform.h"
#include "gmanvsperspective.h"
#include "ri.h"

namespace {

constexpr RtInt kRes = 41;
constexpr RtFloat kTolerance = 1e-3f;

GMANTransform makeTransform(GMANMatrix4 matrix) {
  GMANOneMatrix storage(matrix);
  return GMANTransform(storage);
}

} // namespace

int main() {
  GMANOptions options;
  options.setFormat(kRes, kRes, 1.0);
  options.setPixelSamples(1.0, 1.0);

  GMANOptions::ScreenWindowStruct sw;
  sw.left = -1.0;
  sw.right = 1.0;
  sw.bottom = -1.0;
  sw.top = 1.0;

  GMANMatrix4 const identity;
  gman::VSPerspective viewingSys(kRes, kRes, sw, identity, 90.0, 0.5, 50.0);

  GMANRaytraceRenderer renderer;

  GMANMatrix4 place;
  place.trans(0.0, 0.0, 5.0);
  GMANTransform transform = makeTransform(place);

  GMANAttributes attr;
  GMANPrimitive* sphere =
      renderer.getObjectManager()->getRSSphere(1.0, -1.0, 1.0, 360.0, GMANParameterList(), &options, &attr, &transform);
  renderer.getWorldManager()->add(sphere);

  GMANFrameBuffer frameBuffer(kRes, kRes, options.getBackground());
  renderer.render(&frameBuffer, &viewingSys, options, attr);

  RtFloat const centreDepth = renderer.getDepth(kRes / 2, kRes / 2);
  check(std::fabs(centreDepth - 4.0) <= kTolerance, "depth: the centre sample pins getDepth to 4.0");

  RtFloat const cornerDepth = renderer.getDepth(0, 0);
  check(cornerDepth == RI_INFINITY, "depth: a corner ray missing the sphere pins getDepth to RI_INFINITY");

  return checkSummary("GMANRaytraceRenderer::getDepth pins camera-space z at a hit and RI_INFINITY at a miss");
}
