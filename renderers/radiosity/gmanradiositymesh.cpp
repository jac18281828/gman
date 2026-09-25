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
#include <cmath>
#include <cstddef>
#include <utility>
#include <vector>

#include "gmanerror.h"
#include "gmanmath.h"
#include "gmanmatrix4.h"
#include "gmanpoint.h"
#include "gmanprimitives.h"
#include "gmanradiositygrouping.h"
#include "gmanradiositymesh.h"
#include "gmanray.h"
#include "gmanraybbox.h"
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
#include "gmanvector.h"
#include "gmanworldmanager.h"

namespace {

// No element occupies this polygon cell: clipping found it entirely
// outside.
constexpr std::size_t kNoElement = static_cast<std::size_t>(-1);

// A clipped cell's own net area at or under this is a sliver too small to
// divide safely for a centroid, or a hole's own subtraction erasing the
// cell outright; either way it gets no element, the same fate a cell
// entirely outside the outer loop meets.
constexpr double kMinPolygonCellArea = 1e-12;

// A ceiling on how many nu/nv relaxation rounds chooseParametricResolution
// takes, sized well above the worst case its own seven quadrics need.
// A full sphere -- the deepest coupling among them, since only its u (not
// its v) resolution depends on the other axis's own row sampling -- settles
// in 3: one round resolves v correctly (u independent of it) while u still
// sits at its pole-degenerate first guess, the next corrects u against that
// settled v, the third confirms nothing moved. Kept a fixed constant rather
// than tied to kMaxDivisions, so it stays a round count, not another factor
// of N, and the whole search stays within O(N^2 log N).
constexpr std::size_t kMaxResolutionRounds = 6;

// Two nodes of one primitive are the same node within this fraction of
// its gman::primitiveMagnitude: wide enough for a closed quadric's u = 0
// and u = 1 seam and a pole's row, which differ only by float rounding,
// and far below any element edge.
constexpr double kSameNodeScale = 1e-6;

// The sweep window's slack over the tolerance, relative to the nodes'
// extent: double rounding in two projections, far below any tolerance.
constexpr double kSweepSlack = 1e-12;

// A quadric's density central difference reaches this fraction of its
// cell to either side, balancing truncation, which grows with the step,
// against float getLocation's rounding, which shrinks with it: both stay
// under 1e-4 relative across a cell spanning 0.4 rad or less.
constexpr double kDensityStep = 0.05;

using Point2 = std::array<double, 2>;

RtFloat length(GMANVector const& v) { return (RtFloat)std::sqrt(v.dot(v)); }

RtFloat pointDistance(GMANPoint const& a, GMANPoint const& b) { return length(GMANVector(a, b)); }

// The (nu + 1) x (nv + 1) camera-space node positions of a parametric
// primitive's own getLocation, through objectToCamera, row-major in v
// (index j * (nu + 1) + i).
struct ParametricGrid {
  std::size_t nu = 1;
  std::size_t nv = 1;
  std::vector<GMANPoint> positions;
};

ParametricGrid buildParametricGrid(GMANParametric& parametric, GMANMatrix4 const& objectToCamera, std::size_t nu,
                                   std::size_t nv) {
  ParametricGrid grid;
  grid.nu = nu;
  grid.nv = nv;
  grid.positions.resize((nu + 1) * (nv + 1));
  for (std::size_t j = 0; j <= nv; ++j) {
    double const v = (double)j / (double)nv;
    for (std::size_t i = 0; i <= nu; ++i) {
      double const u = (double)i / (double)nu;
      grid.positions[j * (nu + 1) + i] = gman::transformPoint(objectToCamera, parametric.getLocation(u, v));
    }
  }
  return grid;
}

// An endpoint-to-endpoint distance alone reads as ~0 both for a true pole
// (getLocation constant across the whole edge) and for a periodic seam
// sampled too coarsely to see its own real extent (u = 0 meeting u = 1 at
// nu == 1 on a full sweep, camera space still visiting every point in
// between). Folding in the edge's own midpoint tells them apart without
// deciding which case this is: a pole's midpoint sits at that same ~0
// distance too, so it still measures as settled, while a wraparound's
// midpoint is far from both ends, so twice its own half-edge distance
// measures the real extent the naive endpoint distance missed.
RtFloat effectiveEdgeLength(GMANPoint const& p0, GMANPoint const& p1, GMANPoint const& mid) {
  RtFloat const halfA = pointDistance(p0, mid);
  RtFloat const halfB = pointDistance(mid, p1);
  return GMANMax(pointDistance(p0, p1), GMANMax(halfA, halfB) * (RtFloat)2.0);
}

void maxGridEdgeLengths(GMANParametric& parametric, GMANMatrix4 const& objectToCamera, ParametricGrid const& grid,
                        RtFloat& maxU, RtFloat& maxV) {
  std::size_t const nu = grid.nu;
  std::size_t const nv = grid.nv;
  auto at = [&](std::size_t i, std::size_t j) -> GMANPoint const& { return grid.positions[j * (nu + 1) + i]; };

  maxU = 0;
  for (std::size_t j = 0; j <= nv; ++j) {
    double const v = (double)j / (double)nv;
    for (std::size_t i = 0; i < nu; ++i) {
      double const uMid = ((double)i + 0.5) / (double)nu;
      GMANPoint const mid = gman::transformPoint(objectToCamera, parametric.getLocation(uMid, v));
      maxU = GMANMax(maxU, effectiveEdgeLength(at(i, j), at(i + 1, j), mid));
    }
  }

  maxV = 0;
  for (std::size_t i = 0; i <= nu; ++i) {
    double const u = (double)i / (double)nu;
    for (std::size_t j = 0; j < nv; ++j) {
      double const vMid = ((double)j + 0.5) / (double)nv;
      GMANPoint const mid = gman::transformPoint(objectToCamera, parametric.getLocation(u, vMid));
      maxV = GMANMax(maxV, effectiveEdgeLength(at(i, j), at(i, j + 1), mid));
    }
  }
}

// A primitive's own true edge length only shrinks, or holds, as its axis's
// division count grows -- a chord tightens toward its arc as it shortens,
// never loosens. resolveAxisCount leans on that to find the smallest count
// in [1, cap] whose own measure is at or under threshold: double the guess
// until measure holds or the cap is hit, then bisect the last doubling
// step down to the exact smallest count that holds. O(log count) calls to
// measure, against one per unit step.
template <class Measure> std::size_t resolveAxisCount(std::size_t cap, RtFloat threshold, Measure measure) {
  std::size_t hi = 1;
  bool hiHolds = measure(hi) <= threshold;
  while (!hiHolds && hi < cap) {
    hi = hi > cap / 2 ? cap : hi * 2;
    hiHolds = measure(hi) <= threshold;
  }
  if (!hiHolds) {
    return cap;
  }
  std::size_t lo = hi / 2;
  while (hi - lo > 1) {
    std::size_t const mid = lo + (hi - lo) / 2;
    if (measure(mid) <= threshold) {
      hi = mid;
    } else {
      lo = mid;
    }
  }
  return hi;
}

// The smallest (nu, nv), capped at GMANRadiosityMesh::kMaxDivisions each,
// whose own real grid keeps every edge at or under maxEdgeLength. Growing
// nv only ever reveals a larger true maxU (finer v-sampling never shrinks
// an edge already found), and symmetrically for nu against maxV, so
// resolving nu then nv, each against the other's latest count, is a
// monotone relaxation that climbs to the same fixed point the old one-step
// joint growth reached -- in a handful of rounds rather than one step per
// unit.
ParametricGrid chooseParametricResolution(GMANParametric& parametric, GMANMatrix4 const& objectToCamera,
                                          RtFloat maxEdgeLength) {
  std::size_t const cap = GMANRadiosityMesh::kMaxDivisions;
  std::size_t nu = 1;
  std::size_t nv = 1;

  for (std::size_t round = 0; round < kMaxResolutionRounds; ++round) {
    std::size_t const prevNu = nu;
    std::size_t const prevNv = nv;

    nu = resolveAxisCount(cap, maxEdgeLength, [&](std::size_t candidate) {
      ParametricGrid const grid = buildParametricGrid(parametric, objectToCamera, candidate, nv);
      RtFloat maxU = 0, maxV = 0;
      maxGridEdgeLengths(parametric, objectToCamera, grid, maxU, maxV);
      return maxU;
    });

    nv = resolveAxisCount(cap, maxEdgeLength, [&](std::size_t candidate) {
      ParametricGrid const grid = buildParametricGrid(parametric, objectToCamera, nu, candidate);
      RtFloat maxU = 0, maxV = 0;
      maxGridEdgeLengths(parametric, objectToCamera, grid, maxU, maxV);
      return maxV;
    });

    if (nu == prevNu && nv == prevNv) {
      break;
    }
  }

  return buildParametricGrid(parametric, objectToCamera, nu, nv);
}

// Appends grid's own (nu + 1) x (nv + 1) node positions to nodes, each
// node's normal from getNormal through cameraToObject's inverse transpose.
void appendParametricNodes(GMANParametric& parametric, GMANMatrix4 const& cameraToObject, ParametricGrid const& grid,
                           std::vector<GMANRadiosityNode>& nodes) {
  std::size_t const nu = grid.nu;
  std::size_t const nv = grid.nv;
  for (std::size_t j = 0; j <= nv; ++j) {
    double const v = (double)j / (double)nv;
    for (std::size_t i = 0; i <= nu; ++i) {
      double const u = (double)i / (double)nu;
      GMANVector const objNormal = parametric.getNormal(u, v);
      GMANVector normal = gman::transformNormal(cameraToObject, objNormal);
      normal.normalize();

      GMANRadiosityNode node;
      node.position = grid.positions[j * (nu + 1) + i];
      node.normal = normal;
      node.inside = true;
      nodes.push_back(node);
    }
  }
}

// Tries one quadric class's own getObjectToCamera, false for anything else.
template <class Quadric> bool tryQuadricObjectToCamera(GMANPrimitive* primitive, GMANMatrix4& matrix) {
  auto* p = dynamic_cast<Quadric*>(primitive);
  if (!p) {
    return false;
  }
  matrix = p->getObjectToCamera();
  return true;
}

// Fetches the shutter-open placement from whichever of the seven quadric
// classes primitive actually is, tried once per type. Returns false for
// anything else, including a GMANRayPolygon, which has no such matrix.
bool getQuadricObjectToCamera(GMANPrimitive* primitive, GMANMatrix4& matrix) {
  return tryQuadricObjectToCamera<GMANRaySphere>(primitive, matrix) ||
         tryQuadricObjectToCamera<GMANRayCylinder>(primitive, matrix) ||
         tryQuadricObjectToCamera<GMANRayCone>(primitive, matrix) ||
         tryQuadricObjectToCamera<GMANRayDisk>(primitive, matrix) ||
         tryQuadricObjectToCamera<GMANRayParaboloid>(primitive, matrix) ||
         tryQuadricObjectToCamera<GMANRayHyperboloid>(primitive, matrix) ||
         tryQuadricObjectToCamera<GMANRayTorus>(primitive, matrix);
}

// The first edge long enough to define a direction -- isDegenerate()
// having already ruled out a polygon with none -- normalized.
GMANVector firstEdgeDirection(std::vector<GMANPoint> const& loop) {
  constexpr RtFloat kMinEdgeLength = (RtFloat)1e-9;
  std::size_t const n = loop.size();
  for (std::size_t i = 0; i < n; ++i) {
    GMANVector const edge(loop[i], loop[(i + 1) % n]);
    RtFloat const len = length(edge);
    if (len > kMinEdgeLength) {
      return edge / len;
    }
  }
  return GMANVector(1, 0, 0);
}

// A polygon's own orthonormal in-plane basis: e0 along its first
// non-degenerate edge, e1 completing a right-handed frame with its plane
// normal, both anchored at its first vertex.
struct PolygonBasis {
  GMANPoint origin;
  GMANVector e0;
  GMANVector e1;
};

PolygonBasis computePolygonBasis(std::vector<GMANPoint> const& loop, GMANVector const& normal) {
  PolygonBasis basis;
  basis.origin = loop[0];
  basis.e0 = firstEdgeDirection(loop);
  basis.e1 = normal.cross(basis.e0);
  basis.e1.normalize();
  return basis;
}

// A polygon's own vertices in basis's (s, t) coordinates, and their
// bounding extent -- double precision throughout, so a polygon's own tight
// area tolerance below measures geometry, not accumulated float rounding
// across many cells.
struct PolygonExtent {
  std::vector<Point2> loopST;
  double sMin = 0, sMax = 0, tMin = 0, tMax = 0;
};

PolygonExtent projectPolygonExtent(std::vector<GMANPoint> const& loop, PolygonBasis const& basis) {
  PolygonExtent extent;
  extent.loopST.resize(loop.size());
  for (std::size_t i = 0; i < loop.size(); ++i) {
    GMANVector const rel(basis.origin, loop[i]);
    double const s = (double)rel.dot(basis.e0);
    double const t = (double)rel.dot(basis.e1);
    extent.loopST[i] = {s, t};
    if (i == 0) {
      extent.sMin = extent.sMax = s;
      extent.tMin = extent.tMax = t;
    } else {
      extent.sMin = GMANMin(extent.sMin, s);
      extent.sMax = GMANMax(extent.sMax, s);
      extent.tMin = GMANMin(extent.tMin, t);
      extent.tMax = GMANMax(extent.tMax, t);
    }
  }
  return extent;
}

// A hole loop's own vertices in basis's (s, t) coordinates -- the same
// projection projectPolygonExtent applies to the outer loop, without the
// extent: a hole is assumed inside the outer loop's own bounds, so it
// contributes no grid sizing of its own.
std::vector<Point2> projectLoopST(std::vector<GMANPoint> const& loop, PolygonBasis const& basis) {
  std::vector<Point2> loopST(loop.size());
  for (std::size_t i = 0; i < loop.size(); ++i) {
    GMANVector const rel(basis.origin, loop[i]);
    loopST[i] = {(double)rel.dot(basis.e0), (double)rel.dot(basis.e1)};
  }
  return loopST;
}

// A polygon's own grid resolution and step, once its extent is known: the
// smallest counts, capped, that keep a cell's own edge at or under
// maxEdgeLength.
struct PolygonGrid {
  std::size_t nu = 1;
  std::size_t nv = 1;
  double duStep = 0;
  double dvStep = 0;
};

std::size_t resolutionFromExtent(RtFloat extent, RtFloat maxEdgeLength) {
  if (extent <= (RtFloat)0) {
    return 1;
  }
  double const raw = std::ceil((double)(extent / maxEdgeLength));
  std::size_t const count = raw < 1.0 ? 1 : (std::size_t)raw;
  return GMANMin(count, GMANRadiosityMesh::kMaxDivisions);
}

// Even-odd containment of (s, t) in loopST's own ring, in the polygon's
// in-plane basis -- gmanraypolygon.cpp's insidePolygon, projected through
// that basis instead of a dominant coordinate plane.
bool insidePolygonST(std::vector<Point2> const& loopST, double s, double t) {
  bool inside = false;
  std::size_t const n = loopST.size();
  for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
    double const si = loopST[i][0], ti = loopST[i][1];
    double const sj = loopST[j][0], tj = loopST[j][1];
    if ((ti > t) != (tj > t)) {
      double const sCross = si + (sj - si) * (t - ti) / (tj - ti);
      if (s < sCross) {
        inside = !inside;
      }
    }
  }
  return inside;
}

