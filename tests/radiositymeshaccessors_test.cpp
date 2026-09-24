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
 * GMANRadiosityMesh's element accessors: getElementPrimitive against the
 * face a GMANRayBVH hit reports, surfacePoint against a triangle's outer
 * loop, a holed polygon's hole and a sphere's analytic area density, and
 * sameNode across a sphere's seam and poles, two coincident spheres and
 * two faces of one mesh.
 */

#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "check.h"
#include "gmanlinearworldmanager.h"
#include "gmanmath.h"
#include "gmanmatrix4.h"
#include "gmanparameterlist.h"
#include "gmanpoint.h"
#include "gmanradiositymesh.h"
#include "gmanray.h"
#include "gmanraybvh.h"
#include "gmanraypolygon.h"
#include "gmanraypolygonmesh.h"
#include "gmanraysphere.h"
#include "gmantransform.h"
#include "gmanvector.h"

namespace {

// A sphere's density against R^2 (2 pi / nu)(pi / nv) cos(phi): the
// accessor's own central difference is accurate to about 1e-4 there.
constexpr double kDensityRelTolerance = 1e-3;

// A polygon's in-plane point against the bilinear blend of its cell's
// corner nodes, and its density against the cell's corner-node area:
// exact geometry at unit scale, float rounding only.
constexpr double kPlanarTolerance = 1e-5;

// A sphere's surface point lies on the sphere, and its normal is radial,
// up to float rounding through the placement; a polygon's normal is its
// plane normal to the same rounding.
constexpr double kOnSphereRelTolerance = 1e-5;
constexpr double kRadialDotTolerance = 1e-5;

// Samples per cell axis for the containment sweeps: enough to land on
// both sides of any clipped cell's boundary.
constexpr int kSweepSamples = 8;

double pointDistance(GMANPoint const& a, GMANPoint const& b) {
  double const dx = (double)b.getX() - (double)a.getX();
  double const dy = (double)b.getY() - (double)a.getY();
  double const dz = (double)b.getZ() - (double)a.getZ();
  return std::sqrt(dx * dx + dy * dy + dz * dz);
}

GMANTransform makeTransform(GMANMatrix4 matrix) {
  GMANOneMatrix storage(matrix);
  return GMANTransform(storage);
}

// The point at (s, t) across element's own cell, from its corner nodes
// alone: a polygon cell is a parallelogram, so the bilinear blend is the
// in-plane point.
GMANPoint cellPoint(GMANRadiosityMesh const& mesh, std::size_t element, double s, double t) {
  GMANRadiosityElement const& e = mesh.getElement(element);
  GMANPoint const& p0 = mesh.getNode(e.corners[0]).position;
  GMANPoint const& p1 = mesh.getNode(e.corners[1]).position;
  GMANPoint const& p3 = mesh.getNode(e.corners[3]).position;
  GMANVector const alongS(p0, p1);
  GMANVector const alongT(p0, p3);
  return p0 + alongS * (RtFloat)s + alongT * (RtFloat)t;
}

double cellArea(GMANRadiosityMesh const& mesh, std::size_t element) {
  GMANRadiosityElement const& e = mesh.getElement(element);
  GMANPoint const& p0 = mesh.getNode(e.corners[0]).position;
  GMANVector const alongS(p0, mesh.getNode(e.corners[1]).position);
  GMANVector const alongT(p0, mesh.getNode(e.corners[3]).position);
  GMANVector const areaVector = alongS.cross(alongT);
  return std::sqrt((double)areaVector.dot(areaVector));
}

// Even-odd containment of p in a z = 0 loop, independent of the mesh's
// own in-plane basis.
bool insideLoopXY(std::vector<GMANPoint> const& loop, GMANPoint const& p) {
  bool inside = false;
  std::size_t const n = loop.size();
  for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
    double const xi = loop[i].getX(), yi = loop[i].getY();
    double const xj = loop[j].getX(), yj = loop[j].getY();
    if ((yi > p.getY()) != (yj > p.getY())) {
      double const xCross = xi + (xj - xi) * ((double)p.getY() - yi) / (yj - yi);
      if ((double)p.getX() < xCross) {
        inside = !inside;
      }
    }
  }
  return inside;
}

