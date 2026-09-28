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
 * The path tracer's own test floor scene, shared by the integrator tests
 * that render it: a square Polygon at y = kFloorY spanning x in
 * [kFloorXMin, kFloorXMax] and z in [kFloorZMin, kFloorZMax], with a
 * double-precision plane intersection and a measured-pixel test to match.
 * Also the shipped-shader loader every integrator test uses in place of a
 * hand-written closure.
 */

#include <memory>
#include <string>
#include <vector>

#include "gmanattributes.h"
#include "gmancolor.h"
#include "gmandictionary.h"
#include "gmanoptions.h"
#include "gmanparameterlist.h"
#include "gmanpoint.h"
#include "gmanray.h"
#include "gmanraypolygon.h"
#include "gmansurfaceshader.h"
#include "gmanvsperspective.h"
#include "ri.h"

inline constexpr RtFloat kFloorXMin = -8.0f;
inline constexpr RtFloat kFloorXMax = 8.0f;
inline constexpr RtFloat kFloorY = -4.0f;
inline constexpr RtFloat kFloorZMin = 1.0f;
inline constexpr RtFloat kFloorZMax = 17.0f;

// The ray tracer's own polygon intersection carries its own float
// tolerance, so a corner within kEdgeMargin of the true boundary can miss
// where this test's own double-precision plane intersection says it
// should hit.
inline constexpr double kEdgeMargin = 1.0;

inline GMANOptions::ScreenWindowStruct squareScreenWindow() {
  GMANOptions::ScreenWindowStruct sw;
  sw.left = -1.0f;
  sw.right = 1.0f;
  sw.bottom = -1.0f;
  sw.top = 1.0f;
  return sw;
}

inline GMANRayPolygon* buildFloor() {
  std::vector<GMANPoint> const verts = {
      GMANPoint(kFloorXMin, kFloorY, kFloorZMin), GMANPoint(kFloorXMin, kFloorY, kFloorZMax),
      GMANPoint(kFloorXMax, kFloorY, kFloorZMax), GMANPoint(kFloorXMax, kFloorY, kFloorZMin)};
  return new GMANRayPolygon(verts, GMANParameterList());
}

// The ray-plane intersection with y = kFloorY, in double; false when the
// ray is parallel to the floor or the plane lies behind its origin.
inline bool intersectFloorPlane(GMANRay const& ray, double& outX, double& outZ) {
  double const oy = (double)ray.getOrigin().getY();
  double const dy = (double)ray.getDirection().getY();
  if (dy == 0.0) {
    return false;
  }
  double const t = ((double)kFloorY - oy) / dy;
  if (!(t > 0.0)) {
    return false;
  }
  outX = (double)ray.getOrigin().getX() + t * (double)ray.getDirection().getX();
  outZ = (double)ray.getOrigin().getZ() + t * (double)ray.getDirection().getZ();
  return true;
}

inline bool withinFloor(double x, double z, double margin) {
  return x >= (double)kFloorXMin + margin && x <= (double)kFloorXMax - margin && z >= (double)kFloorZMin + margin &&
         z <= (double)kFloorZMax - margin;
}

// A pixel is measured when its cell's four corner rays all hit the floor,
// clear of its edge by kEdgeMargin.
inline bool pixelMeasured(gman::VSPerspective& viewingSys, int px, int py) {
  for (int dy = 0; dy <= 1; ++dy) {
    for (int dx = 0; dx <= 1; ++dx) {
      GMANRay const corner = viewingSys.cameraRay((RtFloat)(px + dx), (RtFloat)(py + dy));
      double x, z;
      if (!intersectFloorPlane(corner, x, z) || !withinFloor(x, z, kEdgeMargin)) {
        return false;
      }
    }
  }
  return true;
}

inline double channel(GMANColor const& c, int i) {
  if (i == 0) {
    return c.getRed();
  }
  return i == 1 ? c.getGreen() : c.getBlue();
}

inline double channel(GMANAlpha const& a, int i) {
  if (i == 0) {
    return a.getRed();
  }
  return i == 1 ? a.getGreen() : a.getBlue();
}

// Loads a shipped shader plugin by its RIB token, as Surface "name" would;
// the returned shader outlives the GMANAttributes that loaded it, since
// getSurface's shared_ptr aliases the plugin module itself.
inline std::shared_ptr<GMANSurfaceShader const> loadShader(std::string const& name, GMANParameterList const& params) {
  GMANAttributes attributes;
  attributes.setSurface(name, params);
  return attributes.getSurface(0.0);
}

// Surface "matte" "float Kd" [kd]'s own parameter list.
inline GMANParameterList matteParams(RtFloat kd) {
  static GMANDictionary dictionary;
  RtToken tokens[1] = {RI_KD};
  RtPointer parms[1] = {&kd};
  return GMANParameterList(dictionary, 1, tokens, parms);
}