Point2 clipIntersection(Point2 const& a, Point2 const& b, int axis, double bound) {
  double const da = a[axis] - bound;
  double const db = b[axis] - bound;
  double const t = da / (da - db);
  return {a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t};
}

// Sutherland-Hodgman against one half-plane (axis >= bound, or axis <=
// bound when keepAbove is false). Only the clip window need be convex --
// each grid cell here -- for this to give the correct signed area and
// centroid; the subject polygon (an outer loop or a hole) can be concave.
std::vector<Point2> clipHalfPlane(std::vector<Point2> const& poly, int axis, double bound, bool keepAbove) {
  std::vector<Point2> out;
  std::size_t const n = poly.size();
  if (n == 0) {
    return out;
  }
  for (std::size_t i = 0; i < n; ++i) {
    Point2 const& curr = poly[i];
    Point2 const& prev = poly[(i + n - 1) % n];
    bool const currIn = keepAbove ? curr[axis] >= bound : curr[axis] <= bound;
    bool const prevIn = keepAbove ? prev[axis] >= bound : prev[axis] <= bound;
    if (currIn != prevIn) {
      out.push_back(clipIntersection(prev, curr, axis, bound));
    }
    if (currIn) {
      out.push_back(curr);
    }
  }
  return out;
}

std::vector<Point2> clipCellAgainstPolygon(std::vector<Point2> const& loopST, double sLo, double sHi, double tLo,
                                           double tHi) {
  std::vector<Point2> result = loopST;
  result = clipHalfPlane(result, 0, sLo, true);
  result = clipHalfPlane(result, 0, sHi, false);
  result = clipHalfPlane(result, 1, tLo, true);
  result = clipHalfPlane(result, 1, tHi, false);
  return result;
}