// Sweeps every element of a z = 0 polygon: surfacePoint answers true
// exactly where the independent containment test does, and there its
// point, normal and density match the cell's own geometry.
void checkPlanarContainment(char const* name, std::vector<GMANPoint> const& outer,
                            std::vector<std::vector<GMANPoint>> const& holes) {
  GMANLinearWorldManager worldManager;
  worldManager.add(new GMANRayPolygon(outer, holes, GMANParameterList()));
  GMANRadiosityMesh mesh;
  mesh.build(worldManager, 0.3f);

  int onSurface = 0;
  int offSurface = 0;
  int disagreements = 0;
  double worstPoint = 0;
  double worstDensity = 0;
  bool normalsAgree = true;
  for (std::size_t e = 0; e < mesh.getElementCount(); ++e) {
    double const area = cellArea(mesh, e);
    for (int a = 0; a < kSweepSamples; ++a) {
      for (int b = 0; b < kSweepSamples; ++b) {
        double const s = ((double)a + 0.5) / kSweepSamples;
        double const t = ((double)b + 0.5) / kSweepSamples;
        GMANPoint const expected = cellPoint(mesh, e, s, t);
        bool expectedInside = insideLoopXY(outer, expected);
        for (std::vector<GMANPoint> const& hole : holes) {
          expectedInside = expectedInside && !insideLoopXY(hole, expected);
        }

        GMANRadiositySurfacePoint point;
        bool const found = mesh.surfacePoint(e, s, t, point);
        if (found != expectedInside) {
          ++disagreements;
        }
        if (!found) {
          ++offSurface;
          continue;
        }
        ++onSurface;
        worstPoint = GMANMax(worstPoint, pointDistance(point.P, expected));
        worstDensity = GMANMax(worstDensity, std::fabs((double)point.density - area) / area);
        if (1.0 - (double)point.N.dot(mesh.getElement(e).normal) > kRadialDotTolerance) {
          normalsAgree = false;
        }
      }
    }
  }

  std::string const label(name);
  std::printf("surfacePoint: %s elements=%zu on=%d off=%d disagreements=%d worstPoint=%.3g worstDensity=%.3g\n", name,
              mesh.getElementCount(), onSurface, offSurface, disagreements, worstPoint, worstDensity);
  check(onSurface > 0 && offSurface > 0, label + ": the sweep lands both on and off the surface");
  check(disagreements == 0, label + ": surfacePoint is true exactly inside the outer loop and outside every hole");
  check(worstPoint < kPlanarTolerance, label + ": the surface point is the cell's own in-plane point");
  check(worstDensity < kPlanarTolerance, label + ": the density is the full cell's area");
  check(normalsAgree, label + ": the normal is the element's own plane normal");
}

void testTriangleOuterLoop() {
  std::vector<GMANPoint> const triangle = {GMANPoint(0, 0, 0), GMANPoint(2, 0, 0), GMANPoint(0, 1.7f, 0)};
  checkPlanarContainment("triangle", triangle, {});
}

void testHoledPolygon() {
  std::vector<GMANPoint> const outer = {GMANPoint(0, 0, 0), GMANPoint(2, 0, 0), GMANPoint(2, 2, 0), GMANPoint(0, 2, 0)};
  // Off the grid's own lines, so cells straddle each hole edge.
  std::vector<GMANPoint> const hole = {GMANPoint(0.55f, 0.65f, 0), GMANPoint(1.35f, 0.65f, 0),
                                       GMANPoint(1.35f, 1.4f, 0), GMANPoint(0.55f, 1.4f, 0)};
  checkPlanarContainment("holed polygon", outer, {hole});
}

