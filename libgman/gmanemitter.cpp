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

#include <algorithm>
#include <cmath>

#include "gmanemitter.h"
#include "gmanerror.h"
#include "gmanlog.h"
#include "gmanmatrix4.h"
#include "gmanraydisk.h"
#include "gmanraysphere.h"
#include "gmanshading.h"

namespace gman {

namespace {

constexpr RtFloat kPi = (RtFloat)3.14159265358979323846;

RtFloat meanChannel(GMANColor const& c) { return (c.getRed() + c.getGreen() + c.getBlue()) / (RtFloat)3.0; }

// A degenerate sphere zone never emits: a non-positive radius, an empty or
// reversed z range, a z range reaching outside the sphere's own surface (a
// zone samplePoint's own r = sqrt(max(0, radius^2 - z^2)) would floor at 0
// rather than describe a real point), or a non-positive thetamax.
bool sphereEligible(GMANRaySphere const& sphere) {
  RtFloat const radius = sphere.getRadius();
  if (!(radius > 0.0f)) {
    return false;
  }
  RtFloat const zmin = sphere.getZMin();
  RtFloat const zmax = sphere.getZMax();
  if (!(zmax > zmin)) {
    return false;
  }
  if (std::fabs(zmax) > radius || std::fabs(zmin) > radius) {
    return false;
  }
  return sphere.getThetaMax() > 0.0f;
}

// A degenerate disk sector never emits: a non-positive radius or thetamax.
bool diskEligible(GMANRayDisk const& disk) { return disk.getRadius() > 0.0f && disk.getThetaMax() > 0.0f; }

// True, and area filled, for a supported (sphere or disk), geometrically
// eligible shape. False for any other primitive type or a degenerate one;
// area is left untouched.
bool emittingShapeArea(GMANRayInterface const& shape, RtFloat& area) {
  if (GMANRaySphere const* sphere = dynamic_cast<GMANRaySphere const*>(&shape)) {
    if (!sphereEligible(*sphere)) {
      return false;
    }
    area = sphere->area();
    return true;
  }
  if (GMANRayDisk const* disk = dynamic_cast<GMANRayDisk const*>(&shape)) {
    if (!diskEligible(*disk)) {
      return false;
    }
    area = disk->area();
    return true;
  }
  return false;
}

// shape's own shutter-open placement. Precondition: shape is a
// GMANRaySphere or GMANRayDisk, the only two supported emitting types.
GMANMatrix4 const& emittingShapeObjectToCamera(GMANRayInterface const& shape) {
  if (GMANRaySphere const* sphere = dynamic_cast<GMANRaySphere const*>(&shape)) {
    return sphere->getObjectToCamera();
  }
  return dynamic_cast<GMANRayDisk const&>(shape).getObjectToCamera();
}

// shape's own uniform-by-area draw, in object space. Precondition: shape is
// a GMANRaySphere or GMANRayDisk.
GMANPoint emittingShapeSamplePoint(GMANRayInterface const& shape, double u1, double u2, GMANVector& normal) {
  if (GMANRaySphere const* sphere = dynamic_cast<GMANRaySphere const*>(&shape)) {
    return sphere->samplePoint(u1, u2, normal);
  }
  return dynamic_cast<GMANRayDisk const&>(shape).samplePoint(u1, u2, normal);
}

// True when objectToCamera's own inverse exists -- a singular placement
// (e.g. Scale 1 1 0) counts as skipped, the same rule
// renderers/radiosity/gmanradiositymesh.cpp already applies to a quadric's
// own placement. cameraToObject is filled either way; a caller that
// ignores a false return must not read it.
bool invertiblePlacement(GMANMatrix4 const& objectToCamera, GMANMatrix4& cameraToObject) {
  cameraToObject = objectToCamera;
  try {
    cameraToObject.invert();
  } catch (GMANError const&) {
    return false;
  }
  return true;
}

// Places an object-space point and normal by objectToCamera and its own
// inverse cameraToObject, the normal through cameraToObject's transpose,
// then normalized -- renderers/radiosity/gmanradiositymesh.cpp's own
// pattern, reused exactly. RiOrientation/RiSides are not consulted,
// matching that file's own choice.
void placeEmitterPoint(GMANMatrix4 const& objectToCamera, GMANMatrix4 const& cameraToObject,
                       GMANPoint const& objectPoint, GMANVector const& objectNormal, GMANPoint& cameraPoint,
                       GMANVector& cameraNormal) {
  cameraPoint = gman::transformPoint(objectToCamera, objectPoint);
  cameraNormal = gman::transformNormal(cameraToObject, objectNormal);
  cameraNormal.normalize();
}

// The area branch of sample(): draws a point on emitter's shape, places
// it, and reports the solid-angle pdf the area-to-solid-angle Jacobian
// gives, black and 0 on the emitter's own non-emitting side.
EmitterSample sampleArea(Emitter const& emitter, GMANPoint const& p, RtFloat u1, RtFloat u2) {
  EmitterSample result;
  result.isDelta = false;

  GMANVector objectNormal;
  GMANPoint const objectPoint = emittingShapeSamplePoint(*emitter.shape, u1, u2, objectNormal);
  GMANMatrix4 const objectToCamera = emittingShapeObjectToCamera(*emitter.shape);
  GMANMatrix4 cameraToObject;
  invertiblePlacement(objectToCamera, cameraToObject); // precondition: emitter came from emitters()

  GMANPoint cameraPoint;
  GMANVector cameraNormal;
  placeEmitterPoint(objectToCamera, cameraToObject, objectPoint, objectNormal, cameraPoint, cameraNormal);

  GMANVector toEmitter(p, cameraPoint);
  RtFloat const distance = toEmitter.magnitude();
  toEmitter.normalize();
  result.wi = toEmitter;
  result.distance = distance;

  RtFloat area = 0.0f;
  emittingShapeArea(*emitter.shape, area);
  RtFloat const cosTheta = cameraNormal.dot(-toEmitter);
  GMANColor const black((RtFloat)0.0, (RtFloat)0.0, (RtFloat)0.0);

  // pdf uses the Jacobian's own abs(cosTheta), 0 only where that Jacobian
  // is singular; Cl goes black separately, on the emitter's far side,
  // per its one-sided emission.
  if (cosTheta == 0.0f || !(distance > 0.0f)) {
    result.Cl = black;
    result.pdf = 0.0f;
    return result;
  }
  result.pdf = (distance * distance) / (area * std::fabs(cosTheta));
  result.Cl = (cosTheta > 0.0f) ? emitter.light->getCl() : black;
  return result;
}

// The area branch of samplePoint(): draws a point on emitter's shape and
// places it, reporting Le undoubled (no pi scale) and pdf per unit area.
EmitterPoint samplePointArea(Emitter const& emitter, RtFloat u1, RtFloat u2) {
  EmitterPoint result;
  result.isDelta = false;

  GMANVector objectNormal;
  GMANPoint const objectPoint = emittingShapeSamplePoint(*emitter.shape, u1, u2, objectNormal);
  GMANMatrix4 const objectToCamera = emittingShapeObjectToCamera(*emitter.shape);
  GMANMatrix4 cameraToObject;
  invertiblePlacement(objectToCamera, cameraToObject); // precondition: emitter came from emitters()

  GMANPoint cameraPoint;
  GMANVector cameraNormal;
  placeEmitterPoint(objectToCamera, cameraToObject, objectPoint, objectNormal, cameraPoint, cameraNormal);

  RtFloat area = 0.0f;
  emittingShapeArea(*emitter.shape, area);

  result.point = cameraPoint;
  result.normal = GMANNormal(cameraNormal.getX(), cameraNormal.getY(), cameraNormal.getZ());
  result.Le = emitter.light->getCl();
  result.pdf = 1.0f / area;
  return result;
}

} // namespace

std::vector<Emitter> emitters(GMANWorldManager& world) {
  std::vector<Emitter> result;
  std::vector<GMANLight const*> deltaSeen;
  std::size_t ineligibleCount = 0;

  for (GMANPrimitive* primitive = world.getFirst(); primitive != nullptr; primitive = world.getNext()) {
    GMANRayInterface const* rayPrimitive = dynamic_cast<GMANRayInterface const*>(primitive);
    if (rayPrimitive == nullptr) {
      continue;
    }
    Appearance const& appearance = rayPrimitive->getAppearance();

    for (GMANLight const* light : appearance.lights) {
      if (light->getType() == GMAN_LIGHT_AMBIENT) {
        continue;
      }
      if (std::find(deltaSeen.begin(), deltaSeen.end(), light) == deltaSeen.end()) {
        deltaSeen.push_back(light);
        result.push_back({light, nullptr, (RtFloat)0.0});
      }
    }

    if (appearance.areaLight == nullptr) {
      continue;
    }
    RtFloat area = 0.0f;
    GMANMatrix4 cameraToObject;
    if (!emittingShapeArea(*rayPrimitive, area) ||
        !invertiblePlacement(emittingShapeObjectToCamera(*rayPrimitive), cameraToObject)) {
      ++ineligibleCount;
      continue;
    }
    RtFloat const power = meanChannel(appearance.areaLight->getCl()) * area * kPi;
    result.push_back({appearance.areaLight, rayPrimitive, power});
  }

  if (ineligibleCount != 0) {
    warning("gman::emitters: {} area-light primitive(s) are ineligible or unsupported; skipped.", ineligibleCount);
  }
  return result;
}

EmitterSample sample(Emitter const& emitter, GMANPoint const& p, RtFloat u1, RtFloat u2) {
  if (emitter.shape != nullptr) {
    return sampleArea(emitter, p, u1, u2);
  }

  EmitterSample result;
  GMANVector l;
  GMANColor cl;
  emitter.light->sample(p, l, cl);
  result.distance = (emitter.light->getType() == GMAN_LIGHT_DISTANT) ? RI_INFINITY : l.magnitude();
  l.normalize();
  result.wi = l;
  result.Cl = GMANColor(cl.getRed() * kPi, cl.getGreen() * kPi, cl.getBlue() * kPi);
  result.pdf = 1.0f;
  result.isDelta = true;
  return result;
}

EmitterPoint samplePoint(Emitter const& emitter, RtFloat u1, RtFloat u2) {
  if (emitter.shape != nullptr) {
    return samplePointArea(emitter, u1, u2);
  }

  EmitterPoint result;
  result.isDelta = true;
  result.Le = emitter.light->getCl();
  result.pdf = 1.0f;
  switch (emitter.light->getType()) {
  case GMAN_LIGHT_POINT:
  case GMAN_LIGHT_SPOT:
    result.point = emitter.light->getPosition();
    break;
  default:
    // No position exists for a distant or ambient light; point and
    // normal are left default-constructed, as nothing in this unit
    // reads either for a delta emitter.
    break;
  }
  return result;
}

} // namespace gman