// The shoelace signed area and raw (undivided) first moments of a
// (possibly clockwise) simple 2D polygon: momentX/momentY are 6 * area *
// centroid, sign and all, so a caller can subtract one loop's moment from
// another's before ever dividing by an area that might itself be near
// zero.
void polygon2DAreaAndMoments(std::vector<Point2> const& poly, double& signedArea, double& momentX, double& momentY) {
  signedArea = 0;
  momentX = 0;
  momentY = 0;
  std::size_t const n = poly.size();
  for (std::size_t i = 0; i < n; ++i) {
    Point2 const& a = poly[i];
    Point2 const& b = poly[(i + 1) % n];
    double const cross = a[0] * b[1] - b[0] * a[1];
    signedArea += cross;
    momentX += (a[0] + b[0]) * cross;
    momentY += (a[1] + b[1]) * cross;
  }
  signedArea *= 0.5;
}

// One loop's own contribution to a cell's net area and moment: loopST
// clipped against the cell, then reduced to a magnitude-only area and a
// moment carrying the same sign convention as area * centroid regardless
// of loopST's own winding, so an outer loop's and a hole's own
// contributions add and subtract correctly no matter how each was wound.
struct ClippedMoments {
  double area = 0;
  double momentX = 0;
  double momentY = 0;
};