// A full sphere: nu from element 0's corners, which follow the grid's own
// row-major node order.
struct SphereGrid {
  std::size_t nu = 0;
  std::size_t nv = 0;
};

SphereGrid sphereGrid(GMANRadiosityMesh const& mesh) {
  SphereGrid grid;
  GMANRadiosityElement const& first = mesh.getElement(0);
  grid.nu = first.corners[3] - first.corners[0] - 1;
  grid.nv = mesh.getElementCount() / grid.nu;
  return grid;
}

std::size_t gridNode(SphereGrid const& grid, std::size_t i, std::size_t j) { return j * (grid.nu + 1) + i; }

void testSphereDensity() {
  double const radius = 1.5;
  GMANPoint const centre(0.3f, -0.2f, 5.0f);
  // Rotate, then translate: p * M applies concat's left operand first.
  GMANMatrix4 placement;
  placement.rot(GMANRadians(31.0), 0.2, 1.0, 0.4);
  GMANMatrix4 move;
  move.trans(centre.getX(), centre.getY(), centre.getZ());
  placement.concat(move);

  GMANLinearWorldManager worldManager;
  worldManager.add(new GMANRaySphere((RtFloat)radius, (RtFloat)-radius, (RtFloat)radius, 360.0f, GMANParameterList(),
                                     makeTransform(placement)));
  GMANRadiosityMesh mesh;
  mesh.build(worldManager, 0.35f);
  SphereGrid const grid = sphereGrid(mesh);

  double worstDensity = 0;
  double worstRadius = 0;
  double worstRadial = 0;
  double deepestFacetCentre = 0;
  int checked = 0;
  double const samples[][2] = {{0.5, 0.5}, {0.125, 0.875}, {0.8, 0.3}};
  for (std::size_t j = 1; j + 1 < grid.nv; ++j) { // away from both pole rows
    for (std::size_t i = 0; i < grid.nu; ++i) {
      std::size_t const e = j * grid.nu + i;
      for (auto const& st : samples) {
        GMANRadiositySurfacePoint point;
        if (!mesh.surfacePoint(e, st[0], st[1], point)) {
          worstDensity = 1;
          continue;
        }
        double const phi = -PI / 2 + PI * ((double)j + st[1]) / (double)grid.nv;
        double const analytic = radius * radius * (2 * PI / (double)grid.nu) * (PI / (double)grid.nv) * std::cos(phi);
        worstDensity = GMANMax(worstDensity, std::fabs((double)point.density - analytic) / analytic);
        worstRadius = GMANMax(worstRadius, std::fabs(pointDistance(point.P, centre) - radius) / radius);

        GMANVector radial(centre, point.P);
        radial.normalize();
        worstRadial = GMANMax(worstRadial, 1.0 - (double)radial.dot(point.N));
        ++checked;
      }
      deepestFacetCentre =
          GMANMax(deepestFacetCentre, (radius - pointDistance(mesh.getElement(e).centre, centre)) / radius);
    }
  }

  std::printf("surfacePoint: sphere nu=%zu nv=%zu checked=%d worstDensity=%.3g worstRadius=%.3g worstRadial=%.3g "
              "deepestFacetCentre=%.3g\n",
              grid.nu, grid.nv, checked, worstDensity, worstRadius, worstRadial, deepestFacetCentre);
  check(checked > 0, "sphere: density samples were taken away from the poles");
  check(worstDensity < kDensityRelTolerance, "sphere: density matches R^2 (2pi/nu)(pi/nv) cos(phi) within 1e-3");
  check(worstRadius < kOnSphereRelTolerance, "sphere: the surface point lies on the sphere");
  check(deepestFacetCentre > 100 * kOnSphereRelTolerance,
        "sphere: facet centres lie measurably inside, so the on-sphere check can fail");
  check(worstRadial < kRadialDotTolerance, "sphere: the normal is the outward radial direction");
}

