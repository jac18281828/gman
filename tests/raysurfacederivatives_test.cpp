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
 * GMANHit's dPdu and dPdv. Every ray primitive reports, with each hit, the
 * camera-space partial derivatives of its hit point along the u and v it
 * reports. The proof observes them: a second ray aimed at P + h*dPdu hits
 * at (u + h, v) and at P + h*dPdu to first order, and likewise for dPdv, so
 * no derivative formula is repeated here. The tangents are perpendicular to
 * the normal, zero where u is undefined or the parameterization is singular,
 * and reach a shader through gman::hitSurfacePoint and gman::shade.
 */

#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "check.h"
#include "gmancolor.h"
#include "gmanmatrix4.h"
#include "gmanparameterlist.h"
#include "gmanpoint.h"
#include "gmanpolygon.h"
#include "gmanray.h"
#include "gmanraycone.h"
#include "gmanraycylinder.h"
#include "gmanraydisk.h"
#include "gmanrayhyperboloid.h"
#include "gmanrayinterface.h"
#include "gmanrayparaboloid.h"
#include "gmanraypolygon.h"
#include "gmanraypolygonmesh.h"
#include "gmanraysphere.h"
#include "gmanraytorus.h"
#include "gmanshaderenvironment.h"
#include "gmanshading.h"
#include "gmansurfaceshader.h"
#include "gmantransform.h"
#include "gmanvector.h"
#include "maketransform.h"
#include "ri.h"