ClippedMoments clipCellMoments(std::vector<Point2> const& loopST, double sLo, double sHi, double tLo, double tHi) {
  std::vector<Point2> const clipped = clipCellAgainstPolygon(loopST, sLo, sHi, tLo, tHi);
  double signedArea = 0, sumX = 0, sumY = 0;
  polygon2DAreaAndMoments(clipped, signedArea, sumX, sumY);
  double const sign = signedArea < 0.0 ? -1.0 : 1.0;
  return {std::fabs(signedArea), sign * sumX, sign * sumY};
}

PolygonGrid choosePolygonGrid(PolygonExtent const& extent, RtFloat maxEdgeLength) {
  PolygonGrid grid;
  double const extentU = GMANMax(extent.sMax - extent.sMin, 0.0);
  double const extentV = GMANMax(extent.tMax - extent.tMin, 0.0);
  grid.nu = resolutionFromExtent((RtFloat)extentU, maxEdgeLength);
  grid.nv = resolutionFromExtent((RtFloat)extentV, maxEdgeLength);
  grid.duStep = extentU / (double)grid.nu;
  grid.dvStep = extentV / (double)grid.nv;
  return grid;
}

// Appends a polygon's own (nu + 1) x (nv + 1) grid nodes -- every one of
// them, inside the polygon or not, flagged accordingly, so a clipped
// cell's own four corners are always there to interpolate between. A node
// is inside only within the outer loop and outside every hole.
void appendPolygonNodes(PolygonBasis const& basis, PolygonExtent const& extent, PolygonGrid const& grid,
                        std::vector<std::vector<Point2>> const& holesST, GMANVector const& normal,
                        std::vector<GMANRadiosityNode>& nodes) {
  for (std::size_t j = 0; j <= grid.nv; ++j) {
    double const t = extent.tMin + grid.dvStep * (double)j;
    for (std::size_t i = 0; i <= grid.nu; ++i) {
      double const s = extent.sMin + grid.duStep * (double)i;
      bool inside = insidePolygonST(extent.loopST, s, t);
      for (std::vector<Point2> const& holeST : holesST) {
        if (inside && insidePolygonST(holeST, s, t)) {
          inside = false;
        }
      }

      GMANRadiosityNode node;
      node.position = basis.origin + basis.e0 * (RtFloat)s + basis.e1 * (RtFloat)t;
      node.normal = normal;
      node.inside = inside;
      nodes.push_back(node);
    }
  }
}