void testSphereSameNode() {
  GMANMatrix4 placement;
  placement.trans(0.4f, 0.1f, 5.0f);
  GMANLinearWorldManager worldManager;
  worldManager.add(new GMANRaySphere(1.2f, -1.2f, 1.2f, 360.0f, GMANParameterList(), makeTransform(placement)));
  // A second primitive at exactly the same place: every node coincides
  // with one of the first sphere's.
  worldManager.add(new GMANRaySphere(1.2f, -1.2f, 1.2f, 360.0f, GMANParameterList(), makeTransform(placement)));
  GMANRadiosityMesh mesh;
  mesh.build(worldManager, 0.4f);

  std::size_t const perSphere = mesh.getNodeCount() / 2;
  // Element 0 belongs to the first sphere; its own grid is the first half.
  SphereGrid grid;
  GMANRadiosityElement const& first = mesh.getElement(0);
  grid.nu = first.corners[3] - first.corners[0] - 1;
  grid.nv = (mesh.getElementCount() / 2) / grid.nu;

  bool seamJoined = true;
  for (std::size_t j = 0; j <= grid.nv; ++j) {
    seamJoined = seamJoined && mesh.sameNode(gridNode(grid, 0, j), gridNode(grid, grid.nu, j));
  }
  bool polesJoined = true;
  for (std::size_t i = 1; i <= grid.nu; ++i) {
    polesJoined = polesJoined && mesh.sameNode(gridNode(grid, 0, 0), gridNode(grid, i, 0));
    polesJoined = polesJoined && mesh.sameNode(gridNode(grid, 0, grid.nv), gridNode(grid, i, grid.nv));
  }
  bool interiorApart = true;
  for (std::size_t j = 1; j < grid.nv; ++j) {
    for (std::size_t i = 1; i < grid.nu; ++i) {
      interiorApart = interiorApart && !mesh.sameNode(gridNode(grid, i, j), gridNode(grid, i - 1, j));
      interiorApart = interiorApart && !mesh.sameNode(gridNode(grid, i, j), gridNode(grid, i, j - 1));
    }
  }
  interiorApart = interiorApart && !mesh.sameNode(gridNode(grid, 0, 0), gridNode(grid, 0, grid.nv));

  bool primitivesApart = true;
  std::size_t coincidentPairs = 0;
  for (std::size_t n = 0; n < perSphere; ++n) {
    std::size_t const twin = perSphere + n;
    if (pointDistance(mesh.getNode(n).position, mesh.getNode(twin).position) == 0.0) {
      ++coincidentPairs;
    }
    primitivesApart = primitivesApart && !mesh.sameNode(n, twin);
  }

  std::printf("sameNode: sphere nu=%zu nv=%zu nodes=%zu coincidentAcrossPrimitives=%zu\n", grid.nu, grid.nv,
              mesh.getNodeCount(), coincidentPairs);
  check(seamJoined, "sphere: every u = 0 node is the same node as its u = 1 twin");
  check(polesJoined, "sphere: every node of each pole row is one node");
  check(interiorApart, "sphere: neighbouring grid nodes, and the two poles, stay apart");
  check(coincidentPairs == perSphere, "two spheres: every node coincides exactly with its twin's position");
  check(primitivesApart, "two spheres: coincident nodes of two primitives are never the same node");
}

std::unique_ptr<GMANRayPolygon> face(std::vector<GMANPoint> outer) {
  return std::make_unique<GMANRayPolygon>(std::move(outer), GMANParameterList());
}

