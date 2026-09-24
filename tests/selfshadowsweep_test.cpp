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
 * R9 proof, §8 A: every ray primitive, at every scale and placement this
 * tree cares about, never shadows itself. Each cell places one primitive
 * alone in its own BVH, samples it with real GMANRayInterface::intersect
 * hits (never a hand-placed point), and calls GMANRayOccluder::transmission
 * toward a light on its own outward side. A sample where a shadow ray
 * re-cast from well off the surface still hits the same primitive is
 * excluded as legitimate self-occlusion (the torus's own tube, a saddle on
 * the skewed hyperboloid) rather than a numerical self-hit; every other
 * lit sample must transmit white. §8 A's own bisection (a separate,
 * uncommitted driver over these same cells) is what sets kSelfShadowOffsetScale;
 * this file only proves the shipped constant clears every cell.
 */

#include <cmath>
#include <string>
#include <vector>

#include "check.h"
#include "gmanlightsourcemgr.h"
#include "gmanlinearworldmanager.h"
#include "gmanmath.h"
#include "gmanmatrix4.h"
#include "gmanparameterlist.h"
#include "gmanpoint.h"
#include "gmanray.h"
#include "gmanraycone.h"
#include "gmanraycylinder.h"
#include "gmanraydisk.h"
#include "gmanrayhyperboloid.h"
#include "gmanrayparaboloid.h"
#include "gmanraypolygon.h"
#include "gmanraysphere.h"
#include "gmanraytorus.h"
#include "gmanraytracerenderer.h"
#include "gmanvector.h"
#include "ri.h"

namespace {

GMANTransform makeTransform(GMANMatrix4 matrix) {
  GMANOneMatrix storage(matrix);
  return GMANTransform(storage);
}

// translated 3x the scale, no rotation or shear: the plain placement §1's
// sweep contrasts against skewedPlacement below.
GMANMatrix4 translatedPlacement(RtFloat scale) {
  GMANMatrix4 m;
  m.trans(3.0 * scale, 0.0, 0.0);
  return m;
}

// The same translation, behind a rotation off every axis and the
// non-uniform scale (1, 2, 0.5) -- the sweep's skewed placement, §1's own
// condition-number-4 case.
GMANMatrix4 skewedPlacement(RtFloat scale) {
  GMANMatrix4 m;
  m.trans(3.0 * scale, 0.0, 0.0);
  GMANMatrix4 rx;
  rx.rot(GMANRadians(25.0), 1.0, 0.0, 0.0);
  m.concat(rx);
  GMANMatrix4 ry;
  ry.rot(GMANRadians(35.0), 0.0, 1.0, 0.0);
  m.concat(ry);
  GMANMatrix4 rz;
  rz.rot(GMANRadians(15.0), 0.0, 0.0, 1.0);
  m.concat(rz);
  GMANMatrix4 s;
  s.scale(1.0, 2.0, 0.5);
  m.concat(s);
  return m;
}

// The object-space origin's own camera-space image under a placement built
// by translatedPlacement/skewedPlacement above: rotation and scale fix the
// origin, so only the translation row survives -- the sweep's own ray
// origins circle this point, not the world origin.
GMANPoint placementCenter(GMANMatrix4 const& placement) {
  return gman::transformPoint(placement, GMANPoint(0.0, 0.0, 0.0));
}

// The largest absolute coordinate over box's own six corners, 0 for a box
// still at its default +/-RI_INFINITY -- mirrors gmanraytracerenderer.cpp's
// own primitiveMagnitude, duplicated here since that one is file-local.
RtFloat boxMagnitude(GMANBBox const& box) {
  GMANPoint const boxMin = box.getMin();
  GMANPoint const boxMax = box.getMax();
  RtFloat const coords[6] = {boxMin.getX(), boxMin.getY(), boxMin.getZ(), boxMax.getX(), boxMax.getY(), boxMax.getZ()};
  RtFloat magnitude = 0.0;
  for (RtFloat const coord : coords) {
    if ((RtFloat)std::fabs(coord) >= RI_INFINITY) {
      return 0.0;
    }
    magnitude = GMANMax(magnitude, (RtFloat)std::fabs(coord));
  }
  return magnitude;
}

// One sweep cell's own fixture: a single primitive, alone in its own BVH,
// its own outward light direction (object space, transformed by the same
// placement the primitive was built with) and the placement's own centre,
// where the sweep's ray origins circle.
struct Cell {
  std::string name;
  GMANRayInterface* primitive; // owned by worldManager below
  GMANLinearWorldManager worldManager;
  GMANRayBVH bvh;
  GMANVector towardLight; // camera space, unit length
  GMANPoint center;
  RtFloat scale;