// Clips each grid cell against the outer loop and against every hole, and
// appends an element for every cell whose net area (outer minus holes)
// clears kMinPolygonCellArea -- its own net area and centroid, so the
// kept elements sum to the outer loop's own area minus its holes', up to
// rounding. A cell the outer loop's own clip finds outside, or a hole's
// own subtraction erases, is dropped; cellToElement stays kNoElement
// there.
void appendPolygonElements(PolygonBasis const& basis, PolygonExtent const& extent, PolygonGrid const& grid,
                           std::vector<std::vector<Point2>> const& holesST, GMANVector const& normal,
                           std::size_t nodeOffset, std::vector<GMANRadiosityElement>& elements,
                           std::vector<std::size_t>& cellToElement) {
  for (std::size_t j = 0; j < grid.nv; ++j) {
    double const tLo = extent.tMin + grid.dvStep * (double)j;
    double const tHi = tLo + grid.dvStep;
    for (std::size_t i = 0; i < grid.nu; ++i) {
      double const sLo = extent.sMin + grid.duStep * (double)i;
      double const sHi = sLo + grid.duStep;

      ClippedMoments net = clipCellMoments(extent.loopST, sLo, sHi, tLo, tHi);
      for (std::vector<Point2> const& holeST : holesST) {
        ClippedMoments const hole = clipCellMoments(holeST, sLo, sHi, tLo, tHi);
        net.area -= hole.area;
        net.momentX -= hole.momentX;
        net.momentY -= hole.momentY;
      }
      if (net.area <= kMinPolygonCellArea) {
        continue; // outside the outer loop, or a hole erased the cell
      }

      double const cx = net.momentX / (6.0 * net.area);
      double const cy = net.momentY / (6.0 * net.area);

      std::array<std::size_t, 4> const corners = {
          nodeOffset + j * (grid.nu + 1) + i,
          nodeOffset + j * (grid.nu + 1) + i + 1,
          nodeOffset + (j + 1) * (grid.nu + 1) + i + 1,
          nodeOffset + (j + 1) * (grid.nu + 1) + i,
      };

      GMANRadiosityElement element;
      element.area = (RtFloat)net.area;
      element.normal = normal;
      element.centre = basis.origin + basis.e0 * (RtFloat)cx + basis.e1 * (RtFloat)cy;
      element.corners = corners;

      std::size_t const cell = j * grid.nu + i;
      cellToElement[cell] = elements.size();
      elements.push_back(element);
    }
  }
}

double pointDistanceDouble(GMANPoint const& a, GMANPoint const& b) {
  double const dx = (double)b.getX() - (double)a.getX();
  double const dy = (double)b.getY() - (double)a.getY();
  double const dz = (double)b.getZ() - (double)a.getZ();
  return std::sqrt(dx * dx + dy * dy + dz * dz);
}

std::array<double, 3> pointDifference(GMANPoint const& hi, GMANPoint const& lo, double span) {
  return {((double)hi.getX() - (double)lo.getX()) / span, ((double)hi.getY() - (double)lo.getY()) / span,
          ((double)hi.getZ() - (double)lo.getZ()) / span};
}

double crossMagnitude(std::array<double, 3> const& a, std::array<double, 3> const& b) {
  double const x = a[1] * b[2] - a[2] * b[1];
  double const y = a[2] * b[0] - a[0] * b[2];
  double const z = a[0] * b[1] - a[1] * b[0];
  return std::sqrt(x * x + y * y + z * z);
}

} // namespace

// Per-primitive dicing state locate() needs: which grid a hit's own
// primitive belongs to, and how to turn (hit.u, hit.v) or a hit point into
// a cell within it.
struct GMANRadiosityMesh::PrimitiveMesh {
  GMANPrimitive const* primitive = nullptr;
  GMANRayInterface const* rayPrimitive = nullptr;
  bool isPolygon = false;

  std::size_t nu = 0;
  std::size_t nv = 0;
  std::size_t nodeOffset = 0;
  std::size_t elementOffset = 0;

  // Dense over nu * nv, index j * nu + i: the element index that cell
  // holds. Always populated for a quadric; kNoElement for a polygon cell
  // clipping dropped entirely.
  std::vector<std::size_t> cellToElement;

  // Polygon only: the in-plane basis and grid origin a hit point projects
  // through to find its own cell.
  GMANPoint basisOrigin;
  GMANVector basisE0;
  GMANVector basisE1;
  double uMin = 0;
  double vMin = 0;
  double duStep = 0;
  double dvStep = 0;

  // Polygon only: the plane normal and the loops, in the same (s, t)
  // basis, that surfacePoint tests containment against.
  GMANVector planeNormal;
  std::vector<Point2> outerST;
  std::vector<std::vector<Point2>> holesST;

  // Quadric only: the surface and its placement surfacePoint evaluates.
  // getLocation and getNormal are non-const, hence the non-const pointer.
  GMANParametric* parametric = nullptr;
  GMANMatrix4 objectToCamera;
  GMANMatrix4 cameraToObject;
};

struct GMANRadiosityMesh::ElementCell {
  std::size_t primitiveMesh = 0;
  std::size_t i = 0;
  std::size_t j = 0;
};

GMANRadiosityMesh::GMANRadiosityMesh() = default;
GMANRadiosityMesh::~GMANRadiosityMesh() = default;
GMANRadiosityMesh::GMANRadiosityMesh(GMANRadiosityMesh&&) noexcept = default;
GMANRadiosityMesh& GMANRadiosityMesh::operator=(GMANRadiosityMesh&&) noexcept = default;

