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
#include "gmanmath.h"
#include "gmanmatrix4.h"
#include "gmanraybbox.h"
#include "gmanraydisk.h"
#include "gmanrayoccluder.h"
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

// shape downcast once to whichever supported type it is: sphere non-null
// xor disk non-null, both null for an unsupported primitive type.
struct EmittingShapeKind {
  GMANRaySphere const* sphere;
  GMANRayDisk const* disk;
};

EmittingShapeKind classifyEmittingShape(GMANRayInterface const& shape) {
  if (GMANRaySphere const* sphere = dynamic_cast<GMANRaySphere const*>(&shape)) {
    return {sphere, nullptr};
  }
  return {nullptr, dynamic_cast<GMANRayDisk const*>(&shape)};
}

// True, and area filled, for a supported (sphere or disk), geometrically
// eligible shape. False for any other primitive type or a degenerate one;
// area is left untouched.
bool emittingShapeArea(GMANRayInterface const& shape, RtFloat& area) {
  EmittingShapeKind const kind = classifyEmittingShape(shape);
  if (kind.sphere != nullptr) {
    if (!sphereEligible(*kind.sphere)) {
      return false;
    }
    area = kind.sphere->area();
    return true;
  }
  if (kind.disk != nullptr) {
    if (!diskEligible(*kind.disk)) {
      return false;
    }
    area = kind.disk->area();
    return true;
  }
  return false;
}

// shape's own shutter-open placement. Precondition: shape is a
// GMANRaySphere or GMANRayDisk, the only two supported emitting types.
GMANMatrix4 const& emittingShapeObjectToCamera(GMANRayInterface const& shape) {
  EmittingShapeKind const kind = classifyEmittingShape(shape);
  return (kind.sphere != nullptr) ? kind.sphere->getObjectToCamera() : kind.disk->getObjectToCamera();
}

// shape's own uniform-by-area draw, in object space. Precondition: shape is
// a GMANRaySphere or GMANRayDisk.
GMANPoint emittingShapeSamplePoint(GMANRayInterface const& shape, double u1, double u2, GMANVector& normal) {
  EmittingShapeKind const kind = classifyEmittingShape(shape);
  return (kind.sphere != nullptr) ? kind.sphere->samplePoint(u1, u2, normal) : kind.disk->samplePoint(u1, u2, normal);
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

// shape's own placed centre (camera space) and bounding radius: the
// sphere's own centre and radius, or the disk's own point at its height
// and radius, each placed by shape's own object-to-camera transform.
// Precondition: shape is a GMANRaySphere or GMANRayDisk.
void emittingShapeCentreAndRadius(GMANRayInterface const& shape, GMANPoint& centre, RtFloat& radius) {
  EmittingShapeKind const kind = classifyEmittingShape(shape);
  GMANPoint objectCentre;
  if (kind.sphere != nullptr) {
    radius = kind.sphere->getRadius();
    objectCentre = GMANPoint(0.0f, 0.0f, 0.0f);
  } else {
    radius = kind.disk->getRadius();
    objectCentre = GMANPoint(0.0f, 0.0f, kind.disk->getHeight());
  }
  centre = gman::transformPoint(emittingShapeObjectToCamera(shape), objectCentre);
}

// One draw on an area emitter's shape, placed into camera space: sample(),
// samplePoint() and drawEmitterPoint's own callers all need the same point,
// normal and area from the same draw.
struct EmitterDraw {
  GMANPoint point;
  GMANVector normal;
  RtFloat area;
};

EmitterDraw drawEmitterPoint(Emitter const& emitter, RtFloat u1, RtFloat u2) {
  GMANVector objectNormal;
  GMANPoint const objectPoint = emittingShapeSamplePoint(*emitter.shape, u1, u2, objectNormal);
  GMANMatrix4 const objectToCamera = emittingShapeObjectToCamera(*emitter.shape);
  GMANMatrix4 cameraToObject;
  invertiblePlacement(objectToCamera, cameraToObject); // precondition: emitter came from emitters()

  EmitterDraw draw;
  placeEmitterPoint(objectToCamera, cameraToObject, objectPoint, objectNormal, draw.point, draw.normal);
  emittingShapeArea(*emitter.shape, draw.area);
  return draw;
}

// The area branch of sample(): draws a point on emitter's shape, places
// it, and reports the solid-angle pdf the area-to-solid-angle Jacobian
// gives, black and 0 on the emitter's own non-emitting side.
EmitterSample sampleArea(Emitter const& emitter, GMANPoint const& p, RtFloat u1, RtFloat u2) {
  EmitterSample result;
  result.isDelta = false;

  EmitterDraw const draw = drawEmitterPoint(emitter, u1, u2);

  GMANVector toEmitter(p, draw.point);
  RtFloat const distance = toEmitter.magnitude();
  toEmitter.normalize();
  result.wi = toEmitter;
  result.distance = distance;

  RtFloat const cosTheta = draw.normal.dot(-toEmitter);
  GMANColor const black((RtFloat)0.0, (RtFloat)0.0, (RtFloat)0.0);

  // pdf uses the Jacobian's own abs(cosTheta), 0 only where that Jacobian
  // is singular; Cl goes black separately, on the emitter's far side,
  // per its one-sided emission.
  if (cosTheta == 0.0f || !(distance > 0.0f)) {
    result.Cl = black;
    result.pdf = 0.0f;
    return result;
  }
  result.pdf = (distance * distance) / (draw.area * std::fabs(cosTheta));
  result.Cl = (cosTheta > 0.0f) ? emitter.light->getCl() : black;

  // The shadow walk's own end point: the sampled point offset off the
  // emitter's own surface, back toward p, so the walk ends short of that
  // surface rather than on it.
  result.shadowTarget =
      gman::offsetOrigin(draw.point, draw.normal, -toEmitter, gman::primitiveMagnitude(emitter.shape->getBBox()));
  return result;
}

// The area branch of samplePoint(): draws a point on emitter's shape and
// places it, reporting Le undoubled (no pi scale) and pdf per unit area.
EmitterPoint samplePointArea(Emitter const& emitter, RtFloat u1, RtFloat u2) {
  EmitterPoint result;
  result.isDelta = false;

  EmitterDraw const draw = drawEmitterPoint(emitter, u1, u2);

  result.point = draw.point;
  result.normal = GMANNormal(draw.normal.getX(), draw.normal.getY(), draw.normal.getZ());
  result.Le = emitter.light->getCl();
  result.pdf = 1.0f / draw.area;
  return result;
}

} // namespace

