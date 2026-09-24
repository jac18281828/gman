/* SPDX-License-Identifier: LGPL-2.1-or-later */

/* This is part of GMAN, a RenderMan-compatible renderer.
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

#include <cmath>

#include "gmanbbox.h"
#include "gmanmath.h"
#include "gmanmatrix4.h"
#include "gmanpoint.h"
#include "gmanvector.h"

namespace gman {

// The componentwise min, or max, of two points -- every axis taken
// independently, so neither point need be ordered against the other's.
inline GMANPoint pointMin(GMANPoint const& a, GMANPoint const& b) {
  return GMANPoint(GMANMin(a.getX(), b.getX()), GMANMin(a.getY(), b.getY()), GMANMin(a.getZ(), b.getZ()));
}

inline GMANPoint pointMax(GMANPoint const& a, GMANPoint const& b) {
  return GMANPoint(GMANMax(a.getX(), b.getX()), GMANMax(a.getY(), b.getY()), GMANMax(a.getZ(), b.getZ()));
}

// The largest absolute coordinate over box's own six coordinates, 0 for a box
// still at its default +/-RI_INFINITY (a primitive whose own bbox was
// never assigned) -- hitSurfacePoint's own M, gman::SurfacePoint's
// surfaceMagnitude field.
inline RtFloat primitiveMagnitude(GMANBBox const& box) {
  GMANPoint const boxMin = box.getMin();
  GMANPoint const boxMax = box.getMax();
  RtFloat const coords[6] = {boxMin.getX(), boxMin.getY(), boxMin.getZ(), boxMax.getX(), boxMax.getY(), boxMax.getZ()};
  RtFloat magnitude = 0.0;
  for (RtFloat const coord : coords) {
    if (std::fabs(coord) >= RI_INFINITY) {
      return 0.0;
    }
    magnitude = GMANMax(magnitude, (RtFloat)std::fabs(coord));
  }
  return magnitude;
}

// A box built from RtFloat corner transforms can, by a few ULP, fall just
// inside a hit a double-precision intersector computes (R5c's torus solves
// its quartic in double). Padding every assigned box outward by an epsilon
// scaled to its own largest coordinate magnitude -- with an absolute floor
// for a box near the origin -- means this rounding gap can never exclude a
// real hit. gmanraytracerenderer.cpp's kSelfShadowOffsetCeiling/
// kSelfShadowOffsetFloor are the precedent for this shape.
constexpr RtFloat kBBoxPadScale = (RtFloat)3.0e-5;
constexpr RtFloat kBBoxPadFloor = (RtFloat)1.0e-9;

// Pads a camera-space box (min, max already componentwise-ordered) outward
// by kBBoxPadScale/kBBoxPadFloor's own epsilon.
inline GMANBBox padBBox(GMANPoint const& min, GMANPoint const& max) {
  RtFloat const coords[6] = {min.getX(), min.getY(), min.getZ(), max.getX(), max.getY(), max.getZ()};
  RtFloat magnitude = 0.0;
  for (RtFloat const coord : coords) {
    magnitude = GMANMax(magnitude, (RtFloat)std::fabs(coord));
  }
  RtFloat const pad = GMANMax(kBBoxPadScale * magnitude, kBBoxPadFloor);
  return GMANBBox(GMANPoint(min.getX() - pad, min.getY() - pad, min.getZ() - pad),
                  GMANPoint(max.getX() + pad, max.getY() + pad, max.getZ() + pad));
}

// Refits an object-space box's eight corners through objectToCamera and
// pads the result (padBBox above): the standard refit, correct for any
// affine placement including a shear or a non-uniform scale, though not
// always the tightest possible box. objectToCamera must be affine -- a
// projective row is not handled here. Every ray primitive but the polygon
// (already camera space at construction) calls this from its own
// transform-taking constructor.
inline GMANBBox cameraSpaceBBox(GMANMatrix4 const& objectToCamera, GMANPoint const& objMin, GMANPoint const& objMax) {
  RtFloat const xs[2] = {objMin.getX(), objMax.getX()};
  RtFloat const ys[2] = {objMin.getY(), objMax.getY()};
  RtFloat const zs[2] = {objMin.getZ(), objMax.getZ()};

  GMANPoint camMin;
  GMANPoint camMax;
  bool first = true;
  for (RtFloat const x : xs) {
    for (RtFloat const y : ys) {
      for (RtFloat const z : zs) {
        GMANPoint const corner = gman::transformPoint(objectToCamera, GMANPoint(x, y, z));
        if (first) {
          camMin = corner;
          camMax = corner;
          first = false;
          continue;
        }
        camMin = pointMin(camMin, corner);
        camMax = pointMax(camMax, corner);
      }
    }
  }

  return padBBox(camMin, camMax);
}

// The shared shape of a surface of revolution's object-space bound: x and
// y span +/-|r| (the full sweep, safe whether or not thetamax clips it),
// z spans [z0, z1] however ordered. Every ray primitive but the polygon
// (already camera space, see cameraSpaceBBox above) calls this, each
// computing its own r from the fields its own intersect() reads -- the
// disk passes height for both z0 and z1.
inline GMANBBox revolutionBBox(GMANMatrix4 const& objectToCamera, RtFloat r, RtFloat z0, RtFloat z1) {
  RtFloat const absR = (RtFloat)std::fabs(r);
  GMANPoint const objMin(-absR, -absR, GMANMin(z0, z1));
  GMANPoint const objMax(absR, absR, GMANMax(z0, z1));
  return cameraSpaceBBox(objectToCamera, objMin, objMax);
}

// The object-space parameter shifting the ray's origin to its closest
// approach to the origin, when that origin starts outside the bounding
// sphere (radius boundingRadius, centred on the origin); 0 when already
// inside. Every coefficient a caller builds from the shifted origin then
// sits near the shape regardless of camera distance, so a double root
// solve holds its accuracy there. Returns false, shift left at the
// closest approach, when that approach still clears the bounding sphere:
// such a ray misses the shape outright, before any coefficient is built.
inline bool boundingSphereShift(double ox, double oy, double oz, double dx, double dy, double dz, double dirSq,
                                double boundingRadius, double& shift) {
  double const boundingRadiusSq = boundingRadius * boundingRadius;
  shift = 0.0;
  if (ox * ox + oy * oy + oz * oz <= boundingRadiusSq)
    return true;
  shift = -(ox * dx + oy * dy + oz * dz) / dirSq;
  double const cx = ox + shift * dx, cy = oy + shift * dy, cz = oz + shift * dz;
  return cx * cx + cy * cy + cz * cz <= boundingRadiusSq;
}

// A ray moved to its closest approach to the bounding sphere: (ox, oy, oz)
// is the object-space origin already shifted, (dx, dy, dz) keeps the
// length cameraToObject gives it, dirSq is their squared length, and
// shift is how far along the direction the origin moved. Every quadratic
// intersector builds its a, b, c from these six fields.
struct ShiftedRay {
  double ox, oy, oz;
  double dx, dy, dz;
  double dirSq;
  double shift;
};

// Moves objOrigin/objDirection into a ShiftedRay via boundingSphereShift
// above. Returns false for a zero-length direction -- checked before the
// shift divides by dirSq, the torus's own guard -- and for a ray that
// clears the bounding sphere outright.
inline bool shiftIntoBoundingSphere(GMANPoint const& objOrigin, GMANVector const& objDirection, double boundingRadius,
                                    ShiftedRay& shifted) {
  double const dx = objDirection.getX(), dy = objDirection.getY(), dz = objDirection.getZ();
  double const dirSq = dx * dx + dy * dy + dz * dz;
  if (dirSq == 0.0)
    return false;

  double const ox = objOrigin.getX(), oy = objOrigin.getY(), oz = objOrigin.getZ();

  double shift = 0.0;
  if (!boundingSphereShift(ox, oy, oz, dx, dy, dz, dirSq, boundingRadius, shift))
    return false;

  shifted.ox = ox + shift * dx;
  shifted.oy = oy + shift * dy;
  shifted.oz = oz + shift * dz;
  shifted.dx = dx;
  shifted.dy = dy;
  shifted.dz = dz;
  shifted.dirSq = dirSq;
  shifted.shift = shift;
  return true;
}

// A quadratic root's own camera-space distance and object-space point,
// object and camera coordinates both: hit.t, hit.point and the double
// (px, py, pz) a caller's own surface checks (z-band, theta, phi) read.
struct ShiftedRootPoint {
  RtFloat t;
  double px, py, pz;
  GMANPoint point;
};

// Evaluates a ShiftedRay at its shifted-frame root tLocal and casts the
// result to camera space through objectToCamera. hit.t is tLocal + shift,
// the camera-space distance along the ray's own (unshifted) origin.
inline ShiftedRootPoint shiftedRootPoint(ShiftedRay const& shifted, double tLocal, GMANMatrix4 const& objectToCamera) {
  ShiftedRootPoint result;
  result.t = (RtFloat)(tLocal + shifted.shift);
  result.px = shifted.ox + tLocal * shifted.dx;
  result.py = shifted.oy + tLocal * shifted.dy;
  result.pz = shifted.oz + tLocal * shifted.dz;
  GMANPoint const objPoint((RtFloat)result.px, (RtFloat)result.py, (RtFloat)result.pz);
  result.point = gman::transformPoint(objectToCamera, objPoint);
  return result;
}

} // namespace gman