GMANRadiosityElement GMANRadiosityMesh::buildFacetElement(std::array<std::size_t, 4> const& corners) const {
  GMANPoint const& p0 = nodes[corners[0]].position;
  GMANPoint const& p1 = nodes[corners[1]].position;
  GMANPoint const& p2 = nodes[corners[2]].position;
  GMANPoint const& p3 = nodes[corners[3]].position;

  // The quad's two triangles, split along the (corner 0, corner 2)
  // diagonal. A pole cell -- p0 == p1 or p0 == p3, say -- degrades one
  // triangle's own area to zero on its own, so a one-triangle pole cell
  // falls out of the general two-triangle formula with no special case.
  GMANVector const areaVec1 = GMANVector(p0, p1).cross(GMANVector(p0, p2)) * (RtFloat)0.5;
  GMANVector const areaVec2 = GMANVector(p0, p2).cross(GMANVector(p0, p3)) * (RtFloat)0.5;
  RtFloat const area1 = length(areaVec1);
  RtFloat const area2 = length(areaVec2);
  RtFloat const totalArea = area1 + area2;

  GMANVector const areaVecSum = areaVec1 + areaVec2;
  GMANVector const cornerNormalSum =
      nodes[corners[0]].normal + nodes[corners[1]].normal + nodes[corners[2]].normal + nodes[corners[3]].normal;

  GMANRadiosityElement element;
  element.area = totalArea;
  element.corners = corners;

  RtFloat const areaVecLength = length(areaVecSum);
  if (areaVecLength > (RtFloat)0) {
    GMANVector normal = areaVecSum / areaVecLength;
    if (normal.dot(cornerNormalSum) < (RtFloat)0) {
      normal = -normal;
    }
    element.normal = normal;
  } else {
    RtFloat const cornerNormalLength = length(cornerNormalSum);
    element.normal = cornerNormalLength > (RtFloat)0 ? cornerNormalSum / cornerNormalLength : GMANVector(0, 0, 0);
  }

  GMANPoint const centroid1 = (p0 + p1 + p2) * (RtFloat)(1.0 / 3.0);
  GMANPoint const centroid2 = (p0 + p2 + p3) * (RtFloat)(1.0 / 3.0);
  if (totalArea > (RtFloat)0) {
    element.centre = (centroid1 * area1 + centroid2 * area2) * ((RtFloat)1.0 / totalArea);
  } else {
    element.centre = ((p0 + p1) + (p2 + p3)) * (RtFloat)0.25;
  }

  return element;
}

void GMANRadiosityMesh::appendParametricElements(std::size_t nu, std::size_t nv, std::size_t nodeOffset,
                                                 std::vector<std::size_t>& cellToElement) {
  for (std::size_t j = 0; j < nv; ++j) {
    for (std::size_t i = 0; i < nu; ++i) {
      std::array<std::size_t, 4> const corners = {
          nodeOffset + j * (nu + 1) + i,
          nodeOffset + j * (nu + 1) + i + 1,
          nodeOffset + (j + 1) * (nu + 1) + i + 1,
          nodeOffset + (j + 1) * (nu + 1) + i,
      };
      std::size_t const cell = j * nu + i;
      cellToElement[cell] = elements.size();
      elements.push_back(buildFacetElement(corners));
    }
  }
}

void GMANRadiosityMesh::diceParametric(GMANPrimitive* primitive, GMANParametric& parametric,
                                       GMANMatrix4 const& objectToCamera, GMANMatrix4 const& cameraToObject,
                                       RtFloat maxEdgeLength) {
  ParametricGrid const grid = chooseParametricResolution(parametric, objectToCamera, maxEdgeLength);
  std::size_t const nu = grid.nu;
  std::size_t const nv = grid.nv;
  std::size_t const nodeOffset = nodes.size();
  std::size_t const elementOffset = elements.size();
  appendParametricNodes(parametric, cameraToObject, grid, nodes);

  PrimitiveMesh mesh;
  mesh.primitive = primitive;
  mesh.rayPrimitive = dynamic_cast<GMANRayInterface const*>(primitive);
  mesh.isPolygon = false;
  mesh.nu = nu;
  mesh.nv = nv;
  mesh.nodeOffset = nodeOffset;
  mesh.elementOffset = elementOffset;
  mesh.cellToElement.resize(nu * nv);
  mesh.parametric = &parametric;
  mesh.objectToCamera = objectToCamera;
  mesh.cameraToObject = cameraToObject;

  appendParametricElements(nu, nv, nodeOffset, mesh.cellToElement);

  primitiveMeshes.push_back(std::move(mesh));
  finishPrimitiveMesh();
}

void GMANRadiosityMesh::fillPolygonPrimitiveMesh(GMANRayPolygon const& polygon, GMANPoint const& basisOrigin,
                                                 GMANVector const& basisE0, GMANVector const& basisE1,
                                                 GMANVector const& normal, double uMin, double vMin, std::size_t nu,
                                                 std::size_t nv, double duStep, double dvStep, std::size_t nodeOffset,
                                                 std::size_t elementOffset, PrimitiveMesh& mesh) const {
  mesh.primitive = &polygon;
  mesh.rayPrimitive = &polygon;
  mesh.isPolygon = true;
  mesh.nu = nu;
  mesh.nv = nv;
  mesh.nodeOffset = nodeOffset;
  mesh.elementOffset = elementOffset;
  mesh.basisOrigin = basisOrigin;
  mesh.basisE0 = basisE0;
  mesh.basisE1 = basisE1;
  mesh.uMin = uMin;
  mesh.vMin = vMin;
  mesh.duStep = duStep;
  mesh.dvStep = dvStep;
  mesh.cellToElement.assign(nu * nv, kNoElement);
  mesh.planeNormal = normal;
}

void GMANRadiosityMesh::dicePolygon(GMANRayPolygon const& polygon, RtFloat maxEdgeLength) {
  if (polygon.isDegenerate()) {
    // Its own intersect() never hits it either, so there is nothing for
    // locate() to ever need here: zero elements, not a skip.
    return;
  }

  std::vector<GMANPoint> const& loop = polygon.getOuterLoop();
  std::vector<std::vector<GMANPoint>> const& holes = polygon.getHoles();
  GMANVector const normal = polygon.getPlaneNormal();

  PolygonBasis const basis = computePolygonBasis(loop, normal);
  PolygonExtent const extent = projectPolygonExtent(loop, basis);
  PolygonGrid const grid = choosePolygonGrid(extent, maxEdgeLength);

  std::vector<std::vector<Point2>> holesST(holes.size());
  for (std::size_t h = 0; h < holes.size(); ++h) {
    holesST[h] = projectLoopST(holes[h], basis);
  }

  std::size_t const nodeOffset = nodes.size();
  std::size_t const elementOffset = elements.size();
  appendPolygonNodes(basis, extent, grid, holesST, normal, nodes);

  PrimitiveMesh mesh;
  fillPolygonPrimitiveMesh(polygon, basis.origin, basis.e0, basis.e1, normal, extent.sMin, extent.tMin, grid.nu,
                           grid.nv, grid.duStep, grid.dvStep, nodeOffset, elementOffset, mesh);
  appendPolygonElements(basis, extent, grid, holesST, normal, nodeOffset, elements, mesh.cellToElement);
  mesh.outerST = extent.loopST;
  mesh.holesST = std::move(holesST);

  primitiveMeshes.push_back(std::move(mesh));
  finishPrimitiveMesh();
}