namespace {

constexpr double kStep = 1.0e-3;
constexpr double kStepTolerance = 0.05;
constexpr double kTangentTolerance = 1.0e-4;
constexpr double kThetamax = 270.0;

struct WorstError {
  double along = 0.0;
  double across = 0.0;
  double point = 0.0;
  double tangent = 0.0;
  double parallel = 0.0;
};

WorstError worst;

double lengthOf(GMANVector const& v) {
  return std::sqrt((double)v.getX() * v.getX() + (double)v.getY() * v.getY() + (double)v.getZ() * v.getZ());
}

double dotOf(GMANVector const& a, GMANVector const& b) {
  return (double)a.getX() * b.getX() + (double)a.getY() * b.getY() + (double)a.getZ() * b.getZ();
}

bool isZero(GMANVector const& v) { return v.getX() == 0.0f && v.getY() == 0.0f && v.getZ() == 0.0f; }

GMANMatrix4 placementMatrix() {
  GMANMatrix4 matrix;
  matrix.rot(0.5f, 1.0f, 2.0f, 0.5f);
  matrix.scale(1.0f, 1.4f, 0.8f);
  matrix.trans(0.2f, -0.1f, 5.0f);
  return matrix;
}

GMANTransform placement() { return makeTransform(placementMatrix()); }

GMANTransform translation() {
  GMANMatrix4 matrix;
  matrix.trans(0.0f, 0.0f, 5.0f);
  return makeTransform(matrix);
}

GMANPoint cameraPoint(RtFloat x, RtFloat y, RtFloat z) {
  return gman::transformPoint(placementMatrix(), GMANPoint(x, y, z));
}

GMANPoint const kCameraOrigin(0.0f, 0.0f, 0.0f);

GMANRay rayTo(GMANPoint const& target) { return GMANRay(kCameraOrigin, GMANVector(kCameraOrigin, target)); }

void checkStep(std::string const& name, GMANRayInterface const& primitive, GMANHit const& hit,
               GMANVector const& tangent, bool alongU) {
  std::string const label = name + (alongU ? ": dPdu" : ": dPdv");
  GMANPoint const target(hit.point.getX() + (RtFloat)(kStep * tangent.getX()),
                         hit.point.getY() + (RtFloat)(kStep * tangent.getY()),
                         hit.point.getZ() + (RtFloat)(kStep * tangent.getZ()));
  GMANHit moved;
  bool const hitMoved = primitive.intersect(rayTo(target), moved);
  check(hitMoved, label + ": a ray aimed at P + h*tangent hits");
  if (!hitMoved) {
    return;
  }

  double const deltaU = (double)moved.u - hit.u;
  double const deltaV = (double)moved.v - hit.v;
  double const along = std::fabs((alongU ? deltaU : deltaV) / kStep - 1.0);
  double const across = std::fabs((alongU ? deltaV : deltaU) / kStep);
  double const offset = lengthOf(GMANVector(target, moved.point)) / (kStep * lengthOf(tangent));

  worst.along = std::fmax(worst.along, along);
  worst.across = std::fmax(worst.across, across);
  worst.point = std::fmax(worst.point, offset);

  check(along <= kStepTolerance, label + ": the moved hit's own parameter advances by h");
  check(across <= kStepTolerance, label + ": the moved hit's other parameter holds");
  check(offset <= kStepTolerance, label + ": the moved hit lies at P + h*tangent to first order");
}

void checkTangency(std::string const& name, GMANHit const& hit) {
  GMANVector normal = hit.normal;
  normal.normalize();
  double const lengthU = lengthOf(hit.dPdu);
  double const lengthV = lengthOf(hit.dPdv);
  check(lengthU > 0.0 && lengthV > 0.0, name + ": both tangents are non-zero");

  double const tangentU = std::fabs(dotOf(hit.dPdu, normal)) / lengthU;
  double const tangentV = std::fabs(dotOf(hit.dPdv, normal)) / lengthV;
  worst.tangent = std::fmax(worst.tangent, std::fmax(tangentU, tangentV));
  check(tangentU <= kTangentTolerance, name + ": dPdu is perpendicular to the normal");
  check(tangentV <= kTangentTolerance, name + ": dPdv is perpendicular to the normal");

  GMANVector const cross = hit.dPdu.cross(hit.dPdv);
  double const alignment = std::fabs(dotOf(cross, normal)) / lengthOf(cross);
  worst.parallel = std::fmax(worst.parallel, 1.0 - alignment);
  check(1.0 - alignment <= kTangentTolerance, name + ": dPdu x dPdv is parallel to the normal");
}

void checkSurface(std::string const& name, GMANRayInterface const& primitive, GMANPoint const& aim) {
  GMANHit hit;
  bool const hitSurface = primitive.intersect(rayTo(aim), hit);
  check(hitSurface, name + ": the ray hits");
  if (!hitSurface) {
    return;
  }
  checkTangency(name, hit);
  checkStep(name, primitive, hit, hit.dPdu, true);
  checkStep(name, primitive, hit, hit.dPdv, false);
}

void checkQuadrics() {
  GMANTransform const transform = placement();
  GMANParameterList const none;

  GMANRaySphere const sphere(1.0f, -0.8f, 0.9f, kThetamax, none, transform);
  checkSurface("sphere", sphere, cameraPoint(-0.17f, 0.96f, 0.2f));

  GMANRayCylinder const cylinder(1.0f, -0.5f, 1.5f, kThetamax, none, transform);
  checkSurface("cylinder", cylinder, cameraPoint(-0.17f, 0.98f, 0.4f));

  GMANRayCone const cone(1.5f, 1.0f, kThetamax, none, transform);
  checkSurface("cone", cone, cameraPoint(-0.1f, 0.55f, 0.5f));

  GMANRayDisk const disk(0.3f, 1.0f, kThetamax, none, transform);
  checkSurface("disk", disk, cameraPoint(-0.2f, 0.6f, 0.3f));

  RtPoint spanningStart = {1.0f, 0.0f, -0.5f};
  RtPoint spanningEnd = {0.6f, 0.4f, 1.0f};
  GMANRayHyperboloid const spanning(spanningStart, spanningEnd, kThetamax, none, transform);
  checkSurface("hyperboloid spanning", spanning, cameraPoint(-0.3f, 0.85f, 0.3f));

  RtPoint annulusStart = {0.4f, 0.0f, 0.3f};
  RtPoint annulusEnd = {1.2f, 0.3f, 0.3f};
  GMANRayHyperboloid const annulus(annulusStart, annulusEnd, kThetamax, none, transform);
  checkSurface("hyperboloid annulus", annulus, cameraPoint(-0.2f, 0.7f, 0.3f));

  GMANRayParaboloid const paraboloid(2.0f, 0.5f, 1.0f, kThetamax, none, transform);
  checkSurface("paraboloid", paraboloid, cameraPoint(-0.245f, 1.39f, 0.75f));

  GMANRayTorus const torus(1.0f, 0.3f, -120.0f, 160.0f, kThetamax, none, transform);
  checkSurface("torus", torus, cameraPoint(-0.2f, 1.2f, 0.1f));
}

std::vector<GMANPoint> quadAt(GMANPoint const& corner, GMANVector const& edgeA, GMANVector const& edgeB, RtFloat size) {
  auto vertex = [&](RtFloat a, RtFloat b) {
    return GMANPoint(corner.getX() + size * (a * edgeA.getX() + b * edgeB.getX()),
                     corner.getY() + size * (a * edgeA.getY() + b * edgeB.getY()),
                     corner.getZ() + size * (a * edgeA.getZ() + b * edgeB.getZ()));
  };
  return {vertex(0.0f, 0.0f), vertex(1.0f, 0.0f), vertex(1.0f, 1.0f), vertex(0.0f, 1.0f)};
}

std::unique_ptr<GMANRayPolygon> quadWithSt(RtFloat const* p, RtFloat const* st,
                                           std::vector<GMANPoint> const& vertices) {
  RtToken tokens[] = {RI_P, RI_ST};
  RtPointer parms[] = {(RtPointer)p, (RtPointer)st};
  GMANParameterList pl(gman::standardDictionary(), 2, tokens, parms, 4, 4, 1, 1);
  return std::make_unique<GMANRayPolygon>(vertices, pl);
}

std::unique_ptr<GMANRayPolygon> quadWithoutSt(RtFloat const* p, std::vector<GMANPoint> const& vertices) {
  RtToken tokens[] = {RI_P};
  RtPointer parms[] = {(RtPointer)p};
  GMANParameterList pl(gman::standardDictionary(), 1, tokens, parms, 4, 4, 1, 1);
  return std::make_unique<GMANRayPolygon>(vertices, pl);
}

GMANVector const kEdgeA(1.0f, 0.1f, 0.3f);
GMANVector const kEdgeB(-0.1f, 1.0f, 0.2f);
GMANPoint const kQuadCorner(-1.0f, -1.0f, 5.0f);
constexpr RtFloat kQuadSize = 2.0f;

constexpr RtFloat kBentSt[] = {0.0f, 0.0f, 1.0f, 0.0f, 2.0f, 2.0f, 0.0f, 1.0f};
constexpr RtFloat kSquareP[] = {0.0f, 0.0f, 0.0f, 2.0f, 0.0f, 0.0f, 2.0f, 2.0f, 0.0f, 0.0f, 2.0f, 0.0f};

GMANPoint pointOnQuad(RtFloat a, RtFloat b) {
  return GMANPoint(kQuadCorner.getX() + kQuadSize * (a * kEdgeA.getX() + b * kEdgeB.getX()),
                   kQuadCorner.getY() + kQuadSize * (a * kEdgeA.getY() + b * kEdgeB.getY()),
                   kQuadCorner.getZ() + kQuadSize * (a * kEdgeA.getZ() + b * kEdgeB.getZ()));
}

void checkPolygons() {
  std::vector<GMANPoint> const vertices = quadAt(kQuadCorner, kEdgeA, kEdgeB, kQuadSize);
  auto const polygon = quadWithSt(kSquareP, kBentSt, vertices);
  GMANPoint const upperLeft = pointOnQuad(0.25f, 0.75f);
  GMANHit hit;
  bool const hitPolygon = polygon->intersect(rayTo(upperLeft), hit);
  check(hitPolygon, "polygon: the ray hits the second fan triangle");
  if (hitPolygon) {
    check(std::fabs(hit.u - 0.5f) < 1.0e-5 && std::fabs(hit.v - 1.0f) < 1.0e-5,
          "polygon: the hit's (s, t) is the second fan triangle's own interpolation");
  }
  checkSurface("polygon second fan triangle", *polygon, upperLeft);
  checkSurface("polygon first fan triangle", *polygon, pointOnQuad(0.75f, 0.25f));

  std::vector<std::unique_ptr<GMANRayPolygon>> faces;
  faces.emplace_back(quadWithSt(kSquareP, kBentSt, quadAt(GMANPoint(-3.0f, -1.0f, 5.0f), kEdgeA, kEdgeB, kQuadSize)));
  faces.emplace_back(quadWithSt(kSquareP, kBentSt, vertices));
  GMANRayPolygonMesh const mesh(std::move(faces));
  checkSurface("polygon mesh face", mesh, upperLeft);
}

GMANHit axisHit(std::string const& name, GMANRayInterface const& primitive) {
  GMANHit hit;
  bool const hitAxis = primitive.intersect(GMANRay(kCameraOrigin, GMANVector(0.0f, 0.0f, 1.0f)), hit);
  check(hitAxis, name + ": the axis ray hits");
  return hit;
}

void checkZero(std::string const& name, GMANHit const& hit) {
  check(isZero(hit.dPdu), name + ": dPdu is the zero vector");
  check(isZero(hit.dPdv), name + ": dPdv is the zero vector");
}

void checkUndefinedAxisPoints() {
  GMANTransform const transform = translation();
  GMANParameterList const none;

  GMANRaySphere const sphere(1.0f, -1.0f, 1.0f, kThetamax, none, transform);
  GMANHit hit = axisHit("sphere pole", sphere);
  check(hit.v == 0.0f, "sphere pole: the ray reaches the pole");
  checkZero("sphere pole", hit);

  GMANRayDisk const disk(0.0f, 1.0f, kThetamax, none, transform);
  hit = axisHit("disk centre", disk);
  check(hit.v == 0.0f, "disk centre: the ray reaches the centre");
  checkZero("disk centre", hit);

  GMANRayCone const cone(1.0f, 1.0f, kThetamax, none, transform);
  hit = axisHit("cone apex", cone);
  check(hit.v == 1.0f, "cone apex: the ray reaches the apex");
  checkZero("cone apex", hit);

  GMANRayParaboloid const paraboloid(2.0f, 0.5f, 1.0f, kThetamax, none, transform);
  hit = axisHit("paraboloid apex", paraboloid);
  check(hit.v == 0.0f, "paraboloid apex: the ray reaches the apex");
  checkZero("paraboloid apex", hit);

  std::vector<GMANPoint> const vertices =
      quadAt(kQuadCorner, GMANVector(1.0f, 0.2f, 0.5f), GMANVector(-0.1f, 1.0f, 0.1f), kQuadSize);
  RtFloat const parallelToZ[] = {0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f};
  auto const polygon = quadWithoutSt(parallelToZ, vertices);
  GMANHit polygonHit;
  bool const hitPolygon = polygon->intersect(rayTo(pointOnQuad(0.5f, 0.5f)), polygonHit);
  check(hitPolygon, "polygon without st, parallel to z: the ray hits");
  if (hitPolygon) {
    checkZero("polygon without st, parallel to z", polygonHit);
  }

  RtFloat const nearlyCollinear[] = {0.0f, 0.0f, 0.0f, 3.0f, 0.3f, 0.0f, 3.0f, 0.3f, 1.0f, 1.0f, 0.1f, 1.0f};
  auto const collapsed = quadWithoutSt(nearlyCollinear, quadAt(kQuadCorner, kEdgeA, kEdgeB, kQuadSize));
  GMANHit collapsedHit;
  bool const hitCollapsed = collapsed->intersect(rayTo(pointOnQuad(0.25f, 0.75f)), collapsedHit);
  check(hitCollapsed, "polygon without st, collapsing in float: the ray hits");
  if (hitCollapsed) {
    check(
        std::fabs(collapsedHit.u - 1.25f) < 1.0e-4 && std::fabs(collapsedHit.v - 0.125f) < 1.0e-4,
        "polygon without st, collapsing in float: the hit's (s, t) interpolates the fan triangle on the first, third and fourth vertices");
    checkZero("polygon without st, collapsing in float", collapsedHit);
  }

  GMANRayParaboloid const nearApex(2.0f, 0.5f, 1.0f, kThetamax, none, transform);
  GMANHit nearApexHit;
  RtFloat const nearApexOffset = std::sqrt(RtFloat(0.1) * std::numeric_limits<RtFloat>::epsilon());
  bool const hitNearApex = nearApex.intersect(rayTo(GMANPoint(nearApexOffset, 0.0f, 5.5f)), nearApexHit);
  check(hitNearApex, "paraboloid near its apex: the ray hits");
  if (hitNearApex) {
    check(nearApexHit.v == 0.0f && std::fabs(nearApexHit.point.getX()) > nearApexOffset / 2,
          "paraboloid near its apex: z rounds to zmin away from the axis");
    checkZero("paraboloid near its apex", nearApexHit);
  }

  RtPoint crossingStart = {-1.0f, 0.0f, 0.3f};
  RtPoint crossingEnd = {1.0f, 0.0f, 0.3f};
  GMANRayHyperboloid const crossing(crossingStart, crossingEnd, kThetamax, none, transform);
  hit = axisHit("hyperboloid annulus crossing the axis", crossing);
  check(std::fabs(hit.v - 0.5f) < 1.0e-6f, "hyperboloid annulus crossing the axis: the ray reaches v = 0.5");
  checkZero("hyperboloid annulus crossing the axis", hit);
}

class TangentShader : public GMANSurfaceShader {
public:
  GMANColor computeCi(GMANSurfaceEnv const& se) const override {
    return GMANColor(se.dPdu.getX(), se.dPdu.getY(), se.dPdu.getZ());
  }
  GMANColor computeOi(GMANSurfaceEnv const& se) const override {
    return GMANColor(se.dPdv.getX(), se.dPdv.getY(), se.dPdv.getZ());
  }
};

void checkShaderSeesTangents() {
  GMANRaySphere const sphere(1.0f, -0.8f, 0.9f, kThetamax, GMANParameterList(), placement());
  GMANRay const ray = rayTo(cameraPoint(-0.17f, 0.96f, 0.2f));
  GMANHit hit;
  bool const hitSphere = sphere.intersect(ray, hit);
  check(hitSphere, "shader: the ray hits the sphere");
  if (!hitSphere) {
    return;
  }

  TangentShader const shader;
  gman::Appearance appearance;
  appearance.shader = std::shared_ptr<GMANSurfaceShader const>(&shader, [](GMANSurfaceShader const*) {});
  gman::SurfacePoint const point = gman::hitSurfacePoint(ray, hit);
  gman::Shading const shading = gman::shade(appearance, point, GMANMatrix4());

  check(!isZero(hit.dPdu) && !isZero(hit.dPdv), "shader: the hit carries non-zero tangents");
  check(shading.Ci.getRed() == hit.dPdu.getX() && shading.Ci.getGreen() == hit.dPdu.getY() &&
            shading.Ci.getBlue() == hit.dPdu.getZ(),
        "shader: dPdu through gman::shade equals the hit's dPdu");
  check(shading.Oi.getRed() == hit.dPdv.getX() && shading.Oi.getGreen() == hit.dPdv.getY() &&
            shading.Oi.getBlue() == hit.dPdv.getZ(),
        "shader: dPdv through gman::shade equals the hit's dPdv");
}

} // namespace

int main() {
  checkQuadrics();
  checkPolygons();
  checkUndefinedAxisPoints();
  checkShaderSeesTangents();

  std::printf("worst error: along %.3g, across %.3g, point %.3g, tangent %.3g, parallel %.3g\n", worst.along,
              worst.across, worst.point, worst.tangent, worst.parallel);
  return checkSummary("every ray primitive's dPdu and dPdv are the partial derivatives of its hit point along "
                      "its reported u and v, tangent to the surface, zero where u is undefined, and reach a "
                      "shader through gman::shade");
}
