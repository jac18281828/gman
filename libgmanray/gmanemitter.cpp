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
#include <cassert>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numbers>
#include <optional>
#include <vector>

#include "gmanbbox.h"
#include "gmancolor.h"
#include "gmanemitter.h"
#include "gmaninvertiblematrix.h"
#include "gmanlightsourcemgr.h"
#include "gmanlog.h"
#include "gmanmath.h"
#include "gmanmatrix4.h"
#include "gmanpoint.h"
#include "gmanprimitive.h"
#include "gmanraydisk.h"
#include "gmanrayinterface.h"
#include "gmanrayoccluder.h"
#include "gmanraypolygon.h"
#include "gmanraypolygonmesh.h"
#include "gmanraysphere.h"
#include "gmanshading.h"
#include "gmanvector.h"
#include "gmanworldmanager.h"
#include "ri.h"

namespace gman {

namespace {

constexpr double kReflexTurnTolerance = 1.0e-5;
constexpr double kLargestBelowOne = 1.0 - std::numeric_limits<double>::epsilon() / 2.0;

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
// xor disk non-null xor mesh non-null, all null for an unsupported
// primitive type.
struct EmittingShapeKind {
  GMANRaySphere const* sphere;
  GMANRayDisk const* disk;
  GMANRayPolygonMesh const* mesh;
};

EmittingShapeKind classifyEmittingShape(GMANRayInterface const& shape) {
  if (GMANRaySphere const* sphere = dynamic_cast<GMANRaySphere const*>(&shape)) {
    return {sphere, nullptr, nullptr};
  }
  if (GMANRayDisk const* disk = dynamic_cast<GMANRayDisk const*>(&shape)) {
    return {nullptr, disk, nullptr};
  }
  return {nullptr, nullptr, dynamic_cast<GMANRayPolygonMesh const*>(&shape)};
}

// True, and the object-space area filled, for a geometrically eligible sphere
// or disk. False for any other primitive type, a polygon mesh included, or a
// degenerate one; area is left untouched.
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

// shape's own shutter-open placement, empty for a singular one. Precondition:
// shape is a GMANRaySphere or GMANRayDisk, the two emitting types a placement
// describes.
std::optional<InvertibleMatrix> const& emittingShapeObjectToCamera(GMANRayInterface const& shape) {
  EmittingShapeKind const kind = classifyEmittingShape(shape);
  return (kind.sphere != nullptr) ? kind.sphere->getObjectToCamera() : kind.disk->getObjectToCamera();
}

// shape's own uniform-by-area draw, in object space. Precondition: shape is
// a GMANRaySphere or GMANRayDisk.
GMANPoint emittingShapeSamplePoint(GMANRayInterface const& shape, double u1, double u2, GMANVector& normal) {
  EmittingShapeKind const kind = classifyEmittingShape(shape);
  return (kind.sphere != nullptr) ? kind.sphere->samplePoint(u1, u2, normal) : kind.disk->samplePoint(u1, u2, normal);
}

// Places an object-space point and normal by objectToCamera, the normal
// through its inverse transpose, then normalized --
// renderers/radiosity/gmanradiositymesh.cpp's own pattern, reused exactly.
// RiOrientation/RiSides are not consulted, matching that file's own choice.
void placeEmitterPoint(InvertibleMatrix const& objectToCamera, GMANPoint const& objectPoint,
                       GMANVector const& objectNormal, GMANPoint& cameraPoint, GMANVector& cameraNormal) {
  cameraPoint = gman::transformPoint(objectToCamera, objectPoint);
  cameraNormal = gman::transformNormal(objectToCamera, objectNormal);
  cameraNormal.normalize();
}

// The camera-space area element's own Jacobian at a point whose object-space
// unit normal is objectNormal: |det L| * |n'|, L the linear part of
// objectToCamera (an affine matrix's own 4x4 determinant already is det L,
// its translation contributing nothing) and n' objectNormal transformed by
// objectToCamera's inverse transpose, before normalizing. Exact for any
// invertible affine placement; 1 within float rounding for a rigid one.
RtFloat areaJacobian(InvertibleMatrix const& objectToCamera, GMANVector const& objectNormal) {
  GMANMatrix4 linear = objectToCamera.forward(); // determinant() is non-const
  RtFloat const linearDet = linear.determinant();
  GMANVector nPrime = gman::transformNormal(objectToCamera, objectNormal); // magnitude() is non-const
  return std::fabs(linearDet) * nPrime.magnitude();
}

// Whether every coordinate of p is finite.
bool finitePoint(GMANPoint const& p) {
  return std::isfinite(p.getX()) && std::isfinite(p.getY()) && std::isfinite(p.getZ());
}

// The area of triangle (a, b, c) along normal, negative when it winds against
// normal.
double signedTriangleArea(GMANPoint const& a, GMANPoint const& b, GMANPoint const& c, GMANVector const& normal) {
  return 0.5 * GMANVector(a, b).cross(GMANVector(a, c)).dot(normal);
}

// The area of face's fan triangle (v0, v[i], v[i + 1]); 0 when it winds
// against the face or is not finite.
double fanTriangleArea(GMANRayPolygon const& face, std::size_t i) {
  std::vector<GMANPoint> const& loop = face.getOuterLoop();
  double const area = signedTriangleArea(loop[0], loop[i], loop[i + 1], face.getPlaneNormal());
  return (std::isfinite(area) && area > 0.0) ? area : 0.0;
}

// The sum of face's fan triangles' areas; 0 for a degenerate face, whose plane
// normal is unset.
double faceArea(GMANRayPolygon const& face) {
  if (face.isDegenerate()) {
    return 0.0;
  }
  double area = 0.0;
  for (std::size_t i = 1; i + 1 < face.getOuterLoop().size(); ++i) {
    area += fanTriangleArea(face, i);
  }
  return area;
}

// The sum of mesh's faces' areas.
double meshArea(GMANRayPolygonMesh const& mesh) {
  double area = 0.0;
  for (std::size_t f = 0; f < mesh.getFaceCount(); ++f) {
    area += faceArea(mesh.getFace(f));
  }
  return area;
}

// The index of the nearest vertex from i, forward or backward around loop,
// that differs from loop[i]; i when none does.
std::size_t distinctNeighbour(std::vector<GMANPoint> const& loop, std::size_t i, bool forward) {
  std::size_t const count = loop.size();
  for (std::size_t step = 1; step < count; ++step) {
    std::size_t const offset = forward ? step : count - step;
    std::size_t const j = (i + offset) % count;
    if (loop[j] != loop[i]) {
      return j;
    }
  }
  return i;
}

// Whether the turn at loop[i], its edges to its nearest distinct neighbours
// crossed along normal, falls below -kReflexTurnTolerance times area; a
// non-finite turn does not.
bool reflexVertex(std::vector<GMANPoint> const& loop, std::size_t i, GMANVector const& normal, double area) {
  GMANPoint const& previous = loop[distinctNeighbour(loop, i, false)];
  GMANPoint const& next = loop[distinctNeighbour(loop, i, true)];
  double const turn = GMANVector(previous, loop[i]).cross(GMANVector(loop[i], next)).dot(normal);
  return turn < -kReflexTurnTolerance * area;
}

// Whether any vertex of the non-degenerate face is reflex.
bool concaveFace(GMANRayPolygon const& face) {
  std::vector<GMANPoint> const& loop = face.getOuterLoop();
  double const area = faceArea(face);
  for (std::size_t i = 0; i < loop.size(); ++i) {
    if (reflexVertex(loop, i, face.getPlaneNormal(), area)) {
      return true;
    }
  }
  return false;
}

// Whether mesh can emit: every vertex finite, no face with a hole or a reflex
// vertex, and a finite positive area.
bool meshEligible(GMANRayPolygonMesh const& mesh) {
  for (std::size_t f = 0; f < mesh.getFaceCount(); ++f) {
    GMANRayPolygon const& face = mesh.getFace(f);
    if (!face.getHoles().empty() || !std::ranges::all_of(face.getOuterLoop(), finitePoint)) {
      return false;
    }
    if (!face.isDegenerate() && concaveFace(face)) {
      return false;
    }
  }
  double const area = meshArea(mesh);
  return std::isfinite(area) && area > 0.0;
}

// The centre and half the diagonal of mesh's camera-space bounding box.
void meshCentreAndRadius(GMANRayPolygonMesh const& mesh, GMANPoint& centre, RtFloat& radius) {
  GMANPoint const low = mesh.getBBox().getMin();
  GMANPoint const high = mesh.getBBox().getMax();
  centre = GMANPoint((low.getX() + high.getX()) / (RtFloat)2.0, (low.getY() + high.getY()) / (RtFloat)2.0,
                     (low.getZ() + high.getZ()) / (RtFloat)2.0);
  GMANVector diagonal(low, high);
  radius = diagonal.magnitude() / (RtFloat)2.0;
}

// The solid-angle density of a point drawn uniformly by area over mesh, seen
// from distance away under cosTheta to the drawn face's normal; 0 for a mesh
// with no area.
RtFloat meshSolidAnglePdf(GMANRayPolygonMesh const& mesh, RtFloat distance, RtFloat cosTheta) {
  RtFloat const area = static_cast<RtFloat>(meshArea(mesh));
  if (!std::isfinite(area) || !(area > 0.0f)) {
    return 0.0f;
  }
  return (distance * distance) / (area * cosTheta);
}

struct Pick {
  std::size_t index;
  double fraction;
};

// The item whose interval in the running sum of weightOf(0 .. count - 1)
// holds u * total, and u rescaled to [0, 1) within it; the last item of
// positive weight when rounding leaves u * total past the sum; empty when no
// weight is positive.
template <class WeightOf>
std::optional<Pick> pickByWeight(std::size_t count, double total, double u, WeightOf const& weightOf) {
  double const target = u * total;
  double lower = 0.0;
  std::optional<Pick> pick;
  for (std::size_t i = 0; i < count; ++i) {
    double const weight = weightOf(i);
    if (!(weight > 0.0)) {
      continue;
    }
    pick = Pick{i, std::clamp((target - lower) / weight, 0.0, kLargestBelowOne)};
    double const upper = lower + weight;
    if (target < upper) {
      return pick;
    }
    lower = upper;
  }
  return pick;
}

// shape's own placed centre (camera space) and bounding radius: the
// sphere's own centre and radius, or the disk's own point at its height
// and radius, each placed by shape's own object-to-camera transform; a
// polygon mesh's own bounding box's centre and half its diagonal.
// Precondition: shape is a GMANRaySphere, GMANRayDisk or GMANRayPolygonMesh.
// False, centre and radius untouched, for a singular placement.
bool emittingShapeCentreAndRadius(GMANRayInterface const& shape, GMANPoint& centre, RtFloat& radius) {
  EmittingShapeKind const kind = classifyEmittingShape(shape);
  if (kind.mesh != nullptr) {
    meshCentreAndRadius(*kind.mesh, centre, radius);
    return true;
  }
  std::optional<InvertibleMatrix> const& objectToCamera = emittingShapeObjectToCamera(shape);
  if (!objectToCamera) {
    return false;
  }
  GMANPoint objectCentre;
  if (kind.sphere != nullptr) {
    radius = kind.sphere->getRadius();
    objectCentre = GMANPoint(0.0f, 0.0f, 0.0f);
  } else {
    radius = kind.disk->getRadius();
    objectCentre = GMANPoint(0.0f, 0.0f, kind.disk->getHeight());
  }
  centre = gman::transformPoint(*objectToCamera, objectCentre);
  return true;
}

// The camera-space area emitters() needs for power: exact for a disk,
// whose area Jacobian is constant over its own plane and so needs no
// sampled point, evaluated from its own fixed (0, 0, -1) normal; an
// estimate for a sphere, whose Jacobian varies pointwise, via |det L|^(2/3)
// -- the area factor a uniform scale of that same determinant would give,
// exact under a rigid or uniformly scaled placement. Precondition: shape
// is a GMANRaySphere or GMANRayDisk.
RtFloat emittingShapeCameraArea(GMANRayInterface const& shape, InvertibleMatrix const& objectToCamera,
                                RtFloat objectArea) {
  EmittingShapeKind const kind = classifyEmittingShape(shape);
  if (kind.disk != nullptr) {
    GMANVector const diskNormal(0.0f, 0.0f, -1.0f);
    return objectArea * areaJacobian(objectToCamera, diskNormal);
  }
  GMANMatrix4 linear = objectToCamera.forward(); // determinant() is non-const
  RtFloat const linearDet = std::fabs(linear.determinant());
  return objectArea * std::pow(linearDet, (RtFloat)(2.0 / 3.0));
}

// One draw on an area emitter's shape, placed into camera space: sample(),
// samplePoint() and drawEmitterPoint's own callers all need the same
// point, normal, area and area Jacobian from the same draw.
struct EmitterDraw {
  GMANPoint point;
  GMANVector normal;
  RtFloat area;
  RtFloat jacobian;
};

// The point of triangle (a, b, c) that the square-root warp of (u1, u2) picks,
// uniform by area: barycentric weights (1 - sqrt(u1), u2 sqrt(u1), sqrt(u1) (1 - u2)).
GMANPoint pointInTriangle(GMANPoint const& a, GMANPoint const& b, GMANPoint const& c, double u1, double u2) {
  double const root = std::sqrt(u1);
  double const weightA = 1.0 - root;
  double const weightB = u2 * root;
  double const weightC = root * (1.0 - u2);
  return GMANPoint(static_cast<RtFloat>(weightA * a.getX() + weightB * b.getX() + weightC * c.getX()),
                   static_cast<RtFloat>(weightA * a.getY() + weightB * b.getY() + weightC * c.getY()),
                   static_cast<RtFloat>(weightA * a.getZ() + weightB * b.getZ() + weightC * c.getZ()));
}

// A point drawn uniformly by area over mesh, with its face's plane normal, the
// mesh's total area and area Jacobian 1; empty for a mesh with no area.
std::optional<EmitterDraw> drawMeshPoint(GMANRayPolygonMesh const& mesh, RtFloat u1, RtFloat u2) {
  double const total = meshArea(mesh);
  if (!std::isfinite(total) || !(total > 0.0)) {
    return std::nullopt;
  }
  auto const faceAreaAt = [&mesh](std::size_t f) { return faceArea(mesh.getFace(f)); };
  std::optional<Pick> const facePick = pickByWeight(mesh.getFaceCount(), total, u1, faceAreaAt);
  if (!facePick) {
    return std::nullopt;
  }
  GMANRayPolygon const& face = mesh.getFace(facePick->index);
  auto const fanAreaAt = [&face](std::size_t t) { return fanTriangleArea(face, t + 1); };
  std::vector<GMANPoint> const& loop = face.getOuterLoop();
  std::optional<Pick> const trianglePick = pickByWeight(loop.size() - 2, faceArea(face), facePick->fraction, fanAreaAt);
  if (!trianglePick) {
    return std::nullopt;
  }

  EmitterDraw draw;
  draw.point = pointInTriangle(loop[0], loop[trianglePick->index + 1], loop[trianglePick->index + 2],
                               trianglePick->fraction, u2);
  draw.normal = face.getPlaneNormal();
  draw.area = static_cast<RtFloat>(total);
  draw.jacobian = (RtFloat)1.0;
  return draw;
}

// Empty for an emitter whose shape's placement is singular, or whose polygon
// mesh has no area.
std::optional<EmitterDraw> drawEmitterPoint(Emitter const& emitter, RtFloat u1, RtFloat u2) {
  if (GMANRayPolygonMesh const* mesh = classifyEmittingShape(*emitter.shape).mesh) {
    return drawMeshPoint(*mesh, u1, u2);
  }
  std::optional<InvertibleMatrix> const& placement = emittingShapeObjectToCamera(*emitter.shape);
  if (!placement) {
    return std::nullopt;
  }
  InvertibleMatrix const& objectToCamera = *placement;

  GMANVector objectNormal;
  GMANPoint const objectPoint = emittingShapeSamplePoint(*emitter.shape, u1, u2, objectNormal);

  EmitterDraw draw;
  placeEmitterPoint(objectToCamera, objectPoint, objectNormal, draw.point, draw.normal);
  emittingShapeArea(*emitter.shape, draw.area);
  draw.jacobian = areaJacobian(objectToCamera, objectNormal);
  return draw;
}

// The area branch of sample(): draws a point on emitter's shape, places
// it, and reports the solid-angle pdf the area-to-solid-angle Jacobian
// gives, black and 0 on the emitter's own non-emitting side. Every field
// zero, Cl black and pdf 0, for a singular placement or a polygon mesh with no
// area.
EmitterSample sampleArea(Emitter const& emitter, GMANPoint const& p, RtFloat u1, RtFloat u2) {
  EmitterSample result;
  result.wi = GMANVector(0.0f, 0.0f, 0.0f);
  result.distance = 0.0f;
  result.shadowTarget = GMANPoint(0.0f, 0.0f, 0.0f);
  result.Cl = GMANColor(0.0f, 0.0f, 0.0f);
  result.pdf = 0.0f;
  result.isDelta = false;

  std::optional<EmitterDraw> const drawn = drawEmitterPoint(emitter, u1, u2);
  if (!drawn) {
    return result;
  }
  EmitterDraw const& draw = *drawn;

  GMANVector toEmitter(p, draw.point);
  RtFloat const distance = toEmitter.magnitude();
  toEmitter.normalize();
  result.wi = toEmitter;
  result.distance = distance;

  RtFloat const cosTheta = draw.normal.dot(-toEmitter);
  GMANColor const black((RtFloat)0.0, (RtFloat)0.0, (RtFloat)0.0);

  // pdf uses the area-to-solid-angle Jacobian's own abs(cosTheta), 0 only
  // where that Jacobian is singular; Cl goes black separately, on the
  // emitter's far side, per its one-sided emission.
  if (cosTheta == 0.0f || !(distance > 0.0f)) {
    return result;
  }
  result.pdf = (distance * distance) / (draw.area * draw.jacobian * std::fabs(cosTheta));
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
// Every field zero, Le black and pdf 0, for a singular placement or a polygon
// mesh with no area.
EmitterPoint samplePointArea(Emitter const& emitter, RtFloat u1, RtFloat u2) {
  EmitterPoint result;
  result.point = GMANPoint(0.0f, 0.0f, 0.0f);
  result.normal = GMANNormal(0.0f, 0.0f, 0.0f);
  result.Le = GMANColor(0.0f, 0.0f, 0.0f);
  result.pdf = 0.0f;
  result.isDelta = false;

  std::optional<EmitterDraw> const drawn = drawEmitterPoint(emitter, u1, u2);
  if (!drawn) {
    return result;
  }
  EmitterDraw const& draw = *drawn;

  result.point = draw.point;
  result.normal = GMANNormal(draw.normal.getX(), draw.normal.getY(), draw.normal.getZ());
  result.Le = emitter.light->getCl();
  result.pdf = 1.0f / (draw.area * draw.jacobian);
  return result;
}

// pi * mean(Cl) * the shape's camera-space area; empty for an ineligible or
// unsupported shape.
std::optional<RtFloat> areaEmitterPower(GMANRayInterface const& shape, GMANLight const& light) {
  RtFloat const radiance = meanChannel(light.getCl());
  if (GMANRayPolygonMesh const* mesh = classifyEmittingShape(shape).mesh) {
    if (!meshEligible(*mesh)) {
      return std::nullopt;
    }
    return std::numbers::pi_v<RtFloat> * radiance * static_cast<RtFloat>(meshArea(*mesh));
  }
  RtFloat area = 0.0f;
  if (!emittingShapeArea(shape, area)) {
    return std::nullopt;
  }
  std::optional<InvertibleMatrix> const& objectToCamera = emittingShapeObjectToCamera(shape);
  if (!objectToCamera) {
    return std::nullopt;
  }
  return std::numbers::pi_v<RtFloat> * radiance * emittingShapeCameraArea(shape, *objectToCamera, area);
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
    std::optional<RtFloat> const power = areaEmitterPower(*rayPrimitive, *appearance.areaLight);
    if (!power) {
      ++ineligibleCount;
      continue;
    }
    result.push_back({appearance.areaLight, rayPrimitive, *power});
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
  result.Cl = GMANColor(cl.getRed() * std::numbers::pi_v<RtFloat>, cl.getGreen() * std::numbers::pi_v<RtFloat>,
                        cl.getBlue() * std::numbers::pi_v<RtFloat>);
  result.pdf = 1.0f;
  result.isDelta = true;
  return result;
}

RtFloat lightSolidAnglePdf(Emitter const& emitter, GMANPoint const& p, GMANPoint const& hitPoint,
                           GMANNormal const& hitNormal) {
  assert(emitter.shape != nullptr);

  GMANVector toHit(p, hitPoint);
  RtFloat const distance = toHit.magnitude();
  if (!(distance > 0.0f)) {
    return 0.0f;
  }
  toHit.normalize();

  RtFloat const cosTheta = hitNormal.dot(-toHit);
  if (!(cosTheta > 0.0f)) {
    return 0.0f;
  }

  if (GMANRayPolygonMesh const* mesh = classifyEmittingShape(*emitter.shape).mesh) {
    return meshSolidAnglePdf(*mesh, distance, cosTheta);
  }

  std::optional<InvertibleMatrix> const& placement = emittingShapeObjectToCamera(*emitter.shape);
  if (!placement) {
    return 0.0f;
  }
  InvertibleMatrix const& objectToCamera = *placement;

  RtFloat area = 0.0f;
  emittingShapeArea(*emitter.shape, area);

  // hitPoint's own object-space normal, recovered by the inverse of the
  // object-to-camera map placeEmitterPoint applies to a freshly drawn
  // one -- exact for the point a real draw or a real ray hit actually
  // placed, which hitPoint always is.
  GMANVector objectNormal = gman::transformNormal(objectToCamera.inverse(), hitNormal);
  objectNormal.normalize();
  RtFloat const jacobian = areaJacobian(objectToCamera, objectNormal);

  return (distance * distance) / (area * jacobian * cosTheta);
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
  if (!emittingShapeCentreAndRadius(*emitter.shape, centre, radius)) {
    return 0.0f;
  }
  GMANVector const toCentre(p, centre);
  RtFloat const dSquared = toCentre.dot(toCentre);
  RtFloat const w = emitter.power / ((RtFloat)4.0 * GMANMax(dSquared, radius * radius));
  return (std::isfinite(w) && w > 0.0f) ? w : 0.0f;
}

} // namespace gman
