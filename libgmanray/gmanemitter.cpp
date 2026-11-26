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
#include <array>
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

// Scales the radius of a full disk of a disk sector's area into the distance
// below which a receiving point draws through the sector's bounding rectangle
// by solid angle, with rejection, and at or beyond which it draws by area. A
// draw by area turns heavy through 1 / d^2 near the disk; a rejecting draw
// adds 0/1 noise far from it, where a draw by area is nearly noise-free. At
// 0.75 the choice keeps the noise within 1.08 times the better strategy's
// for a full disk and within 1.23 times for 30 to 270 degree sectors, over
// views 0 to 89 degrees off the normal and 1, 4 and 16 draws.
constexpr double kDiskNearField = 0.75;

// Scales a partial sphere's radius into the distance from its full sphere's
// surface below which a receiving point draws in the cone the full sphere
// subtends, rejecting a point outside the zone, and at or beyond which it
// draws by area. The cone everywhere loses to the draw by area by up to 4.1
// times on a small cap seen from afar, and the draw by area everywhere by up
// to 5.5 times near any zone; at 1.5 the choice keeps the noise within 1.74
// times the better strategy's over six zones seen from twelve views.
constexpr double kZoneNearField = 1.5;

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

// The mesh a face of a polygon mesh belongs to, else primitive itself: the
// shape an emitter names for that hit.
GMANRayInterface const* emittingShapeOf(GMANRayInterface const* primitive) {
  GMANRayPolygon const* face = dynamic_cast<GMANRayPolygon const*>(primitive);
  if (face == nullptr) {
    return primitive;
  }
  GMANRayPolygonMesh const* mesh = face->getMesh();
  if (mesh == nullptr) {
    return primitive;
  }
  return mesh;
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

// A point or direction in double, the precision the solid-angle draws keep
// from a receiving point to the point they place.
struct DoubleVector {
  double x;
  double y;
  double z;
};

DoubleVector operator+(DoubleVector const& a, DoubleVector const& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }

DoubleVector operator-(DoubleVector const& a, DoubleVector const& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }

DoubleVector operator-(DoubleVector const& a) { return {-a.x, -a.y, -a.z}; }

DoubleVector operator*(double s, DoubleVector const& a) { return {s * a.x, s * a.y, s * a.z}; }

double dot(DoubleVector const& a, DoubleVector const& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

DoubleVector cross(DoubleVector const& a, DoubleVector const& b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

double length(DoubleVector const& a) { return std::sqrt(dot(a, a)); }

// a over its length: not finite for a zero or non-finite a.
DoubleVector normalized(DoubleVector const& a) { return (1.0 / length(a)) * a; }

bool finiteVector(DoubleVector const& a) { return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z); }

DoubleVector toDouble(GMANPoint const& p) { return {p.getX(), p.getY(), p.getZ()}; }

DoubleVector toDouble(GMANVector const& v) { return {v.getX(), v.getY(), v.getZ()}; }

GMANPoint toPoint(DoubleVector const& a) {
  return GMANPoint(static_cast<RtFloat>(a.x), static_cast<RtFloat>(a.y), static_cast<RtFloat>(a.z));
}

GMANVector toVector(DoubleVector const& a) {
  return GMANVector(static_cast<RtFloat>(a.x), static_cast<RtFloat>(a.y), static_cast<RtFloat>(a.z));
}

// x, or 0 for a negative x, as a square root's argument rounding may push
// below 0; not a number when x is not.
double nonNegative(double x) { return (x < 0.0) ? 0.0 : x; }

// The angle between unit vectors u and v, accurate near 0 and near pi; not a
// number when either is not.
double angleBetween(DoubleVector const& u, DoubleVector const& v) {
  if (dot(u, v) < 0.0) {
    return std::numbers::pi - 2.0 * std::asin(std::clamp(length(u + v) / 2.0, 0.0, 1.0));
  }
  return 2.0 * std::asin(std::clamp(length(v - u) / 2.0, 0.0, 1.0));
}

// The spherical triangle a triangle subtends from a point: the unit
// directions a, b and c to its corners, the interior angle alpha at a and
// its solid angle, the interior angles' sum less pi (Girard's theorem). Not
// finite when two directions coincide or a corner lies at the point.
struct SphericalTriangle {
  DoubleVector a;
  DoubleVector b;
  DoubleVector c;
  double alpha;
  double solidAngle;
};

SphericalTriangle sphericalTriangle(DoubleVector const& p, DoubleVector const& v0, DoubleVector const& v1,
                                    DoubleVector const& v2) {
  SphericalTriangle triangle;
  triangle.a = normalized(v0 - p);
  triangle.b = normalized(v1 - p);
  triangle.c = normalized(v2 - p);
  const auto ab = normalized(cross(triangle.a, triangle.b));
  const auto bc = normalized(cross(triangle.b, triangle.c));
  const auto ca = normalized(cross(triangle.c, triangle.a));
  triangle.alpha = angleBetween(ab, -ca);
  const auto beta = angleBetween(bc, -ab);
  const auto gamma = angleBetween(ca, -bc);
  triangle.solidAngle = triangle.alpha + beta + gamma - std::numbers::pi;
  return triangle;
}

// A direction uniform over triangle's solid angle, by Arvo's inversion
// ("Stratified sampling of spherical triangles", 1995): u1 sets the
// sub-triangle area u1 * solidAngle, which fixes the point c' on arc ac, and
// u2 sets the cosine z = 1 - u2 * (1 - c' . b) along arc bc'. Not finite
// where the inversion fails.
DoubleVector sphericalTriangleDirection(SphericalTriangle const& triangle, double u1, double u2) {
  const auto subArea = u1 * triangle.solidAngle;
  const auto s = std::sin(subArea - triangle.alpha);
  const auto t = std::cos(subArea - triangle.alpha);
  const auto cosAlpha = std::cos(triangle.alpha);
  const auto sinAlpha = std::sin(triangle.alpha);
  const auto u = t - cosAlpha;
  const auto v = s + sinAlpha * dot(triangle.a, triangle.b);
  const auto q = std::clamp(((v * t - u * s) * cosAlpha - v) / ((v * s + u * t) * sinAlpha), -1.0, 1.0);
  const auto awayFromA = normalized(triangle.c - dot(triangle.c, triangle.a) * triangle.a);
  const auto cPrime = q * triangle.a + std::sqrt((1.0 - q) * (1.0 + q)) * awayFromA;
  const auto oneMinusZ = u2 * (1.0 - dot(cPrime, triangle.b));
  const auto awayFromB = normalized(cPrime - dot(cPrime, triangle.b) * triangle.b);
  const auto sinZ = std::sqrt(nonNegative(oneMinusZ * (2.0 - oneMinusZ)));
  return (1.0 - oneMinusZ) * triangle.b + sinZ * awayFromB;
}

// The point where the ray from p along direction meets the plane of triangle
// (v0, v1, v2); empty unless the ray meets it ahead of p at a finite point.
std::optional<DoubleVector> rayPlanePoint(DoubleVector const& p, DoubleVector const& direction, DoubleVector const& v0,
                                          DoubleVector const& v1, DoubleVector const& v2) {
  const auto normal = cross(v1 - v0, v2 - v0);
  const auto t = dot(v0 - p, normal) / dot(direction, normal);
  if (!std::isfinite(t) || !(t > 0.0)) {
    return std::nullopt;
  }
  const auto point = p + t * direction;
  if (!finiteVector(point)) {
    return std::nullopt;
  }
  return point;
}

// A point drawn uniformly in the solid angle an emitter subtends from a
// receiving point, the camera-space normal of its emitting side there, and
// the density of the draw per unit solid angle.
struct SolidAngleDraw {
  DoubleVector point;
  GMANVector normal;
  double pdf;
};

// The point of triangle (v0, v1, v2) seen from p along a direction drawn
// uniformly in its solid angle, where that ray meets the triangle's plane;
// empty when the direction or that point is not finite.
std::optional<DoubleVector> pointInSphericalTriangle(DoubleVector const& p, DoubleVector const& v0,
                                                     DoubleVector const& v1, DoubleVector const& v2, double u1,
                                                     double u2) {
  const auto direction = sphericalTriangleDirection(sphericalTriangle(p, v0, v1, v2), u1, u2);
  if (!finiteVector(direction)) {
    return std::nullopt;
  }
  return rayPlanePoint(p, direction, v0, v1, v2);
}

// density as a float, 0 unless that is finite and positive.
RtFloat positiveDensity(double density) {
  const auto rounded = static_cast<RtFloat>(density);
  return (std::isfinite(rounded) && rounded > 0.0f) ? rounded : 0.0f;
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

// The solid angle from p of face's fan triangle (v0, v[i], v[i + 1]): 0
// unless its area is positive, p lies strictly on the face's emitting side
// and that solid angle is finite and positive.
double fanTriangleSolidAngle(GMANRayPolygon const& face, std::size_t i, DoubleVector const& p) {
  if (!(fanTriangleArea(face, i) > 0.0)) {
    return 0.0;
  }
  auto const& loop = face.getOuterLoop();
  const auto v0 = toDouble(loop[0]);
  if (!(dot(p - v0, toDouble(face.getPlaneNormal())) > 0.0)) {
    return 0.0;
  }
  const auto solidAngle = sphericalTriangle(p, v0, toDouble(loop[i]), toDouble(loop[i + 1])).solidAngle;
  return (std::isfinite(solidAngle) && solidAngle > 0.0) ? solidAngle : 0.0;
}

// The sum of face's fan triangles' solid angles from p; 0 for a degenerate
// face, whose plane normal is unset.
double faceSolidAngle(GMANRayPolygon const& face, DoubleVector const& p) {
  if (face.isDegenerate()) {
    return 0.0;
  }
  double solidAngle = 0.0;
  for (std::size_t i = 1; i + 1 < face.getOuterLoop().size(); ++i) {
    solidAngle += fanTriangleSolidAngle(face, i, p);
  }
  return solidAngle;
}

// The sum of mesh's faces' solid angles from p: the solid angle its emitting
// sides subtend there, back-facing faces excluded.
double meshSolidAngle(GMANRayPolygonMesh const& mesh, DoubleVector const& p) {
  double solidAngle = 0.0;
  for (std::size_t f = 0; f < mesh.getFaceCount(); ++f) {
    solidAngle += faceSolidAngle(mesh.getFace(f), p);
  }
  return solidAngle;
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

// A point drawn uniformly in the solid angle mesh's emitting sides subtend
// from p: u1 picks a face, then one of its fan triangles, each in proportion
// to its solid angle, and the rescaled u1 and u2 draw a direction in that
// triangle's spherical triangle. The density is 1 / that solid angle at
// every point. Empty when the mesh subtends none from p or the draw is not
// finite.
std::optional<SolidAngleDraw> drawMeshBySolidAngle(GMANRayPolygonMesh const& mesh, DoubleVector const& p, double u1,
                                                   double u2) {
  const auto total = meshSolidAngle(mesh, p);
  if (!std::isfinite(total) || !(total > 0.0)) {
    return std::nullopt;
  }
  const auto faceAt = [&mesh, &p](std::size_t f) { return faceSolidAngle(mesh.getFace(f), p); };
  const auto facePick = pickByWeight(mesh.getFaceCount(), total, u1, faceAt);
  if (!facePick) {
    return std::nullopt;
  }
  auto const& face = mesh.getFace(facePick->index);
  auto const& loop = face.getOuterLoop();
  const auto triangleAt = [&face, &p](std::size_t t) { return fanTriangleSolidAngle(face, t + 1, p); };
  const auto trianglePick = pickByWeight(loop.size() - 2, faceSolidAngle(face, p), facePick->fraction, triangleAt);
  if (!trianglePick) {
    return std::nullopt;
  }
  const auto i = trianglePick->index + 1;
  const auto point = pointInSphericalTriangle(p, toDouble(loop[0]), toDouble(loop[i]), toDouble(loop[i + 1]),
                                              trianglePick->fraction, u2);
  if (!point) {
    return std::nullopt;
  }
  return SolidAngleDraw{*point, face.getPlaneNormal(), 1.0 / total};
}

// p carried into object space by the inverse of objectToCamera, in double.
DoubleVector objectPoint(InvertibleMatrix const& objectToCamera, GMANPoint const& p) {
  auto const& m = objectToCamera.backward();
  const auto x = static_cast<double>(p.getX());
  const auto y = static_cast<double>(p.getY());
  const auto z = static_cast<double>(p.getZ());
  return {x * m[0][0] + y * m[1][0] + z * m[2][0] + m[3][0], x * m[0][1] + y * m[1][1] + z * m[2][1] + m[3][1],
          x * m[0][2] + y * m[1][2] + z * m[2][2] + m[3][2]};
}

// An object-space point placed in camera space by objectToCamera, in double.
DoubleVector placedPoint(InvertibleMatrix const& objectToCamera, DoubleVector const& q) {
  auto const& m = objectToCamera.forward();
  return {q.x * m[0][0] + q.y * m[1][0] + q.z * m[2][0] + m[3][0],
          q.x * m[0][1] + q.y * m[1][1] + q.z * m[2][1] + m[3][1],
          q.x * m[0][2] + q.y * m[1][2] + q.z * m[2][2] + m[3][2]};
}

// thetamax in degrees clamped to [0, 360], as GMANDisk::area and
// GMANDisk::samplePoint clamp it.
double clampedDegrees(RtFloat thetamax) { return GMANMin(GMANMax(static_cast<double>(thetamax), 0.0), 360.0); }

double turnRadians(double degrees) { return degrees / 360.0 * 2.0 * std::numbers::pi; }

// The smallest axis-aligned rectangle in a disk's plane holding its sector:
// the centre, the arc's two ends and each axis point the arc passes.
struct SectorBounds {
  double xMin;
  double xMax;
  double yMin;
  double yMax;
};

SectorBounds sectorBounds(GMANRayDisk const& disk) {
  const auto radius = static_cast<double>(disk.getRadius());
  const auto degrees = clampedDegrees(disk.getThetaMax());
  const auto angle = turnRadians(degrees);
  auto bounds = SectorBounds{0.0, radius, 0.0, 0.0};
  bounds.xMin = std::min(bounds.xMin, radius * std::cos(angle));
  bounds.yMin = std::min(bounds.yMin, radius * std::sin(angle));
  bounds.yMax = std::max(bounds.yMax, radius * std::sin(angle));
  if (degrees >= 90.0) {
    bounds.yMax = radius;
  }
  if (degrees >= 180.0) {
    bounds.xMin = -radius;
  }
  if (degrees >= 270.0) {
    bounds.yMin = -radius;
  }
  return bounds;
}

// How a disk draws from a receiving point: nothing from on or behind its
// plane, which its object-space normal (0, 0, -1) faces away from; through
// its bounding rectangle by solid angle, with rejection, nearer that
// rectangle than kDiskNearField times the radius of a full disk of its
// sector's area; by area beyond.
enum class DiskStrategy { none, rectangle, area };

// The one decision sample() and lightSolidAnglePdf() share, so both answer
// the same density at the same p.
DiskStrategy diskStrategy(GMANRayDisk const& disk, InvertibleMatrix const& objectToCamera, GMANPoint const& p) {
  const auto q = objectPoint(objectToCamera, p);
  const auto height = static_cast<double>(disk.getHeight());
  if (!(q.z < height)) {
    return DiskStrategy::none;
  }
  const auto bounds = sectorBounds(disk);
  const auto dx = q.x - std::clamp(q.x, bounds.xMin, bounds.xMax);
  const auto dy = q.y - std::clamp(q.y, bounds.yMin, bounds.yMax);
  const auto distance = length({dx, dy, q.z - height});
  const auto equivalentRadius =
      static_cast<double>(disk.getRadius()) * std::sqrt(clampedDegrees(disk.getThetaMax()) / 360.0);
  if (distance < kDiskNearField * equivalentRadius) {
    return DiskStrategy::rectangle;
  }
  return DiskStrategy::area;
}

// The disk's bounding rectangle placed in camera space, corners in order
// around it from (xMin, yMin).
std::array<DoubleVector, 4> rectangleCorners(GMANRayDisk const& disk, InvertibleMatrix const& objectToCamera) {
  const auto bounds = sectorBounds(disk);
  const auto height = static_cast<double>(disk.getHeight());
  return {placedPoint(objectToCamera, {bounds.xMin, bounds.yMin, height}),
          placedPoint(objectToCamera, {bounds.xMax, bounds.yMin, height}),
          placedPoint(objectToCamera, {bounds.xMax, bounds.yMax, height}),
          placedPoint(objectToCamera, {bounds.xMin, bounds.yMax, height})};
}

// The solid angle from p of the rectangle's triangle t, (c0, c1, c2) or (c0,
// c2, c3); 0 unless finite and positive.
double rectangleTriangleSolidAngle(std::array<DoubleVector, 4> const& corners, DoubleVector const& p, std::size_t t) {
  const auto solidAngle = sphericalTriangle(p, corners[0], corners[t + 1], corners[t + 2]).solidAngle;
  return (std::isfinite(solidAngle) && solidAngle > 0.0) ? solidAngle : 0.0;
}

double rectangleSolidAngle(std::array<DoubleVector, 4> const& corners, DoubleVector const& p) {
  return rectangleTriangleSolidAngle(corners, p, 0) + rectangleTriangleSolidAngle(corners, p, 1);
}

// The angle of the object-space point (x, y) about z from the +x axis, in
// [0, 2 pi), as GMANDisk::samplePoint and GMANSphere::samplePoint sweep it.
double turnAngle(double x, double y) {
  const auto angle = std::atan2(y, x);
  if (angle < 0.0) {
    return angle + 2.0 * std::numbers::pi;
  }
  return angle;
}

// Whether the object-space point (x, y) lies within thetamax of the +x axis
// about z.
bool withinThetaMax(RtFloat thetamax, double x, double y) {
  return turnAngle(x, y) <= turnRadians(clampedDegrees(thetamax));
}

// Whether the object-space point (x, y) of the disk's plane lies within its
// radius and thetamax, measured as GMANDisk::samplePoint measures them.
bool inSector(GMANRayDisk const& disk, double x, double y) {
  if (!(std::sqrt(x * x + y * y) <= static_cast<double>(disk.getRadius()))) {
    return false;
  }
  return withinThetaMax(disk.getThetaMax(), x, y);
}

// A point drawn uniformly in the solid angle the disk's bounding rectangle
// subtends from p, its two triangles picked by u1 in proportion to their
// solid angles as a mesh's are, kept when it maps back inside the sector.
// The density is 1 / the rectangle's solid angle. Empty for a rejected or
// non-finite draw.
std::optional<SolidAngleDraw> drawDiskBySolidAngle(GMANRayDisk const& disk, InvertibleMatrix const& objectToCamera,
                                                   DoubleVector const& p, double u1, double u2) {
  const auto corners = rectangleCorners(disk, objectToCamera);
  const auto total = rectangleSolidAngle(corners, p);
  if (!std::isfinite(total) || !(total > 0.0)) {
    return std::nullopt;
  }
  const auto triangleAt = [&corners, &p](std::size_t t) { return rectangleTriangleSolidAngle(corners, p, t); };
  const auto pick = pickByWeight(2, total, u1, triangleAt);
  if (!pick) {
    return std::nullopt;
  }
  const auto point =
      pointInSphericalTriangle(p, corners[0], corners[pick->index + 1], corners[pick->index + 2], pick->fraction, u2);
  if (!point) {
    return std::nullopt;
  }
  const auto alongX = corners[1] - corners[0];
  const auto alongY = corners[3] - corners[0];
  const auto offset = *point - corners[0];
  const auto xx = dot(alongX, alongX);
  const auto xy = dot(alongX, alongY);
  const auto yy = dot(alongY, alongY);
  const auto determinant = xx * yy - xy * xy;
  const auto s = (yy * dot(offset, alongX) - xy * dot(offset, alongY)) / determinant;
  const auto t = (xx * dot(offset, alongY) - xy * dot(offset, alongX)) / determinant;
  const auto bounds = sectorBounds(disk);
  if (!inSector(disk, bounds.xMin + s * (bounds.xMax - bounds.xMin), bounds.yMin + t * (bounds.yMax - bounds.yMin))) {
    return std::nullopt;
  }
  auto normal = gman::transformNormal(objectToCamera, GMANVector(0.0f, 0.0f, -1.0f));
  normal.normalize();
  return SolidAngleDraw{*point, normal, 1.0 / total};
}

// Whether sphere is its whole surface rather than a zone of it.
bool fullSphere(GMANRaySphere const& sphere) {
  const auto radius = sphere.getRadius();
  return sphere.getZMin() <= -radius && sphere.getZMax() >= radius && sphere.getThetaMax() >= 360.0f;
}

// How a sphere draws from a receiving point: nothing from on or inside it;
// in the cone its full sphere subtends, a full sphere at every distance and
// a zone nearer the full sphere than kZoneNearField times its radius; by area
// beyond.
enum class SphereStrategy { none, cone, area };

// The one decision sample() and lightSolidAnglePdf() share, so both answer
// the same density at the same p.
SphereStrategy sphereStrategy(GMANRaySphere const& sphere, InvertibleMatrix const& objectToCamera, GMANPoint const& p) {
  const auto radius = static_cast<double>(sphere.getRadius());
  const auto distance = length(objectPoint(objectToCamera, p));
  if (!(distance > radius)) {
    return SphereStrategy::none;
  }
  if (fullSphere(sphere) || distance - radius < kZoneNearField * radius) {
    return SphereStrategy::cone;
  }
  return SphereStrategy::area;
}

// The cone of directions from an object-space point q outside a sphere of
// radius about the origin that meet it: the unit direction from the centre
// toward q, q's distance, 1 - cos theta_max computed as sin^2 / (1 + cos),
// and the cone's solid angle 2 pi (1 - cos theta_max).
struct SphereCone {
  DoubleVector outward;
  double distance;
  double oneMinusCos;
  double solidAngle;
};

SphereCone sphereCone(DoubleVector const& q, double radius) {
  SphereCone cone;
  cone.distance = length(q);
  cone.outward = (1.0 / cone.distance) * q;
  const auto sinSquared = (radius / cone.distance) * (radius / cone.distance);
  const auto cosMax = std::sqrt((cone.distance - radius) * (cone.distance + radius)) / cone.distance;
  cone.oneMinusCos = sinSquared / (1.0 + cosMax);
  cone.solidAngle = 2.0 * std::numbers::pi * cone.oneMinusCos;
  return cone;
}

// |L w|^3 / |det L|, L the linear part of objectToCamera and w an
// object-space unit direction: the reciprocal of the solid-angle Jacobian of
// w -> L w / |L w|, 1 for a rigid or uniformly scaled placement.
double directionDensityScale(InvertibleMatrix const& objectToCamera, DoubleVector const& w) {
  auto const& m = objectToCamera.forward();
  const auto placed =
      length({w.x * m[0][0] + w.y * m[1][0] + w.z * m[2][0], w.x * m[0][1] + w.y * m[1][1] + w.z * m[2][1],
              w.x * m[0][2] + w.y * m[1][2] + w.z * m[2][2]});
  const auto determinant = m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) -
                           m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
                           m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
  return placed * placed * placed / std::fabs(static_cast<double>(determinant));
}

// The camera-space density of a direction from the object-space point q
// toward the object-space point on the sphere, drawn uniformly in cone.
double coneDensity(InvertibleMatrix const& objectToCamera, SphereCone const& cone, DoubleVector const& q,
                   DoubleVector const& onSphere) {
  return directionDensityScale(objectToCamera, normalized(onSphere - q)) / cone.solidAngle;
}

// The point where a direction drawn uniformly in cone first meets the sphere:
// cos theta = 1 - u1 (1 - cos theta_max) off the axis toward the centre and
// phi = 2 pi u2 about it. The point is placed through its angle alpha at the
// centre, cos alpha = (d sin^2 theta + cos theta sqrt(r^2 - d^2 sin^2
// theta)) / r, rather than by intersecting the ray, so a draw at the cone's
// rim lands on the sphere.
DoubleVector conePoint(SphereCone const& cone, double radius, double u1, double u2) {
  const auto oneMinusCosTheta = u1 * cone.oneMinusCos;
  const auto cosTheta = 1.0 - oneMinusCosTheta;
  const auto sinSquared = oneMinusCosTheta * (2.0 - oneMinusCosTheta);
  const auto reach = std::sqrt(nonNegative(radius * radius - cone.distance * cone.distance * sinSquared));
  const auto cosAlpha = std::clamp((cone.distance * sinSquared + cosTheta * reach) / radius, -1.0, 1.0);
  const auto sinAlpha = std::sqrt((1.0 - cosAlpha) * (1.0 + cosAlpha));
  auto const& e = cone.outward;
  const auto sign = std::copysign(1.0, e.z);
  const auto a = -1.0 / (sign + e.z);
  const auto b = e.x * e.y * a;
  const auto tangent = DoubleVector{1.0 + sign * e.x * e.x * a, sign * b, -sign * e.x};
  const auto bitangent = DoubleVector{b, sign + e.y * e.y * a, -e.y};
  const auto phi = 2.0 * std::numbers::pi * u2;
  const auto around = std::cos(phi) * tangent + std::sin(phi) * bitangent;
  return radius * (cosAlpha * e + sinAlpha * around);
}

// Whether the object-space point lies in the sphere's zone, z within [zmin,
// zmax] and its angle within thetamax, measured as GMANSphere::samplePoint
// measures them.
bool inZone(GMANRaySphere const& sphere, DoubleVector const& point) {
  if (!(point.z >= static_cast<double>(sphere.getZMin()) && point.z <= static_cast<double>(sphere.getZMax()))) {
    return false;
  }
  return withinThetaMax(sphere.getThetaMax(), point.x, point.y);
}

// A point drawn uniformly in the cone of directions from p that meet
// sphere's full sphere, kept when it lies in a zone's bounds. The density is |L w|^3 / (|det L| *
// the cone's solid angle). Empty for a rejected or non-finite draw.
std::optional<SolidAngleDraw> drawSphereByCone(GMANRaySphere const& sphere, InvertibleMatrix const& objectToCamera,
                                               GMANPoint const& p, double u1, double u2) {
  const auto radius = static_cast<double>(sphere.getRadius());
  const auto q = objectPoint(objectToCamera, p);
  const auto cone = sphereCone(q, radius);
  const auto onSphere = conePoint(cone, radius, u1, u2);
  if (!finiteVector(onSphere) || (!fullSphere(sphere) && !inZone(sphere, onSphere))) {
    return std::nullopt;
  }
  auto normal = gman::transformNormal(objectToCamera, toVector((1.0 / radius) * onSphere));
  normal.normalize();
  return SolidAngleDraw{placedPoint(objectToCamera, onSphere), normal, coneDensity(objectToCamera, cone, q, onSphere)};
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

// The draw sample() answers when it places nothing: every field zero, Cl
// black and pdf 0.
EmitterSample zeroSample() {
  EmitterSample result;
  result.wi = GMANVector(0.0f, 0.0f, 0.0f);
  result.distance = 0.0f;
  result.shadowTarget = GMANPoint(0.0f, 0.0f, 0.0f);
  result.Cl = GMANColor(0.0f, 0.0f, 0.0f);
  result.pdf = 0.0f;
  result.isDelta = false;
  return result;
}

// The sample a solid-angle draw makes from p: the direction and distance to
// the drawn point, emitter's radiance, which the draw's emitting side sends
// toward p, the draw's density, and the shadow walk's end point off the
// emitter's surface. zeroSample() when any of them is not finite.
EmitterSample solidAngleSample(Emitter const& emitter, GMANPoint const& p, SolidAngleDraw const& draw) {
  const auto toEmitter = draw.point - toDouble(p);
  const auto distance = length(toEmitter);
  const auto wi = toVector((1.0 / distance) * toEmitter);
  const auto floatDistance = static_cast<RtFloat>(distance);
  const auto pdf = static_cast<RtFloat>(draw.pdf);
  if (!std::isfinite(floatDistance) || !(floatDistance > 0.0f) || !std::isfinite(pdf) || !(pdf > 0.0f) ||
      !std::isfinite(wi.getX()) || !std::isfinite(wi.getY()) || !std::isfinite(wi.getZ())) {
    return zeroSample();
  }
  auto result = zeroSample();
  result.wi = wi;
  result.distance = floatDistance;
  result.Cl = emitter.light->getCl();
  result.pdf = pdf;
  result.shadowTarget =
      gman::offsetOrigin(toPoint(draw.point), draw.normal, -wi, gman::primitiveMagnitude(emitter.shape->getBBox()));
  return result;
}

// Draws a point on emitter's shape uniformly by area, places it, and reports
// the solid-angle pdf the area-to-solid-angle Jacobian gives, black and 0 on
// the emitter's own non-emitting side. zeroSample() for a singular placement
// or a polygon mesh with no area.
EmitterSample sampleByArea(Emitter const& emitter, GMANPoint const& p, RtFloat u1, RtFloat u2) {
  auto result = zeroSample();

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

// sample() for a disk, by the strategy diskStrategy picks at p.
EmitterSample sampleDisk(Emitter const& emitter, GMANRayDisk const& disk, GMANPoint const& p, RtFloat u1, RtFloat u2) {
  auto const& placement = disk.getObjectToCamera();
  if (!placement) {
    return zeroSample();
  }
  const auto strategy = diskStrategy(disk, *placement, p);
  if (strategy == DiskStrategy::none) {
    return zeroSample();
  }
  if (strategy == DiskStrategy::area) {
    return sampleByArea(emitter, p, u1, u2);
  }
  const auto drawn = drawDiskBySolidAngle(disk, *placement, toDouble(p), u1, u2);
  if (!drawn) {
    return zeroSample();
  }
  return solidAngleSample(emitter, p, *drawn);
}

// sample() for a sphere, by the strategy sphereStrategy picks at p.
EmitterSample sampleSphere(Emitter const& emitter, GMANRaySphere const& sphere, GMANPoint const& p, RtFloat u1,
                           RtFloat u2) {
  auto const& placement = sphere.getObjectToCamera();
  if (!placement) {
    return zeroSample();
  }
  const auto strategy = sphereStrategy(sphere, *placement, p);
  if (strategy == SphereStrategy::none) {
    return zeroSample();
  }
  if (strategy == SphereStrategy::area) {
    return sampleByArea(emitter, p, u1, u2);
  }
  const auto drawn = drawSphereByCone(sphere, *placement, p, u1, u2);
  if (!drawn) {
    return zeroSample();
  }
  return solidAngleSample(emitter, p, *drawn);
}

// The area branch of sample(): a polygon mesh draws uniformly in the solid
// angle its emitting sides subtend from p, a disk by diskStrategy and a
// sphere by sphereStrategy.
EmitterSample sampleArea(Emitter const& emitter, GMANPoint const& p, RtFloat u1, RtFloat u2) {
  const auto kind = classifyEmittingShape(*emitter.shape);
  if (kind.mesh != nullptr) {
    const auto drawn = drawMeshBySolidAngle(*kind.mesh, toDouble(p), u1, u2);
    if (!drawn) {
      return zeroSample();
    }
    return solidAngleSample(emitter, p, *drawn);
  }
  if (kind.disk != nullptr) {
    return sampleDisk(emitter, *kind.disk, p, u1, u2);
  }
  if (kind.sphere != nullptr) {
    return sampleSphere(emitter, *kind.sphere, p, u1, u2);
  }
  return sampleByArea(emitter, p, u1, u2);
}

// lightSolidAnglePdf() for a hit distance away from p at cosTheta to
// hitNormal, on emitter's shape drawn uniformly by area: d^2 / (A J cos);
// 0 for a singular placement.
RtFloat areaPdf(Emitter const& emitter, GMANNormal const& hitNormal, RtFloat distance, RtFloat cosTheta) {
  auto const& placement = emittingShapeObjectToCamera(*emitter.shape);
  if (!placement) {
    return 0.0f;
  }
  auto const& objectToCamera = *placement;

  RtFloat area = 0.0f;
  emittingShapeArea(*emitter.shape, area);

  // hitPoint's own object-space normal, recovered by the inverse of the
  // object-to-camera map placeEmitterPoint applies to a freshly drawn
  // one -- exact for the point a real draw or a real ray hit actually
  // placed, which hitPoint always is.
  auto objectNormal = gman::transformNormal(objectToCamera.inverse(), hitNormal);
  objectNormal.normalize();
  const auto jacobian = areaJacobian(objectToCamera, objectNormal);

  return (distance * distance) / (area * jacobian * cosTheta);
}

// lightSolidAnglePdf() for a disk, by the strategy diskStrategy picks at p.
RtFloat diskPdf(Emitter const& emitter, GMANRayDisk const& disk, GMANPoint const& p, GMANNormal const& hitNormal,
                RtFloat distance, RtFloat cosTheta) {
  auto const& placement = disk.getObjectToCamera();
  if (!placement) {
    return 0.0f;
  }
  const auto strategy = diskStrategy(disk, *placement, p);
  if (strategy == DiskStrategy::none) {
    return 0.0f;
  }
  if (strategy == DiskStrategy::area) {
    return areaPdf(emitter, hitNormal, distance, cosTheta);
  }
  return positiveDensity(1.0 / rectangleSolidAngle(rectangleCorners(disk, *placement), toDouble(p)));
}

// lightSolidAnglePdf() for a sphere, by the strategy sphereStrategy picks at
// p.
RtFloat spherePdf(Emitter const& emitter, GMANRaySphere const& sphere, GMANPoint const& p, GMANPoint const& hitPoint,
                  GMANNormal const& hitNormal, RtFloat distance, RtFloat cosTheta) {
  auto const& placement = sphere.getObjectToCamera();
  if (!placement) {
    return 0.0f;
  }
  const auto strategy = sphereStrategy(sphere, *placement, p);
  if (strategy == SphereStrategy::none) {
    return 0.0f;
  }
  if (strategy == SphereStrategy::area) {
    return areaPdf(emitter, hitNormal, distance, cosTheta);
  }
  const auto q = objectPoint(*placement, p);
  const auto cone = sphereCone(q, static_cast<double>(sphere.getRadius()));
  return positiveDensity(coneDensity(*placement, cone, q, objectPoint(*placement, hitPoint)));
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

std::optional<std::size_t> emitterIndex(std::vector<Emitter> const& emitters, GMANRayInterface const* hitPrimitive) {
  if (hitPrimitive == nullptr) {
    return std::nullopt;
  }
  GMANRayInterface const* const hitShape = emittingShapeOf(hitPrimitive);
  for (std::size_t j = 0; j < emitters.size(); ++j) {
    if (emitters[j].shape == hitShape) {
      return j;
    }
  }
  return std::nullopt;
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

  const auto kind = classifyEmittingShape(*emitter.shape);
  if (kind.mesh != nullptr) {
    return positiveDensity(1.0 / meshSolidAngle(*kind.mesh, toDouble(p)));
  }
  if (kind.disk != nullptr) {
    return diskPdf(emitter, *kind.disk, p, hitNormal, distance, cosTheta);
  }
  if (kind.sphere != nullptr) {
    return spherePdf(emitter, *kind.sphere, p, hitPoint, hitNormal, distance, cosTheta);
  }
  return areaPdf(emitter, hitNormal, distance, cosTheta);
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
