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

#pragma once

/*
 * Shared across the path tracer's own area-light test files, built on
 * tests/pathtracerscene.h's own floor scene.
 */

#include <cmath>

#include "gmancolor.h"
#include "gmanmatrix4.h"
#include "gmanpathtracerenderer.h"
#include "gmanraysphere.h"
#include "gmantransform.h"
#include "gmanvsperspective.h"
#include "maketransform.h"
#include "pathtracerscene.h"
#include "ri.h"

// An emitting sphere or disk's own radiance in a pathtracer area-light test.
inline constexpr RtFloat kAreaLe = 10.0f;

// A rigidly placed emitting sphere, added to renderer's world, opaque (Os
// 1, the RIB default, so a shadow ray that walked all the way to the
// sampled point would graze the emitter's own surface there). areaLight is
// the caller's own, outliving renderer.render()'s own call.
inline GMANRaySphere* addEmittingSphere(GMANPathtraceRenderer& renderer, GMANLight const& areaLight, RtFloat radius,
                                        RtFloat centreX, RtFloat centreY, RtFloat centreZ) {
  GMANMatrix4 place;
  place.trans(centreX, centreY, centreZ);
  GMANTransform const transform = makeTransform(place);
  GMANRaySphere* sphere = new GMANRaySphere(radius, -radius, radius, 360.0f, GMANParameterList(), transform);
  gman::Appearance appearance;
  appearance.areaLight = &areaLight;
  appearance.Os = GMANColor(1.0f, 1.0f, 1.0f);
  sphere->setAppearance(appearance);
  renderer.getWorldManager()->add(sphere);
  return sphere;
}

// The mean, over a 16 x 16 midpoint grid of pixel (px, py)'s cell, of the
// equivalent-point-source formula for a Lambertian sphere light: rho * le *
// radius^2 * h / d^3, h the sphere centre's height above the floor plane, d
// the distance from the grid point to the sphere's centre. Every length
// here is the sphere's own object-space one, never a placed, scaled one:
// the formula is dimensionless in length (radius^2 * h / d^3), so scaling
// every length in both the rendered scene and this call by the same factor
// leaves its value unchanged -- the actual proof a scaled scene's render
// still matches.
inline GMANColor analyticSphereLightExpected(gman::VSPerspective& viewingSys, int px, int py, RtFloat le,
                                             RtFloat radius, RtFloat centreX, RtFloat centreY, RtFloat centreZ) {
  constexpr int kMidGrid = 16;
  constexpr RtFloat kReflectance = 0.5f;
  double sum = 0.0;
  for (int sy = 0; sy < kMidGrid; ++sy) {
    for (int sx = 0; sx < kMidGrid; ++sx) {
      RtFloat const rx = (RtFloat)px + ((RtFloat)sx + 0.5f) / (RtFloat)kMidGrid;
      RtFloat const ry = (RtFloat)py + ((RtFloat)sy + 0.5f) / (RtFloat)kMidGrid;
      GMANRay const ray = viewingSys.cameraRay(rx, ry);
      double x, z;
      if (!intersectFloorPlane(ray, x, z)) {
        continue;
      }
      double const dx = x - (double)centreX;
      double const dy = (double)kFloorY - (double)centreY;
      double const dz = z - (double)centreZ;
      double const dist2 = dx * dx + dy * dy + dz * dz;
      double const dist = std::sqrt(dist2);
      sum += (double)kReflectance * (double)le * (double)radius * (double)radius * std::fabs(dy) / (dist2 * dist);
    }
  }
  RtFloat const value = (RtFloat)(sum / (double)(kMidGrid * kMidGrid));
  return GMANColor(value, value, value);
}