void GMANRadiosityMesh::diceMesh(GMANRayPolygonMesh const& mesh, RtFloat maxEdgeLength) {
  for (std::size_t i = 0; i < mesh.getFaceCount(); ++i) {
    dicePolygon(mesh.getFace(i), maxEdgeLength);
  }
}

bool GMANRadiosityMesh::diceOne(GMANPrimitive* primitive, RtFloat maxEdgeLength) {
  if (auto* polygon = dynamic_cast<GMANRayPolygon*>(primitive)) {
    dicePolygon(*polygon, maxEdgeLength);
    return true;
  }
  if (auto* mesh = dynamic_cast<GMANRayPolygonMesh*>(primitive)) {
    diceMesh(*mesh, maxEdgeLength);
    return true;
  }

  GMANMatrix4 objectToCamera;
  if (!getQuadricObjectToCamera(primitive, objectToCamera)) {
    return false;
  }
  auto* parametric = dynamic_cast<GMANParametric*>(primitive);
  if (!parametric) {
    return false; // every quadric above is also GMANParametric; defensive
  }

  GMANMatrix4 cameraToObject = objectToCamera;
  try {
    cameraToObject.invert();
  } catch (GMANError const&) {
    // Its own intersect() never hits it either (the same object-space
    // round trip fails there), so it has no surface left to dice.
    return false;
  }

  diceParametric(primitive, *parametric, objectToCamera, cameraToObject, maxEdgeLength);
  return true;
}

void GMANRadiosityMesh::build(GMANWorldManager& worldManager, RtFloat maxEdgeLength) {
  nodes.clear();
  elements.clear();
  primitiveMeshes.clear();
  elementCells.clear();
  nodeGroups.clear();
  skippedCount = 0;

  for (GMANPrimitive* primitive = worldManager.getFirst(); primitive; primitive = worldManager.getNext()) {
    if (!dynamic_cast<GMANRayInterface*>(primitive)) {
      ++skippedCount;
      continue;
    }
    if (!diceOne(primitive, maxEdgeLength)) {
      ++skippedCount;
    }
  }
}

bool GMANRadiosityMesh::locate(GMANHit const& hit, GMANRadiosityLocation& location) const {
  auto const it = std::find_if(primitiveMeshes.begin(), primitiveMeshes.end(),
                               [&](PrimitiveMesh const& mesh) { return mesh.primitive == hit.primitive; });
  if (it == primitiveMeshes.end()) {
    return false;
  }
  PrimitiveMesh const& mesh = *it;

  double s, t;
  double originU, originV, stepU, stepV;
  if (mesh.isPolygon) {
    GMANVector const rel(mesh.basisOrigin, hit.point);
    s = (double)rel.dot(mesh.basisE0);
    t = (double)rel.dot(mesh.basisE1);
    originU = mesh.uMin;
    originV = mesh.vMin;
    stepU = mesh.duStep;
    stepV = mesh.dvStep;
  } else {
    s = (double)GMANClamp(hit.u, (RtFloat)0, (RtFloat)1);
    t = (double)GMANClamp(hit.v, (RtFloat)0, (RtFloat)1);
    originU = 0;
    originV = 0;
    stepU = 1.0 / (double)mesh.nu;
    stepV = 1.0 / (double)mesh.nv;
  }

  double const fu = (s - originU) / stepU;
  double const fv = (t - originV) / stepV;

  std::size_t i = fu < 0.0 ? 0 : (std::size_t)fu;
  std::size_t j = fv < 0.0 ? 0 : (std::size_t)fv;
  i = GMANMin(i, mesh.nu - 1);
  j = GMANMin(j, mesh.nv - 1);

  std::size_t const elementIndex = mesh.cellToElement[j * mesh.nu + i];
  if (elementIndex == kNoElement) {
    return false;
  }

  RtFloat const localU = GMANClamp((RtFloat)(fu - (double)i), (RtFloat)0, (RtFloat)1);
  RtFloat const localV = GMANClamp((RtFloat)(fv - (double)j), (RtFloat)0, (RtFloat)1);

  GMANRadiosityElement const& element = elements[elementIndex];
  location.element = elementIndex;
  location.corners = element.corners;
  location.weights[0] = (1 - localU) * (1 - localV);
  location.weights[1] = localU * (1 - localV);
  location.weights[2] = localU * localV;
  location.weights[3] = (1 - localU) * localV;
  return true;
}

GMANRayInterface const* GMANRadiosityMesh::getElementPrimitive(std::size_t element) const {
  return primitiveMeshes[elementCells[element].primitiveMesh].rayPrimitive;
}