// An L of two unit faces of one mesh, sharing the edge x = 0, z = 5.
void testMeshFaces() {
  std::vector<std::unique_ptr<GMANRayPolygon>> faces;
  faces.push_back(face({GMANPoint(0, 0, 5), GMANPoint(1, 0, 5), GMANPoint(1, 1, 5), GMANPoint(0, 1, 5)}));
  faces.push_back(face({GMANPoint(0, 0, 5), GMANPoint(0, 1, 5), GMANPoint(0, 1, 6), GMANPoint(0, 0, 6)}));
  auto* polygonMesh = new GMANRayPolygonMesh(std::move(faces));

  GMANLinearWorldManager worldManager;
  worldManager.add(polygonMesh);
  GMANRadiosityMesh mesh;
  mesh.build(worldManager, 0.25f);
  GMANRayBVH bvh;
  bvh.build(worldManager);

  bool everyFaceHit = true;
  bool matchesBvh = true;
  for (std::size_t e = 0; e < mesh.getElementCount(); ++e) {
    GMANRadiosityElement const& element = mesh.getElement(e);
    GMANPoint const origin = element.centre + element.normal * 0.5f;
    GMANRay const ray(origin, -element.normal);
    GMANHit hit;
    GMANRayInterface const* hitPrimitive = nullptr;
    if (!bvh.nearestHit(ray, hit, hitPrimitive)) {
      everyFaceHit = false;
      continue;
    }
    GMANRayInterface const* primitive = mesh.getElementPrimitive(e);
    matchesBvh = matchesBvh && primitive == hitPrimitive;
    matchesBvh = matchesBvh && (primitive == &polygonMesh->getFace(0) || primitive == &polygonMesh->getFace(1));
  }
  check(everyFaceHit, "mesh faces: a ray at each element's centre hits the mesh");
  check(matchesBvh, "mesh faces: getElementPrimitive is the face a GMANRayBVH hit reports");

  std::size_t sharedPairs = 0;
  bool facesApart = true;
  for (std::size_t a = 0; a < mesh.getNodeCount(); ++a) {
    for (std::size_t b = a + 1; b < mesh.getNodeCount(); ++b) {
      if (pointDistance(mesh.getNode(a).position, mesh.getNode(b).position) > 1e-6) {
        continue;
      }
      ++sharedPairs;
      facesApart = facesApart && !mesh.sameNode(a, b);
    }
  }
  std::printf("sameNode: mesh faces nodes=%zu coincidentPairs=%zu\n", mesh.getNodeCount(), sharedPairs);
  check(sharedPairs > 0, "mesh faces: the shared edge's nodes coincide across the two faces");
  check(facesApart, "mesh faces: nodes of two faces of one mesh are never the same node");
}

void testSpherePrimitive() {
  GMANLinearWorldManager worldManager;
  auto* sphere = new GMANRaySphere(1.0f, -1.0f, 1.0f, 360.0f, GMANParameterList());
  worldManager.add(sphere);
  worldManager.add(new GMANRayPolygon({GMANPoint(-2, -2, -1.5f), GMANPoint(2, -2, -1.5f), GMANPoint(2, 2, -1.5f)},
                                      GMANParameterList()));
  GMANRadiosityMesh mesh;
  mesh.build(worldManager, 0.5f);

  std::size_t onSphere = 0;
  bool consistent = true;
  for (std::size_t e = 0; e < mesh.getElementCount(); ++e) {
    GMANRayInterface const* primitive = mesh.getElementPrimitive(e);
    bool const nearSphere = std::fabs(pointDistance(mesh.getElement(e).centre, GMANPoint(0, 0, 0)) - 1.0) < 0.1;
    if (primitive == sphere) {
      ++onSphere;
    }
    consistent = consistent && ((primitive == sphere) == nearSphere);
  }
  check(onSphere > 0 && onSphere < mesh.getElementCount(), "two primitives: elements come from both");
  check(consistent, "two primitives: getElementPrimitive names the primitive each element lies on");
}

} // namespace

int main() {
  testTriangleOuterLoop();
  testHoledPolygon();
  testSphereDensity();
  testSphereSameNode();
  testMeshFaces();
  testSpherePrimitive();

  return checkSummary("GMANRadiosityMesh's element primitive, surface point and node groups hold");
}
