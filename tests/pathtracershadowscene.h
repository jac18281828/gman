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
 * The shadow-walk tests' own scene, shared by pathtracershadow_test.cpp and
 * pathtracershadowcap_test.cpp: the floor scene under one distant light,
 * a rectangular blocker above it, and the shadowed-pixel test that finds
 * the blocker's own shadow on the floor.
 */

#include <algorithm>
#include <cstddef>
#include <memory>
#include <vector>

#include "gmanattributes.h"
#include "gmanframebuffer.h"
#include "gmanlightsourcemgr.h"
#include "gmanoptions.h"
#include "gmanparameterlist.h"
#include "gmanpathtracerenderer.h"
#include "gmanpoint.h"
#include "gmanraypolygon.h"
#include "gmanvector.h"
#include "gmanvsperspective.h"
#include "pathtracerscene.h"
#include "ri.h"

inline constexpr RtInt kRes = 81;
inline constexpr RtFloat kReflectance = 0.5f; // Kd = 1, Cs = 0.5
inline constexpr RtInt kSamples = 64;

inline constexpr RtFloat kLightIntensity = 0.8f;
inline constexpr double kExpectedLit = 0.4; // rho * I, cos(theta) = 1

inline constexpr RtFloat kBlockerY = 12.0f;
inline constexpr RtFloat kBlockerXMin = -2.0f;
inline constexpr RtFloat kBlockerXMax = 2.0f;
inline constexpr RtFloat kBlockerZMin = 5.0f;
inline constexpr RtFloat kBlockerZMax = 9.0f;

inline constexpr RtFloat kShadowedXMin = -1.8f;
inline constexpr RtFloat kShadowedXMax = 1.8f;
inline constexpr RtFloat kShadowedZMin = 5.2f;
inline constexpr RtFloat kShadowedZMax = 8.8f;
inline constexpr std::size_t kMinShadowedPixels = 100;

inline bool withinRect(double x, double z, double xmin, double xmax, double zmin, double zmax, double margin) {
  return x >= xmin + margin && x <= xmax - margin && z >= zmin + margin && z <= zmax - margin;
}

// A shadowed pixel: measured, and its four corner hits lie within the
// shadowed rectangle, clear of its own edge.
inline bool pixelShadowed(gman::VSPerspective& viewingSys, int px, int py) {
  if (!pixelMeasured(viewingSys, px, py)) {
    return false;
  }
  for (int dy = 0; dy <= 1; ++dy) {
    for (int dx = 0; dx <= 1; ++dx) {
      GMANRay const corner = viewingSys.cameraRay((RtFloat)(px + dx), (RtFloat)(py + dy));
      double x, z;
      if (!intersectFloorPlane(corner, x, z) ||
          !withinRect(x, z, kShadowedXMin, kShadowedXMax, kShadowedZMin, kShadowedZMax, 0.05)) {
        return false;
      }
    }
  }
  return true;
}

// The blocker rectangle at height y, reversed winding for the pane's far
// face.
inline std::vector<GMANPoint> rectAt(RtFloat y, bool reversedWinding = false) {
  std::vector<GMANPoint> verts = {GMANPoint(kBlockerXMin, y, kBlockerZMin), GMANPoint(kBlockerXMin, y, kBlockerZMax),
                                  GMANPoint(kBlockerXMax, y, kBlockerZMax), GMANPoint(kBlockerXMax, y, kBlockerZMin)};
  if (reversedWinding) {
    std::reverse(verts.begin(), verts.end());
  }
  return verts;
}

// Renders the floor under one distant light, with blockerAppearances added
// above it at blockerVerts, once; the caller selects whichever pixel sets
// it needs from the result.
inline void renderScene(std::vector<gman::Appearance> const& blockerAppearances,
                        std::vector<std::vector<GMANPoint>> const& blockerVerts,
                        std::unique_ptr<GMANFrameBuffer>& frameBufferOut,
                        std::unique_ptr<gman::VSPerspective>& viewingSysOut, std::size_t& droppedOut) {
  GMANOptions options;
  options.setFormat(kRes, kRes, 1.0f);
  options.setPixelSamples(1.0f, 1.0f);
  options.setPixelFilter(RiBoxFilter, 1.0f, 1.0f);
  options.setPathtracerSamples(kSamples);

  GMANMatrix4 const identity;
  viewingSysOut.reset(new gman::VSPerspective(kRes, kRes, squareScreenWindow(), identity, 90.0f, 0.5f, 50.0f));

  GMANLight const light(GMAN_LIGHT_DISTANT, GMANColor(kLightIntensity, kLightIntensity, kLightIntensity), GMANPoint(),
                        GMANVector(0.0f, -1.0f, 0.0f));

  GMANPathtraceRenderer renderer;
  GMANRayPolygon* floor = buildFloor();
  gman::Appearance floorAppearance;
  floorAppearance.shader = loadShader("matte", matteParams(1.0f));
  floorAppearance.Cs = GMANColor(kReflectance, kReflectance, kReflectance);
  floorAppearance.Os = GMANColor(1.0f, 1.0f, 1.0f);
  floorAppearance.lights = {&light};
  floor->setAppearance(floorAppearance);
  renderer.getWorldManager()->add(floor);

  for (std::size_t i = 0; i < blockerVerts.size(); ++i) {
    const auto blocker = new GMANRayPolygon(blockerVerts[i], GMANParameterList());
    gman::Appearance appearance = blockerAppearances[i];
    appearance.lights = {&light};
    blocker->setAppearance(appearance);
    renderer.getWorldManager()->add(blocker);
  }

  frameBufferOut.reset(new GMANFrameBuffer(kRes, kRes, options.getBackground()));
  GMANAttributes const attr;
  renderer.render(frameBufferOut.get(), viewingSysOut.get(), options, attr);
  droppedOut = renderer.droppedPathCount();
}