  Cell(std::string cellName, GMANRayInterface* prim, GMANVector const& objectTowardLight, GMANMatrix4 const& placement,
       RtFloat cellScale)
      : name(std::move(cellName)), primitive(prim), scale(cellScale) {
    // A bare GMANRayInterface's appearance defaults to black (fully
    // transparent, rayoccluder_test.cpp's own sphereAt comment): opaque
    // white here so a real self-hit actually attenuates transmission
    // rather than passing through unnoticed.
    gman::Appearance opaque;
    opaque.Os = GMANColor(1.0f, 1.0f, 1.0f);
    primitive->setAppearance(opaque);
    worldManager.add(primitive);
    bvh.build(worldManager);
    towardLight = gman::transformDirection(placement, objectTowardLight);
    towardLight.normalize();
    center = placementCenter(placement);
  }
};

GMANRaySphere* buildSphere(RtFloat scale, GMANMatrix4 const& placement) {
  return new GMANRaySphere(scale, -scale, scale, 360.0f, GMANParameterList(), makeTransform(placement));
}

GMANRayCylinder* buildCylinder(RtFloat scale, GMANMatrix4 const& placement) {
  return new GMANRayCylinder(scale, -scale, scale, 360.0f, GMANParameterList(), makeTransform(placement));
}

GMANRayCone* buildCone(RtFloat scale, GMANMatrix4 const& placement) {
  return new GMANRayCone(2.0f * scale, scale, 360.0f, GMANParameterList(), makeTransform(placement));
}

// GMANParaboloid::getLocation's own radius at v == 1 (z == zmax) is
// rmax/sqrt(zmax), not rmax itself -- rmax == scale*sqrt(scale) here keeps
// that radius, and so the whole shape's own extent, linear in scale the
// way every other primitive's is.
GMANRayParaboloid* buildParaboloid(RtFloat scale, GMANMatrix4 const& placement) {
  RtFloat const rmax = scale * (RtFloat)std::sqrt((double)scale);
  return new GMANRayParaboloid(rmax, 0.0f, scale, 360.0f, GMANParameterList(), makeTransform(placement));
}

// A spanning (non-flat-annulus) hyperboloid: point1 and point2 differ in
// both radius and z, so the swept surface spans a genuine one-sheet
// hyperboloid rather than the flat-annulus special case.
GMANRayHyperboloid* buildHyperboloid(RtFloat scale, GMANMatrix4 const& placement) {
  RtPoint point1 = {0.5f * scale, 0.0f, -1.0f * scale};
  RtPoint point2 = {1.2f * scale, 0.0f, 1.0f * scale};
  return new GMANRayHyperboloid(point1, point2, 360.0f, GMANParameterList(), makeTransform(placement));
}

GMANRayDisk* buildDisk(RtFloat scale, GMANMatrix4 const& placement) {
  return new GMANRayDisk(0.0f, scale, 360.0f, GMANParameterList(), makeTransform(placement));
}

GMANRayTorus* buildTorus(RtFloat scale, GMANMatrix4 const& placement) {
  return new GMANRayTorus(scale, 0.3f * scale, -180.0f, 180.0f, 360.0f, GMANParameterList(), makeTransform(placement));
}

// Camera space already: transforms a canonical, CCW-from-+z square through
// placement by hand, the way every other ray primitive's own transform
// argument would.
GMANRayPolygon* buildPolygon(RtFloat scale, GMANMatrix4 const& placement) {
  std::vector<GMANPoint> const objectVerts = {
      GMANPoint(-scale, -scale, 0.0f),
      GMANPoint(scale, -scale, 0.0f),
      GMANPoint(scale, scale, 0.0f),
      GMANPoint(-scale, scale, 0.0f),
  };
  std::vector<GMANPoint> verts;
  verts.reserve(objectVerts.size());
  for (GMANPoint const& v : objectVerts) {
    verts.push_back(gman::transformPoint(placement, v));
  }
  return new GMANRayPolygon(verts, GMANParameterList());
}

// Off-axis in every coordinate, so a revolution primitive's radial and
// axial normals both see plenty of lit samples -- the same direction
// rayoccluder_test.cpp's own testSelfShadowAtScale uses.
GMANVector const kGenericTowardLight(1.0f, 0.6f, 0.3f);

struct PrimitiveKind {
  char const* name;
  GMANRayInterface* (*build)(RtFloat, GMANMatrix4 const&);
  GMANVector towardLight;
};

// The disk's own object-space normal is (0,0,-1) (GMANDisk's own
// convention); lighting it from -z is the only placement that ever lights
// its face. Every other kind uses the generic off-axis direction.
PrimitiveKind const kPrimitiveKinds[] = {
    {"sphere", [](RtFloat s, GMANMatrix4 const& p) -> GMANRayInterface* { return buildSphere(s, p); },
     kGenericTowardLight},
    {"cylinder", [](RtFloat s, GMANMatrix4 const& p) -> GMANRayInterface* { return buildCylinder(s, p); },
     kGenericTowardLight},
    {"cone", [](RtFloat s, GMANMatrix4 const& p) -> GMANRayInterface* { return buildCone(s, p); }, kGenericTowardLight},
    {"paraboloid", [](RtFloat s, GMANMatrix4 const& p) -> GMANRayInterface* { return buildParaboloid(s, p); },
     kGenericTowardLight},
    {"hyperboloid", [](RtFloat s, GMANMatrix4 const& p) -> GMANRayInterface* { return buildHyperboloid(s, p); },
     kGenericTowardLight},
    {"disk", [](RtFloat s, GMANMatrix4 const& p) -> GMANRayInterface* { return buildDisk(s, p); },
     GMANVector(0.0f, 0.0f, -1.0f)},
    {"torus", [](RtFloat s, GMANMatrix4 const& p) -> GMANRayInterface* { return buildTorus(s, p); },
     kGenericTowardLight},
    {"polygon", [](RtFloat s, GMANMatrix4 const& p) -> GMANRayInterface* { return buildPolygon(s, p); },
     GMANVector(0.0f, 0.0f, 1.0f)},
};

// hitPoint offset by 1e-3 * scale along Ng, oriented toward reference --
// far past any self-shadow offset this constant could ever reach, so a
// second hit found from here is a genuine second surface, not a numerical
// self-hit. Mirrors offsetOrigin's own orientation rule
// (gmanraytracerenderer.cpp) at a fixed, much larger bias.
GMANPoint probeOffset(GMANPoint const& hitPoint, GMANVector const& Ng, GMANVector const& reference, RtFloat scale) {
  RtFloat const bias = (RtFloat)1.0e-3 * scale;
  RtFloat const signedBias = (Ng.dot(reference) < 0.0f) ? -bias : bias;
  return GMANPoint(hitPoint.getX() + Ng.getX() * signedBias, hitPoint.getY() + Ng.getY() * signedBias,
                   hitPoint.getZ() + Ng.getZ() * signedBias);
}

// One cell's own sweep: samples a (theta, phi) grid of ray origins on a
// sphere of radius 5*scale around the cell's own centre, keeps every lit
// hit (N.towardLight > 0), excludes legitimate self-occlusion (probeOffset
// above still hits the same primitive) and asserts every remaining sample
// transmits white toward the light.
void sweepCell(Cell& cell) {
  GMANRayOccluder const occluder(cell.bvh);
  GMANVector lightToScene(-cell.towardLight.getX(), -cell.towardLight.getY(), -cell.towardLight.getZ());
  GMANLight const light(GMAN_LIGHT_DISTANT, GMANColor(1.0f, 1.0f, 1.0f), GMANPoint(), lightToScene);

  constexpr int kThetaSteps = 48;
  constexpr int kPhiSteps = 24;
  RtFloat const radius = 5.0f * cell.scale;

  int litSamples = 0;
  int excluded = 0;
  int selfShadowed = 0;

  for (int ti = 0; ti < kThetaSteps; ++ti) {
    RtFloat const theta = (RtFloat)ti / (RtFloat)kThetaSteps * 2.0f * (RtFloat)PI;
    for (int pi = 1; pi < kPhiSteps; ++pi) {
      RtFloat const phi = (RtFloat)pi / (RtFloat)kPhiSteps * (RtFloat)PI;
      GMANVector const dir((RtFloat)(std::sin(phi) * std::cos(theta)), (RtFloat)(std::sin(phi) * std::sin(theta)),
                           (RtFloat)std::cos(phi));
      GMANPoint const origin(cell.center.getX() + dir.getX() * radius, cell.center.getY() + dir.getY() * radius,
                             cell.center.getZ() + dir.getZ() * radius);
      GMANVector const direction(origin, cell.center);
      GMANRay const ray(origin, direction);
      GMANHit hit;
      if (!cell.primitive->intersect(ray, hit)) {
        continue;
      }
      RtFloat const nDotL = hit.normal.dot(cell.towardLight);
      if (nDotL <= 0.0f) {
        continue;
      }
      ++litSamples;

      GMANPoint const probeOrigin = probeOffset(hit.point, hit.normal, cell.towardLight, cell.scale);
      GMANRay const probeRay(probeOrigin, cell.towardLight, RI_EPSILON, RI_INFINITY);
      GMANHit probeHit;
      if (cell.primitive->intersect(probeRay, probeHit)) {
        ++excluded;
        continue;
      }

      RtFloat const magnitude = boxMagnitude(cell.primitive->getBBox());
      GMANColor const result =
          occluder.transmission(light, hit.point, cell.towardLight, hit.normal, RI_INFINITY, magnitude);
      if (result.getRed() < 0.99f || result.getGreen() < 0.99f || result.getBlue() < 0.99f) {
        ++selfShadowed;
      }
    }
  }

  int const remaining = litSamples - excluded;
  constexpr int kMinRemaining = 40;
  check(remaining >= kMinRemaining, cell.name +
                                        ": enough non-excluded lit samples were gathered to trust a zero "
                                        "count (" +
                                        std::to_string(remaining) + "/" + std::to_string(litSamples) + " lit, " +
                                        std::to_string(excluded) + " excluded)");
  check(selfShadowed == 0, cell.name + ": every non-excluded lit sample transmits white toward the light (" +
                               std::to_string(selfShadowed) + "/" + std::to_string(remaining) + " self-shadowed)");
}

void sweepAllCells() {
  RtFloat const scales[] = {1.0f, 1000.0f, 0.001f};
  for (RtFloat const scale : scales) {
    for (PrimitiveKind const& kind : kPrimitiveKinds) {
      {
        GMANMatrix4 const placement = translatedPlacement(scale);
        Cell cell(std::string(kind.name) + " x" + std::to_string(scale) + " translated", kind.build(scale, placement),
                  kind.towardLight, placement, scale);
        sweepCell(cell);
      }
      {
        GMANMatrix4 const placement = skewedPlacement(scale);
        Cell cell(std::string(kind.name) + " x" + std::to_string(scale) + " skewed", kind.build(scale, placement),
                  kind.towardLight, placement, scale);
        sweepCell(cell);
      }
    }
  }
}

} // namespace

int main() {
  sweepAllCells();

  return checkSummary("R9's self-shadow sweep: every ray primitive, at every scale and placement, never shadows "
                      "itself once legitimate self-occlusion is excluded");
}