bool GMANRadiosityMesh::polygonSurfacePoint(PrimitiveMesh const& mesh, ElementCell const& cell, double s, double t,
                                            GMANRadiositySurfacePoint& point) const {
  double const planeS = mesh.uMin + mesh.duStep * ((double)cell.i + s);
  double const planeT = mesh.vMin + mesh.dvStep * ((double)cell.j + t);
  if (!insidePolygonST(mesh.outerST, planeS, planeT)) {
    return false;
  }
  for (std::vector<Point2> const& holeST : mesh.holesST) {
    if (insidePolygonST(holeST, planeS, planeT)) {
      return false;
    }
  }
  point.P = mesh.basisOrigin + mesh.basisE0 * (RtFloat)planeS + mesh.basisE1 * (RtFloat)planeT;
  point.N = mesh.planeNormal;
  point.density = (RtFloat)(mesh.duStep * mesh.dvStep);
  return true;
}

GMANRadiositySurfacePoint GMANRadiosityMesh::quadricSurfacePoint(PrimitiveMesh const& mesh, ElementCell const& cell,
                                                                 double s, double t) const {
  double const nu = (double)mesh.nu;
  double const nv = (double)mesh.nv;
  double const u = ((double)cell.i + s) / nu;
  double const v = ((double)cell.j + t) / nv;
  auto const location = [&](double atU, double atV) {
    return gman::transformPoint(mesh.objectToCamera, mesh.parametric->getLocation(atU, atV));
  };

  GMANRadiositySurfacePoint found;
  found.P = location(u, v);
  found.N = gman::transformNormal(mesh.cameraToObject, mesh.parametric->getNormal(u, v));
  found.N.normalize();

  // The stencil stays inside the primitive's own [0, 1]^2, one-sided at
  // its border.
  double const uLo = GMANMax(u - kDensityStep / nu, 0.0);
  double const uHi = GMANMin(u + kDensityStep / nu, 1.0);
  double const vLo = GMANMax(v - kDensityStep / nv, 0.0);
  double const vHi = GMANMin(v + kDensityStep / nv, 1.0);
  std::array<double, 3> const dPdu = pointDifference(location(uHi, v), location(uLo, v), uHi - uLo);
  std::array<double, 3> const dPdv = pointDifference(location(u, vHi), location(u, vLo), vHi - vLo);
  found.density = (RtFloat)(crossMagnitude(dPdu, dPdv) / (nu * nv));
  return found;
}

bool GMANRadiosityMesh::surfacePoint(std::size_t element, double s, double t, GMANRadiositySurfacePoint& point) const {
  ElementCell const& cell = elementCells[element];
  PrimitiveMesh const& mesh = primitiveMeshes[cell.primitiveMesh];
  GMANRadiositySurfacePoint found;

  if (mesh.isPolygon) {
    if (!polygonSurfacePoint(mesh, cell, s, t, found)) {
      return false;
    }
  } else {
    found = quadricSurfacePoint(mesh, cell, s, t);
  }

  GMANVector const& front = elements[element].normal;
  if (found.N.dot(found.N) == (RtFloat)0) {
    found.N = front;
  } else if (found.N.dot(front) < (RtFloat)0) {
    found.N = -found.N;
  }
  point = found;
  return true;
}

bool GMANRadiosityMesh::sameNode(std::size_t a, std::size_t b) const { return nodeGroups[a] == nodeGroups[b]; }

// Common tail of diceParametric and dicePolygon.
void GMANRadiosityMesh::finishPrimitiveMesh() {
  recordElementCells();
  groupCoincidentNodes();
}

void GMANRadiosityMesh::recordElementCells() {
  std::size_t const meshIndex = primitiveMeshes.size() - 1;
  PrimitiveMesh const& mesh = primitiveMeshes.back();
  elementCells.resize(elements.size());
  for (std::size_t cell = 0; cell < mesh.cellToElement.size(); ++cell) {
    std::size_t const element = mesh.cellToElement[cell];
    if (element == kNoElement) {
      continue;
    }
    elementCells[element] = {meshIndex, cell % mesh.nu, cell / mesh.nu};
  }
}

void GMANRadiosityMesh::groupCoincidentNodes() {
  PrimitiveMesh const& mesh = primitiveMeshes.back();
  std::size_t const begin = mesh.nodeOffset;
  std::size_t const count = nodes.size() - begin;
  double const magnitude = (double)gman::primitiveMagnitude(mesh.rayPrimitive->getBBox());
  double const tolerance = kSameNodeScale * magnitude;
  double extent = 0.0;
  for (std::size_t local = 0; local < count; ++local) {
    extent = GMANMax(extent, gman::maxAbsCoordinate(nodes[begin + local].position));
  }
  double const window = tolerance + kSweepSlack * extent;

  std::vector<std::pair<double, std::size_t>> order;
  order.reserve(count);
  for (std::size_t local = 0; local < count; ++local) {
    order.push_back({gman::nodeGroupSweepProjection(nodes[begin + local].position), local});
  }
  std::sort(order.begin(), order.end());

  std::vector<std::size_t> parent(count);
  for (std::size_t local = 0; local < count; ++local) {
    parent[local] = local;
  }
  for (std::size_t i = 0; i < count; ++i) {
    GMANPoint const& a = nodes[begin + order[i].second].position;
    for (std::size_t j = i + 1; j < count && order[j].first - order[i].first <= window; ++j) {
      if (pointDistanceDouble(a, nodes[begin + order[j].second].position) <= tolerance) {
        gman::uniteGroups(parent, order[i].second, order[j].second);
      }
    }
  }

  nodeGroups.resize(nodes.size());
  for (std::size_t local = 0; local < count; ++local) {
    nodeGroups[begin + local] = begin + gman::findGroupRoot(parent, local);
  }
}