std::vector<Emitter> emitters(GMANWorldManager& world, std::size_t* ambientCount) {
  std::vector<Emitter> result;
  std::vector<GMANLight const*> deltaSeen;
  std::vector<GMANLight const*> ambientSeen;
  std::size_t ineligibleCount = 0;

  for (GMANPrimitive* primitive = world.getFirst(); primitive != nullptr; primitive = world.getNext()) {
    GMANRayInterface const* rayPrimitive = dynamic_cast<GMANRayInterface const*>(primitive);
    if (rayPrimitive == nullptr) {
      continue;
    }
    Appearance const& appearance = rayPrimitive->getAppearance();

    for (GMANLight const* light : appearance.lights) {
      if (light->getType() == GMAN_LIGHT_AMBIENT) {
        if (ambientCount != nullptr && std::find(ambientSeen.begin(), ambientSeen.end(), light) == ambientSeen.end()) {
          ambientSeen.push_back(light);
        }
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

  if (ambientCount != nullptr) {
    *ambientCount = ambientSeen.size();
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
    // normal are left default-constructed, as no caller reads either
    // for a delta emitter.
    break;
  }
  return result;
}

RtFloat lightChoiceWeight(Emitter const& emitter, GMANPoint const& p) {
  if (emitter.shape == nullptr) {
    EmitterSample const preview = sample(emitter, p, 0.5f, 0.5f);
    RtFloat const mean = meanChannel(preview.Cl);
    return (std::isfinite(mean) && mean > 0.0f) ? mean : 0.0f;
  }

  GMANPoint centre;
  RtFloat radius = 0.0f;
  emittingShapeCentreAndRadius(*emitter.shape, centre, radius);
  GMANVector const toCentre(p, centre);
  RtFloat const dSquared = toCentre.dot(toCentre);
  RtFloat const w = emitter.power / ((RtFloat)4.0 * GMANMax(dSquared, radius * radius));
  return (std::isfinite(w) && w > 0.0f) ? w : 0.0f;
}

} // namespace gman
