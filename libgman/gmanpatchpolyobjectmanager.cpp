/* SPDX-License-Identifier: LGPL-2.1-or-later */

/* This is part of GMAN, a RenderMan-compatible renderer.
 *
 * Copyright (c) 2001, 2000, 1999  John Cairns
 *
 * Author: John Cairns <john@2ad.com>
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
#include <cstring>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "gmanobjectmanager.h"
#include "gmanpatchpolyobjectmanager.h"
#include "gmanpolygon.h"
#include "gmanpolygoninternal.h"
#include "gmanpolygonmesh.h"
#include "gmanprimitives.h"
#include "gmanshading.h"
#include "gmanviewingsystem.h"
#include "gmanviewingsysteminputs.h"
#include "gmanvsorthographic.h"
#include "gmanvsperspective.h"
#include "ri.h"

namespace {

// The one tolerance shared by every classification below: a pure number,
// not an area, since every comparison it guards is a ratio (a cross-dot
// divided by the lengths that give it units) rather than a raw cross-dot.
// RI_EPSILON (a public interface constant with other callers, and an
// area rather than a ratio) does not apply. float carries about seven
// decimal digits, so a ratio built from two cross products, a dot and a
// division carries absolute error near 1e-7; this sits an order above
// that noise and far below any turn or offset a real polygon intends --
// 1e-6 radians is 0.00006 degrees.
const RtFloat kTriangulationTolerance = (RtFloat)1.0e-6;

// buildPolygonObject's own dicing resolution: divisions per edge of each
// ear-clipped triangle's barycentric grid. diceCountFor sizes it to the
// triangle's own raster-space extent and the current ShadingRate; this is
// both the cap that sizing clamps against and the fallback when no
// projection can be resolved -- the same fixed count createParametric's
// own kParametricDiceU/kParametricDiceV use for a quadric.
const RtInt kPolygonDiceN = 16;

// createParametric's own dicing resolution: divisions along u and v of the
// vertex and face grid every quadric and patch shares.
const RtInt kParametricDiceU = 16;
const RtInt kParametricDiceV = 16;

// getRSPatchMesh's own corners: RiTextureCoordinates spans a single
// parametric surface's unit square, and a PatchMesh's sub-patches already
// share one such square end to end. getRSPatchMesh passes this identity
// mapping rather than resolving RiTextureCoordinates or "s"/"t"/"st" itself
// -- see its own comment.
const GMANTextureCoordinates kIdentityCorners = {0, 0, 1, 0, 0, 1, 1, 1};

// GMANBasis::offset closes a periodic axis's wraparound with a single
// subtraction (`if (i>=nu) i-=nu`), which only lands the raw index back
// in [0,n) when that raw index stayed below 2n. A sub-patch's last
// control point sits at astart+3, and astart tops out at the final
// sub-patch's start, n-step (nbupatch-1 == n/step-1 sub-patches in), so
// the worst-case raw index offset() ever computes is n-step+3. Requiring
// that below 2n gives n > 3-step, i.e. n >= 4-step; n >= step is still
// needed so the axis has room for at least one sub-patch. Combined, the
// periodic axis's true lower bound is n >= max(step, 4-step) -- for
// step>=4 (e.g. RI_POWERSTEP) 4-step<=0, so n>=step alone already covers
// it; the max only bites at step==1 (b-spline, catmull-rom), where 4-step
// exceeds step.
bool validPeriodicBicubicMeshDim(RtInt n, RtInt step) {
  if (step < 1) {
    return false;
  }
  RtInt minN = step > (4 - step) ? step : (4 - step);
  return n >= minN && n % step == 0;
}

// A bicubic axis's control points divide into nbupatch sub-patches of
// `step` points each; GMANBasis::bicubicMesh computes that count as
// nu/uStep (periodic) or 1+(nu-4)/uStep (nonperiodic), both truncating
// integer division. Reject anything that formula would truncate: n%step
// (periodic) or (n-4)%step (nonperiodic) must be exactly zero, matching
// RISpec's own alignment rule for a PatchMesh's nu/nv, or a misaligned n
// would silently drop trailing control points and render a smaller mesh
// than the RIB asked for. n>=4 (nonperiodic) rules out the remaining case
// truncation hides there: a dimension too small for even one sub-patch,
// which the same integer division can otherwise round up to nbupatch>=1
// and read past the n points that actually exist. The periodic axis needs
// a second, tighter bound below -- see validPeriodicBicubicMeshDim.
bool validBicubicMeshDim(RtInt n, bool periodic, RtInt step) {
  if (periodic) {
    return validPeriodicBicubicMeshDim(n, step);
  }
  if (step < 1) {
    return false;
  }
  return n >= 4 && (n - 4) % step == 0;
}

// A bilinear sub-patch is one 2x2 block of corners; either axis needs at
// least two distinct control points to form one, wrapped or not.
bool validBilinearMeshDim(RtInt n) { return n >= 2; }

// opt's camera-to-world, or identity when opt is null (before RiWorldBegin,
// or a caller with no options at all) -- gman::shade's own cameraToWorld
// argument, resolved once per primitive rather than once per vertex.
GMANMatrix4 cameraToWorldOf(GMANOptions const* opt) { return opt ? opt->getCameraToWorld() : GMANMatrix4(); }

// buildPolygonObject's own per-call dicing inputs: a viewing system built
// from the same resolved projection RiWorldBegin's own uses (null when opt
// is null -- a direct caller with no options at all, which diceCountFor
// reads as "fall back to the cap"), whether that projection is perspective
// (orthographic's projection is affine, so it never falls back on a
// corner's position), and the current ShadingRate.
struct RasterProjection {
  std::unique_ptr<GMANViewingSystem> projector;
  bool perspective;
  RtFloat shadingRate;
};

// Resolved once per getRSPolygonMesh call, not once per ear-clipped
// triangle: the projection and ShadingRate are constant across every face
// one such call dices. worldToCamera is left identity -- diceCountFor only
// ever calls project(), which reads none of a GMANViewingSystem's
// world-to-camera or camera-to-world state.
RasterProjection rasterProjectionFor(GMANOptions const* opt, GMANAttributes const* attr) {
  RtFloat const shadingRate = attr->getShadingRate();
  if (!opt) {
    return RasterProjection{nullptr, false, shadingRate};
  }
  // standardDictionary(), not the renderer's own dictionary: no getRS*
  // signature carries one in, and none may change to add it. "fov"
  // resolves the same as RiWorldBegin's unless a scene redeclares it,
  // which changes the dicing granularity, and so the shading
  // resolution, never coverage.
  gman::ViewingSystemInputs const vsi = gman::resolveViewingSystemInputs(*opt, gman::standardDictionary());
  bool const perspective = vsi.projectionName != "orthographic";
  GMANMatrix4 const worldToCamera;
  std::unique_ptr<GMANViewingSystem> projector;
  if (perspective) {
    projector = std::make_unique<gman::VSPerspective>(vsi.xres, vsi.yres, vsi.screenWindow, worldToCamera, vsi.fov,
                                                      vsi.clipping.nearDist, vsi.clipping.farDist);
  } else {
    projector = std::make_unique<gman::VSOrthographic>(vsi.xres, vsi.yres, vsi.screenWindow, worldToCamera,
                                                       vsi.clipping.nearDist, vsi.clipping.farDist);
  }
  return RasterProjection{std::move(projector), perspective, shadingRate};
}

// The raster-space distance between two of project()'s own return points --
// only their x, y are meaningful (see GMANViewingSystem::project).
RtFloat rasterDistance(GMANPoint const& a, GMANPoint const& b) {
  RtFloat const dx = a.getX() - b.getX();
  RtFloat const dy = a.getY() - b.getY();
  return std::sqrt(dx * dx + dy * dy);
}

// One ear-clipped triangle's own divisions per edge: clamp(ceil(L /
// sqrt(ShadingRate)), 1, kPolygonDiceN), L the longest of its three
// raster-space edges. Falls back to the cap -- today's fixed behaviour --
// whenever the rule cannot be evaluated: no projection resolved, a
// non-positive ShadingRate, a non-finite L, or (perspective only) a corner
// at or behind the eye, where a projection is meaningless.
RtInt diceCountFor(GMANPoint const& p0, GMANPoint const& p1, GMANPoint const& p2, RasterProjection const& dicing) {
  if (!dicing.projector || !(dicing.shadingRate > (RtFloat)0.0)) {
    return kPolygonDiceN;
  }
  if (dicing.perspective && (p0.getZ() <= (RtFloat)0.0 || p1.getZ() <= (RtFloat)0.0 || p2.getZ() <= (RtFloat)0.0)) {
    return kPolygonDiceN;
  }

  GMANPoint const r0 = dicing.projector->project(p0);
  GMANPoint const r1 = dicing.projector->project(p1);
  GMANPoint const r2 = dicing.projector->project(p2);
  RtFloat const e01 = rasterDistance(r0, r1);
  RtFloat const e12 = rasterDistance(r1, r2);
  RtFloat const e20 = rasterDistance(r2, r0);
  // Each edge is checked on its own: a NaN or infinite edge fails every ">"
  // comparison, so folding it into a running max the way a finite edge
  // would silently drops it instead of forcing the fallback below.
  if (!std::isfinite(e01) || !std::isfinite(e12) || !std::isfinite(e20)) {
    return kPolygonDiceN;
  }
  RtFloat L = e01;
  if (e12 > L) {
    L = e12;
  }
  if (e20 > L) {
    L = e20;
  }

  // Clamped in real (double) arithmetic before ever becoming an RtInt: L
  // can be finite and still project past what an int can hold (a huge
  // scale, or a screen window narrow enough to blow up the raster scale),
  // and converting an out-of-range double to int is undefined behaviour.
  double n = std::ceil((double)L / std::sqrt((double)dicing.shadingRate));
  if (n < 1.0) {
    n = 1.0;
  }
  if (n > (double)kPolygonDiceN) {
    n = (double)kPolygonDiceN;
  }
  return (RtInt)n;
}

// A parametric surface's four corner texture coordinates (RISpec 3.2's
// RiTextureCoordinates), resolved in precedence order, most specific last so
// it wins: attr's corners (the RiTextureCoordinates default, s=u/t=v unless
// the scene called it), then "st" (eight varying values, s/t per corner in
// RISpec corner order: (0,0), (1,0), (0,1), (1,1)), then "s"/"t" (four
// varying values each, overriding only their own component). Shared by every
// quadric and getRSPatch; getRSPatchMesh does not call this (see its own
// comment).
GMANTextureCoordinates resolveParametricCorners(GMANParameterList& pl, GMANAttributes* attr) {
  GMANTextureCoordinates corners = attr->getTextureCoordinates();

  RtFloat* st = gman::floatArray(pl, RI_ST);
  if (st) {
    corners.s1 = st[0];
    corners.t1 = st[1];
    corners.s2 = st[2];
    corners.t2 = st[3];
    corners.s3 = st[4];
    corners.t3 = st[5];
    corners.s4 = st[6];
    corners.t4 = st[7];
  }
  RtFloat* s = gman::floatArray(pl, RI_S);
  if (s) {
    corners.s1 = s[0];
    corners.s2 = s[1];
    corners.s3 = s[2];
    corners.s4 = s[3];
  }
  RtFloat* tp = gman::floatArray(pl, RI_T);
  if (tp) {
    corners.t1 = tp[0];
    corners.t2 = tp[1];
    corners.t3 = tp[2];
    corners.t4 = tp[3];
  }
  return corners;
}

// The bilinear interpolation RiTextureCoordinates' own corner rule spells
// out: corner order (0,0), (1,0), (0,1), (1,1), the same order
// GMANTextureCoordinates' s1..t4 already carry.
RtFloat bilerpCorner(double u, double v, RtFloat c00, RtFloat c10, RtFloat c01, RtFloat c11) {
  return (RtFloat)((1.0 - u) * (1.0 - v) * c00 + u * (1.0 - v) * c10 + (1.0 - u) * v * c01 + u * v * c11);
}

// The gman::SurfacePoint at one vertex, in camera space -- this is what
// "shade per vertex and let [Gouraud interpolation] interpolate" means in
// practice: a clip-introduced vertex has no u,v of its own to shade with,
// but it does get a color, because GMANClipEdge::intersect already
// interpolates GMANVertex::color across a clipped edge (the same machinery
// wired up for the vertex alpha blend). Eye sits at the
// camera-space origin (gman::VSPerspective::ray), so the incident
// direction is just the normalized surface point.
gman::SurfacePoint vertexSurfacePoint(GMANPoint const& location, GMANNormal const& normal, RtFloat u, RtFloat v,
                                      RtFloat s, RtFloat t) {
  gman::SurfacePoint point;
  point.P = location;
  point.N = normal;
  point.Ng = normal; // no displacement this phase; the two never diverge
  point.I = GMANVector(location.getX(), location.getY(), location.getZ());
  point.I.normalize();
  point.E = GMANPoint(0.0, 0.0, 0.0);
  point.u = u;
  point.v = v;
  point.s = s;
  point.t = t;
  return point;
}

// A shaded vertex's Oi, read as the GMANAlpha a vertex's alpha channel
// stores: both share GMANColorBase's own floating-point sample type, so
// this is a plain repackaging of the same three numbers.
GMANAlpha vertexAlpha(GMANColor const& oi) { return GMANAlpha(oi.getRed(), oi.getGreen(), oi.getBlue()); }

// The sine of the angle between a and b, judged against normal: a.cross(b)
// is an area (units of length squared); dividing by |a|*|b| turns it into
// a dimensionless quantity in [-1, 1] regardless of either vector's own
// scale, so a caller can compare it against a fixed tolerance wherever
// the geometry sits. normal must already be unit length -- the sine
// identity depends on it. A zero-length a or b would divide to NaN, which
// fails every comparison; returning 0 reads instead as the angle a
// vanishing vector cannot have a turn or a side of, which is what both
// callers below already treat a zero-length input as.
RtFloat dimensionlessCross(GMANVector a, GMANVector b, const GMANVector& normal) {
  RtFloat lenA = a.magnitude();
  RtFloat lenB = b.magnitude();
  if (lenA == (RtFloat)0.0 || lenB == (RtFloat)0.0) {
    return (RtFloat)0.0;
  }
  return a.cross(b).dot(normal) / (lenA * lenB);
}

// A ring vertex's turning direction relative to the polygon's own normal:
// positive is convex, negative reflex, zero for a collinear or duplicate
// vertex. Testing against this normal, rather than against the ring's own
// winding, keeps the result correct whichever way the ring winds --
// normal already followed that winding when Newell's method built it.
RtFloat turnOrientation(const GMANPoint& prev, const GMANPoint& cur, const GMANPoint& next, const GMANVector& normal) {
  return dimensionlessCross(GMANVector(prev, cur), GMANVector(cur, next), normal);
}

// The sine of the angle between one triangle edge and the vector from
// that edge's start to p, the same dimensionless quantity turnOrientation
// returns, so a boundary case reads the same near-zero value wherever the
// triangle sits, at any scale or rotation. p that lands exactly on the
// line through the edge -- the common case for a ring vertex bridged by a
// diagonal of its own polygon -- gives exactly 0 in exact arithmetic;
// comparing that against literal 0 instead of a tolerance band lets
// rounding alone decide which side p falls on.
RtFloat sideOf(const GMANPoint& from, const GMANPoint& to, const GMANPoint& p, const GMANVector& normal) {
  return dimensionlessCross(GMANVector(from, to), GMANVector(from, p), normal);
}

// True when p lies inside or on the boundary of coplanar triangle
// (a, b, c): on the same side of every edge, judged by sideOf, so the
// test does not depend on which way the triangle happens to wind.
bool pointInTriangle(const GMANPoint& a, const GMANPoint& b, const GMANPoint& c, const GMANPoint& p,
                     const GMANVector& normal) {
  RtFloat d0 = sideOf(a, b, p, normal);
  RtFloat d1 = sideOf(b, c, p, normal);
  RtFloat d2 = sideOf(c, a, p, normal);
  bool hasNeg = d0 < -kTriangulationTolerance || d1 < -kTriangulationTolerance || d2 < -kTriangulationTolerance;
  bool hasPos = d0 > kTriangulationTolerance || d1 > kTriangulationTolerance || d2 > kTriangulationTolerance;
  return !(hasNeg && hasPos);
}

// The position in remaining to clip next: the first whose own orient is
// non-reflex and either degenerate (a zero-area "ear" needs no containment
// test -- clipping it changes neither the polygon's shape nor its area, and
// skipping the test is what keeps a run of such vertices from stalling the
// caller's loop) or whose own candidate triangle holds no vertex of reflex
// besides its own three corners. Falls back to the position holding the
// largest orient when no candidate qualifies -- only reachable from
// malformed (self-intersecting) input, since a simple polygon always has a
// true ear; clipping the least-reflex candidate anyway degrades rather than
// hangs.
//
// Only a reflex vertex can lie inside a convex ear's triangle -- a
// standard property of simple polygons -- so each candidate's containment
// test runs against reflex alone, not every remaining position.
std::size_t findEar(std::vector<GMANPoint> const& ring, std::vector<RtInt> const& remaining,
                    std::vector<RtFloat> const& orient, std::vector<RtInt> const& reflex, GMANVector const& normal) {
  const std::size_t m = remaining.size();
  std::size_t fallbackAt = 0;
  RtFloat fallbackOrient = orient[0];
  for (std::size_t i = 0; i < m; i++) {
    if (orient[i] > fallbackOrient) {
      fallbackOrient = orient[i];
      fallbackAt = i;
    }
    if (orient[i] < -kTriangulationTolerance) {
      continue; // reflex: never an ear
    }
    const RtInt iPrev = remaining[(i + m - 1) % m];
    const RtInt iCur = remaining[i];
    const RtInt iNext = remaining[(i + 1) % m];

    bool degenerate = orient[i] <= kTriangulationTolerance;
    bool containsReflex = false;
    for (std::vector<RtInt>::const_iterator it = reflex.begin(); !degenerate && !containsReflex && it != reflex.end();
         ++it) {
      RtInt idx = *it;
      if (idx == iPrev || idx == iCur || idx == iNext) {
        continue;
      }
      containsReflex = pointInTriangle(ring[iPrev], ring[iCur], ring[iNext], ring[idx], normal);
    }
    if (degenerate || !containsReflex) {
      return i;
    }
  }
  return fallbackAt;
}

// Ear clipping over a vertex ring: triangulates any simple planar polygon,
// concave included, into exactly ring.size() - 2 triangles. GeneralPolygon
// bridges each hole into the outer loop (bridgeHoles) and hands the
// combined ring to this same function, so it takes points and a normal
// rather than assuming any particular caller's vertex storage; the
// returned triples index into that same ring. Each pass computes every
// remaining position's own orientation and reflex set, then findEar picks
// the position to clip.
std::vector<std::array<RtInt, 3>> triangulateEarClipping(const std::vector<GMANPoint>& ring, const GMANVector& normal) {
  std::vector<std::array<RtInt, 3>> triangles;
  const RtInt n = (RtInt)ring.size();
  if (n < 3) {
    return triangles;
  }
  triangles.reserve(n - 2);

  std::vector<RtInt> remaining(n);
  for (RtInt i = 0; i < n; i++) {
    remaining[i] = i;
  }

  for (std::size_t m = remaining.size(); m > 3; m = remaining.size()) {
    std::vector<RtInt> reflex;
    std::vector<RtFloat> orient(m);
    for (std::size_t i = 0; i < m; i++) {
      const RtInt iPrev = remaining[(i + m - 1) % m];
      const RtInt iCur = remaining[i];
      const RtInt iNext = remaining[(i + 1) % m];
      orient[i] = turnOrientation(ring[iPrev], ring[iCur], ring[iNext], normal);
      if (orient[i] < -kTriangulationTolerance) {
        reflex.push_back(iCur);
      }
    }

    const std::size_t clipAt = findEar(ring, remaining, orient, reflex, normal);

    const RtInt iPrev = remaining[(clipAt + m - 1) % m];
    const RtInt iCur = remaining[clipAt];
    const RtInt iNext = remaining[(clipAt + 1) % m];
    triangles.push_back({iPrev, iCur, iNext});
    remaining.erase(remaining.begin() + clipAt);
  }

  triangles.push_back({remaining[0], remaining[1], remaining[2]});
  return triangles;
}

// A Polygon/GeneralPolygon vertex's u, v, s and t. u and v are its
// object-space x, y unconditionally -- the RISpec gives a non-parametric
// primitive's surface parameters as its own x, y, with no override -- and s,
// t default to that same x, y and take the highest-precedence varying value
// supplied instead: "st" first, then "s"/"t" each overriding only its own
// component. RiTextureCoordinates does not apply to a polygon (RISpec 3.2);
// getRSPolygonMesh never resolves it.
struct PolygonVertexTexCoord {
  RtFloat u, v, s, t;
};

// p is the vertex's own flat "P" array (3 floats per vertex, object space,
// before the CTM); nverts is also "s"/"t"/"st"'s own declared length, so
// index i reads the same vertex from every one of them. s and t come from
// polygonTexCoords, gman's one s/t rule.
std::vector<PolygonVertexTexCoord> resolvePolygonTextureCoordinates(GMANParameterList const& pl, RtInt nverts,
                                                                    RtFloat const* p) {
  std::vector<std::pair<RtFloat, RtFloat>> const st = gman::polygonTexCoords(pl, p, (std::size_t)nverts);

  std::vector<PolygonVertexTexCoord> coords(nverts);
  for (RtInt i = 0; i < nverts; i++) {
    coords[i] = {p[3 * i], p[3 * i + 1], st[i].first, st[i].second};
  }
  return coords;
}

// Row-major index into one ear-clipped triangle's own (n+1)(n+2)/2-point
// barycentric grid: row a (0..n) holds n-a+1 points (b = 0..n-a), so row a
// starts right after every shorter row before it.
RtInt dicedGridIndex(RtInt a, RtInt b, RtInt n) { return a * (n + 1) - a * (a - 1) / 2 + b; }

// Shades vertex at its own stored location (the caller sets that first)
// with normal and (u, v, s, t), through vertexSurfacePoint and gman::shade.
// Stores the result's Ci and Oi (as a GMANAlpha) and touches nothing else.
void shadeVertex(GMANVertex& vertex, GMANNormal const& normal, RtFloat u, RtFloat v, RtFloat s, RtFloat t,
                 gman::Appearance const& appearance, GMANMatrix4 const& cameraToWorld) {
  gman::SurfacePoint const point = vertexSurfacePoint(vertex.getLocation(), normal, u, v, s, t);
  gman::Shading const shading = gman::shade(appearance, point, cameraToWorld);
  vertex.setColor(shading.Ci);
  vertex.setColor(vertexAlpha(shading.Oi));
}

// Appends one new GMANFace over corners on surface to faces, after
// calcNormal(), setSides(sides) and setOrientation(orientation), in that
// order. Contrast appendFace: this adds a single face to a face list;
// appendFace joins a whole face's already-built body and vertex chains onto
// a mesh.
void addFace(std::vector<GMANFace*>& faces, GMANVertex* corners[GMAN_NFACE_VERTS], GMANSurface* surface, RtInt sides,
             RtToken orientation) {
  GMANFace* face = new GMANFace(corners, surface);
  face->calcNormal();
  face->setSides(sides);
  face->setOrientation(orientation);
  faces.push_back(face);
}

// One new, freshly shaded grid vertex at barycentric weights (w0, w1, w2)
// against a diced triangle's own three corners -- P, u, v, s and t each the
// same affine combination of those three corners' own values. The shading
// normal is not interpolated: normalVec is already the one planar normal
// every vertex of a Polygon/GeneralPolygon shares.
GMANVertex* dicedGridVertex(GMANPoint const& p0, GMANPoint const& p1, GMANPoint const& p2,
                            PolygonVertexTexCoord const& tc0, PolygonVertexTexCoord const& tc1,
                            PolygonVertexTexCoord const& tc2, RtFloat w0, RtFloat w1, RtFloat w2,
                            GMANNormal const& normal, GMANVector const& normalVec, gman::Appearance const& appearance,
                            GMANMatrix4 const& cameraToWorld) {
  GMANVertex* vertex = new GMANVertex();
  vertex->setLocation(p0 * w0 + p1 * w1 + p2 * w2);
  vertex->setNormal(normalVec);

  const RtFloat u = tc0.u * w0 + tc1.u * w1 + tc2.u * w2;
  const RtFloat v = tc0.v * w0 + tc1.v * w1 + tc2.v * w2;
  const RtFloat s = tc0.s * w0 + tc1.s * w1 + tc2.s * w2;
  const RtFloat t = tc0.t * w0 + tc1.t * w1 + tc2.t * w2;
  shadeVertex(*vertex, normal, u, v, s, t, appearance, cameraToWorld);
  return vertex;
}

// Dices one ear-clipped triangle (corners i0, i1, i2, indexing
// vertexLocations/texCoords/vertices) into its own n-per-edge barycentric
// grid -- n the caller's own diceCountFor result for this triangle --
// appending every newly shaded interior or edge-interior vertex to vertices
// and every sub-triangle's face to faceList. Reuses the triangle's three
// corners' own GMANVertex objects unchanged at the grid's three corners
// rather than duplicating them; two ear-clipped triangles never share a
// diced grid vertex, even along a common diagonal -- each dices
// independently.
void dicePolygonTriangle(RtInt i0, RtInt i1, RtInt i2, std::vector<GMANPoint> const& vertexLocations,
                         std::vector<PolygonVertexTexCoord> const& texCoords, GMANNormal const& normal,
                         GMANVector const& normalVec, gman::Appearance const& appearance,
                         GMANMatrix4 const& cameraToWorld, RtInt sides, RtToken orientation, GMANSurface* surface,
                         std::vector<GMANVertex*>& vertices, std::vector<GMANFace*>& faceList, RtInt n) {
  GMANPoint const& p0 = vertexLocations[i0];
  GMANPoint const& p1 = vertexLocations[i1];
  GMANPoint const& p2 = vertexLocations[i2];
  PolygonVertexTexCoord const& tc0 = texCoords[i0];
  PolygonVertexTexCoord const& tc1 = texCoords[i1];
  PolygonVertexTexCoord const& tc2 = texCoords[i2];

  std::vector<GMANVertex*> grid((n + 1) * (n + 2) / 2);
  for (RtInt a = 0; a <= n; a++) {
    for (RtInt b = 0; b <= n - a; b++) {
      const RtInt idx = dicedGridIndex(a, b, n);
      if (a == 0 && b == 0) {
        grid[idx] = vertices[i0];
      } else if (a == n && b == 0) {
        grid[idx] = vertices[i1];
      } else if (a == 0 && b == n) {
        grid[idx] = vertices[i2];
      } else {
        const RtFloat w1 = (RtFloat)a / (RtFloat)n;
        const RtFloat w2 = (RtFloat)b / (RtFloat)n;
        const RtFloat w0 = (RtFloat)1.0 - w1 - w2;
        grid[idx] =
            dicedGridVertex(p0, p1, p2, tc0, tc1, tc2, w0, w1, w2, normal, normalVec, appearance, cameraToWorld);
        vertices.push_back(grid[idx]);
      }
    }
  }

  // Splits each unit cell into an "up" triangle (a,b), (a+1,b), (a,b+1)
  // and, unless it is the row's last cell, a "down" triangle (a+1,b),
  // (a+1,b+1), (a,b+1) -- the standard triangulated grid, n*n sub-triangles
  // total. GMANFace's fixed 4-vertex shape repeats the 3rd slot in the 4th,
  // the same idiom every quadric's pole face already uses for a
  // degenerate triangle.
  for (RtInt a = 0; a < n; a++) {
    for (RtInt b = 0; b < n - a; b++) {
      GMANVertex* up[4];
      up[0] = grid[dicedGridIndex(a, b, n)];
      up[1] = grid[dicedGridIndex(a + 1, b, n)];
      up[2] = grid[dicedGridIndex(a, b + 1, n)];
      up[3] = up[2];
      addFace(faceList, up, surface, sides, orientation);

      if (b < n - a - 1) {
        GMANVertex* down[4];
        down[0] = grid[dicedGridIndex(a + 1, b, n)];
        down[1] = grid[dicedGridIndex(a + 1, b + 1, n)];
        down[2] = grid[dicedGridIndex(a, b + 1, n)];
        down[3] = down[2];
        addFace(faceList, down, surface, sides, orientation);
      }
    }
  }
}

// One polygon face's own body and vertex chain, handed on as buildFace's
// out-parameters pass them. FaceChains itself owns nothing: buildFace hands
// body and vertRoot to its own caller, and that caller's GMANObject frees
// both -- GMANBody's destructor already walks and deletes its surface
// chain, and that surface's own destructor deletes the face chain it holds.
struct FaceChains {
  GMANBody* body;       // the face's body; its surface holds the face chain
  GMANVertex* vertRoot; // the head of the face's vertex chain
};

// buildFace's own tail, one face at a time: one GMANVertex per entry in
// vertexLocations, triangulated over ring (which may repeat an
// entry at a bridge -- triangulateEarClipping's own comment already covers
// the resulting duplicate position and zero-area corner), each ear-clipped
// triangle then diced and shaded across its own face (dicePolygonTriangle).
// ring indexes vertexLocations rather than a face's own vertex array, so the
// two ring slots a bridge duplicates share one GMANVertex and one shaded
// colour instead of splitting the surface. texCoords is index-aligned with
// vertexLocations, not with ring. Returns the face's body and its vertex
// chain's head, as FaceChains.
FaceChains buildPolygonObject(std::vector<GMANPoint> const& vertexLocations, std::vector<RtInt> const& ring,
                              GMANVector const& normalVec, RtInt sides, RtToken orientation,
                              gman::Appearance const& appearance, GMANMatrix4 const& cameraToWorld,
                              std::vector<PolygonVertexTexCoord> const& texCoords, RasterProjection const& dicing) {
  GMANNormal normal(normalVec.getX(), normalVec.getY(), normalVec.getZ());

  GMANBody* body = new GMANBody(GMANColor(), GMANColor());
  GMANSurface* surface = new GMANSurface(body);
  body->setSurface(surface);

  const RtInt nverts = (RtInt)vertexLocations.size();
  std::vector<GMANVertex*> vertices(nverts);
  for (RtInt i = 0; i < nverts; i++) {
    vertices[i] = new GMANVertex();
    vertices[i]->setLocation(vertexLocations[i]);
    vertices[i]->setNormal(normalVec);

    PolygonVertexTexCoord const& tc = texCoords[i];
    shadeVertex(*vertices[i], normal, tc.u, tc.v, tc.s, tc.t, appearance, cameraToWorld);
  }

  std::vector<GMANPoint> ringPoints(ring.size());
  for (std::size_t i = 0; i < ring.size(); i++) {
    ringPoints[i] = vertexLocations[ring[i]];
  }
  std::vector<std::array<RtInt, 3>> triangles = triangulateEarClipping(ringPoints, normalVec);

  // Each triangle's own n, found before any dicing so faceList reserves
  // its exact total instead of every triangle's worst case at the cap.
  std::vector<RtInt> diceCounts(triangles.size());
  std::size_t totalFaces = 0;
  for (std::size_t i = 0; i < triangles.size(); i++) {
    std::array<RtInt, 3> const& tri = triangles[i];
    diceCounts[i] = diceCountFor(vertexLocations[ring[tri[0]]], vertexLocations[ring[tri[1]]],
                                 vertexLocations[ring[tri[2]]], dicing);
    totalFaces += (std::size_t)diceCounts[i] * (std::size_t)diceCounts[i];
  }

  std::vector<GMANFace*> faceList;
  faceList.reserve(totalFaces);
  for (std::size_t i = 0; i < triangles.size(); i++) {
    std::array<RtInt, 3> const& tri = triangles[i];
    dicePolygonTriangle(ring[tri[0]], ring[tri[1]], ring[tri[2]], vertexLocations, texCoords, normal, normalVec,
                        appearance, cameraToWorld, sides, orientation, surface, vertices, faceList, diceCounts[i]);
  }

  for (std::size_t i = 0; i + 1 < vertices.size(); i++) {
    vertices[i]->setNext(vertices[i + 1]);
  }
  for (std::size_t i = 0; i + 1 < faceList.size(); i++) {
    faceList[i]->setNext(faceList[i + 1]);
  }
  surface->setFace(faceList.empty() ? NULL : faceList[0]);

  return FaceChains{body, vertices.empty() ? NULL : vertices[0]};
}

// Whether the ray from ring vertex v toward target enters the ring's
// interior, given v's own incoming (prev->v) and outgoing (v->next) edges.
// A convex v (interior angle < 180) puts the interior wedge in the
// intersection of both edges' left half-planes; a reflex v (interior angle
// > 180) puts it in their union -- the same duality pointInTriangle's own
// AND/OR split needs for a bounded triangle rather than an open wedge.
// bridgeHoles' own use: which of a bridge vertex's two ring occurrences to
// bridge to, since only one has the hole's rightmost vertex inside its
// wedge.
bool inInteriorWedge(const GMANPoint& v, const GMANPoint& prev, const GMANPoint& next, const GMANPoint& target,
                     const GMANVector& normal) {
  bool convex = turnOrientation(prev, v, next, normal) > -kTriangulationTolerance;
  bool leftOfIncoming = sideOf(prev, v, target, normal) > -kTriangulationTolerance;
  bool leftOfOutgoing = sideOf(v, next, target, normal) > -kTriangulationTolerance;
  return convex ? (leftOfIncoming && leftOfOutgoing) : (leftOfIncoming || leftOfOutgoing);
}

// The outer loop's own (u, v) frame every bridging computation below is
// judged in: origin the outer loop's own first vertex, vDir = normal x
// uDir -- outerFrame's own comment covers how uDir is chosen. projU and
// projV give a point's own coordinate along each axis, measured from
// origin -- so the bridges a rotated or rescaled placement of the same
// polygon produces are the same bridges, not an artifact of whichever way
// a fixed world axis happened to point.
struct PlanarFrame {
  GMANPoint origin;
  GMANVector uDir;
  GMANVector vDir;
  RtFloat projU(GMANPoint const& pt) const { return GMANVector(origin, pt).dot(uDir); }
  RtFloat projV(GMANPoint const& pt) const { return GMANVector(origin, pt).dot(vDir); }
};

// A hole's own points stay in "P" order until it is actually bridged (see
// spliceHole): assigning ids and appending to vertexPositions before
// knowing whether the hole's ray meets the ring at all would strand an
// unused GMANVertex for a dropped hole -- one no triangle ever references,
// failing the coverage property every kept vertex must satisfy.
struct Hole {
  std::vector<GMANPoint> points;
  std::vector<RtInt> slots;
  bool reversed;
  RtInt rightmostLocal;
  RtFloat rightmostU;
  RtInt loopIndex;
};

// The outer loop's own frame: uDir is the first edge of non-zero length, so
// the frame rotates with the polygon rather than sitting on a fixed world
// axis a rotated placement would then bridge differently. The longest edge
// seen is kept as a fallback only for an outer loop whose every edge falls
// under the tolerance floor relative to its own bounding box --
// unreachable once the caller's own degeneracy guard has passed, since
// that guard already requires a non-negligible area, but cheap insurance
// against a zero uDir all the same.
PlanarFrame outerFrame(std::vector<GMANPoint> const& outer, GMANVector const& normalVec, RtFloat outerBboxSide) {
  GMANVector uDir;
  bool foundEdge = false;
  RtFloat longestEdge = (RtFloat)0.0;
  for (std::size_t i = 0; i < outer.size() && !foundEdge; i++) {
    GMANVector edge(outer[i], outer[(i + 1) % outer.size()]);
    RtFloat len = edge.magnitude();
    if (len > kTriangulationTolerance * outerBboxSide) {
      uDir = edge;
      uDir /= len;
      foundEdge = true;
    } else if (len > longestEdge) {
      longestEdge = len;
      uDir = edge;
    }
  }
  if (!foundEdge && longestEdge > (RtFloat)0.0) {
    uDir /= longestEdge;
  }
  GMANVector vDir = normalVec.cross(uDir);
  return PlanarFrame{outer[0], uDir, vDir};
}

// One Hole for each loop after loops[0] that gman::isDegeneratePolygon
// does not reject, sorted by descending rightmostU (rule 1: a ray cast
// from a not-yet-bridged hole's rightmost vertex can then only meet the
// boundary or a hole already bridged -- never one still to come, which it
// could otherwise cross), ties by ascending loopIndex.
std::vector<Hole> collectHoles(std::vector<std::vector<GMANPoint>> const& loops,
                               std::vector<std::vector<RtInt>> const& loopSlots, GMANVector const& normalVec,
                               PlanarFrame const& frame) {
  std::vector<Hole> holes;

  for (std::size_t loopIndex = 1; loopIndex < loops.size(); loopIndex++) {
    const std::vector<GMANPoint>& loop = loops[loopIndex];
    if (gman::isDegeneratePolygon(loop)) {
      continue; // too few points, or degenerate against its own extent: dropped
    }
    GMANVector holeNewell = gman::newellNormal(loop);

    Hole hole;
    hole.loopIndex = (RtInt)loopIndex;
    hole.points = loop;
    hole.slots = loopSlots[loopIndex];
    // A hole wound the same way as the outer loop would add its area
    // instead of removing it; reversing its traversal order in spliceHole
    // is what turns the bridge into a cut.
    hole.reversed = holeNewell.dot(normalVec) > (RtFloat)0.0;

    hole.rightmostLocal = 0;
    hole.rightmostU = frame.projU(loop[0]);
    for (std::size_t j = 1; j < loop.size(); j++) {
      RtFloat u = frame.projU(loop[j]);
      if (u > hole.rightmostU) {
        hole.rightmostU = u;
        hole.rightmostLocal = (RtInt)j;
      }
    }
    holes.push_back(hole);
  }

  std::sort(holes.begin(), holes.end(), [](const Hole& a, const Hole& b) {
    if (a.rightmostU != b.rightmostU) {
      return a.rightmostU > b.rightmostU;
    }
    return a.loopIndex < b.loopIndex;
  });
  return holes;
}

// Rule 2: the ring position e whose edge, ring[e] to ring[(e + 1) %
// size], the +u ray from m crosses nearest -- the smallest u strictly
// greater than m's own. -1 when the ray crosses no ring edge.
RtInt nearestCrossedEdge(std::vector<GMANPoint> const& vertexPositions, std::vector<RtInt> const& ring,
                         PlanarFrame const& frame, GMANPoint const& m) {
  const RtFloat mu = frame.projU(m);
  const RtFloat mv = frame.projV(m);
  RtInt edgeStart = -1;
  RtFloat nearestU = 0;
  const RtInt ringSize = (RtInt)ring.size();
  for (RtInt e = 0; e < ringSize; e++) {
    const GMANPoint& a = vertexPositions[ring[e]];
    const GMANPoint& b = vertexPositions[ring[(e + 1) % ringSize]];
    RtFloat av = frame.projV(a), bv = frame.projV(b);
    if ((av > mv) == (bv > mv)) {
      continue; // does not cross the ray's line
    }
    RtFloat au = frame.projU(a), bu = frame.projU(b);
    RtFloat crossU = au + (mv - av) / (bv - av) * (bu - au);
    if (crossU <= mu) {
      continue; // behind the ray's origin
    }
    if (edgeStart < 0 || crossU < nearestU) {
      edgeStart = e;
      nearestU = crossU;
    }
  }
  return edgeStart;
}

// The vertex id, an index into vertexPositions, that m bridges to, given
// the ring edge (edgeStart) its +u ray crosses. When the crossing point
// lies within kTriangulationTolerance * outerBboxSide of that edge's
// start, then of its end, that endpoint. Otherwise the reflex ring vertex
// inside triangle (m, crossing point, P) -- the crossed edge's own
// endpoints excluded -- with the largest cosine to uDir, the nearer on an
// equal cosine: bridging straight to P past such a vertex would cross it.
// Otherwise P itself: the crossed edge's endpoint with the larger u, the
// end on a tie.
RtInt chooseBridgeTarget(std::vector<GMANPoint> const& vertexPositions, std::vector<RtInt> const& ring, RtInt edgeStart,
                         GMANPoint const& m, PlanarFrame const& frame, GMANVector const& normalVec,
                         RtFloat outerBboxSide) {
  const RtInt ringSize = (RtInt)ring.size();
  const RtInt aId = ring[edgeStart];
  const RtInt bId = ring[(edgeStart + 1) % ringSize];
  const GMANPoint& a = vertexPositions[aId];
  const GMANPoint& b = vertexPositions[bId];
  const RtInt pId = (frame.projU(a) > frame.projU(b)) ? aId : bId;
  const RtFloat mv = frame.projV(m);
  const RtFloat tParam = (mv - frame.projV(a)) / (frame.projV(b) - frame.projV(a));
  const GMANPoint iPoint = a + (GMANPoint)(GMANVector(a, b) * tParam);

  const RtFloat coincideTol = kTriangulationTolerance * outerBboxSide;
  GMANVector distToA(iPoint, a);
  GMANVector distToB(iPoint, b);
  if (distToA.magnitude() <= coincideTol) {
    return aId;
  }
  if (distToB.magnitude() <= coincideTol) {
    return bId;
  }

  const GMANPoint& pPoint = vertexPositions[pId];
  RtInt best = -1;
  RtFloat bestCos = 0;
  RtFloat bestDist = 0;
  for (RtInt e = 0; e < ringSize; e++) {
    RtInt vId = ring[e];
    if (vId == aId || vId == bId) {
      continue; // the crossed edge's own endpoints, already considered
    }
    const GMANPoint& v = vertexPositions[vId];
    const GMANPoint& prev = vertexPositions[ring[(e + ringSize - 1) % ringSize]];
    const GMANPoint& next = vertexPositions[ring[(e + 1) % ringSize]];
    if (turnOrientation(prev, v, next, normalVec) >= -kTriangulationTolerance) {
      continue; // only a reflex vertex can lie inside a visibility ear
    }
    if (!pointInTriangle(m, iPoint, pPoint, v, normalVec)) {
      continue;
    }
    GMANVector toV(m, v);
    RtFloat dist = toV.magnitude();
    if (dist == (RtFloat)0.0) {
      continue;
    }
    RtFloat cosAngle = toV.dot(frame.uDir) / dist;
    if (best < 0 || cosAngle > bestCos || (cosAngle == bestCos && dist < bestDist)) {
      best = vId;
      bestCos = cosAngle;
      bestDist = dist;
    }
  }
  return (best >= 0) ? best : pId;
}

// Rule 3: target may already occur twice in the ring (a previous hole's
// own bridge point); the wrong occurrence's wedge does not contain m, and
// bridging to it would cross into the wrong lobe. Returns the first ring
// position holding target whose interior wedge (inInteriorWedge) contains
// m, or the first position holding target when none does.
RtInt chooseBridgeSlot(std::vector<GMANPoint> const& vertexPositions, std::vector<RtInt> const& ring, RtInt target,
                       GMANPoint const& m, GMANVector const& normalVec) {
  const RtInt ringSize = (RtInt)ring.size();
  RtInt targetSlot = -1;
  for (RtInt e = 0; e < ringSize; e++) {
    if (ring[e] != target) {
      continue;
    }
    if (targetSlot < 0) {
      targetSlot = e; // first occurrence: the default if none matches
    }
    const GMANPoint& v = vertexPositions[ring[e]];
    const GMANPoint& prev = vertexPositions[ring[(e + ringSize - 1) % ringSize]];
    const GMANPoint& next = vertexPositions[ring[(e + 1) % ringSize]];
    if (inInteriorWedge(v, prev, next, m, normalVec)) {
      targetSlot = e;
      break;
    }
  }
  return targetSlot;
}

// Commits hole into the ring at targetSlot: its vertices get ids (from the
// old vertexPositions.size() up) and join vertexPositions and vertexSlots
// only on this call, once bridging is known to succeed -- a dropped hole
// never reaches spliceHole and so never strands an id. ring becomes
// ring[0..targetSlot], the hole from its rightmost vertex (traversed
// backwards when reversed), that vertex again, ring[targetSlot], then
// ring[targetSlot + 1..] -- a doubled edge, not a split surface, since
// both occurrences of ring[targetSlot] share the same GMANVertex.
void spliceHole(Hole const& hole, RtInt targetSlot, std::vector<GMANPoint>& vertexPositions,
                std::vector<RtInt>& vertexSlots, std::vector<RtInt>& ring) {
  const RtInt n = (RtInt)hole.points.size();
  const RtInt ringSize = (RtInt)ring.size();
  RtInt const bridgeTarget = ring[targetSlot];

  RtInt base = (RtInt)vertexPositions.size();
  std::vector<RtInt> ids(n);
  for (RtInt j = 0; j < n; j++) {
    ids[j] = base + j;
  }
  vertexPositions.insert(vertexPositions.end(), hole.points.begin(), hole.points.end());
  vertexSlots.insert(vertexSlots.end(), hole.slots.begin(), hole.slots.end());
  const RtInt mId = ids[hole.rightmostLocal];

  std::vector<RtInt> bridged;
  bridged.reserve(ringSize + n + 2);
  for (RtInt e = 0; e <= targetSlot; e++) {
    bridged.push_back(ring[e]);
  }
  const RtInt step = hole.reversed ? -1 : 1;
  for (RtInt k = 0; k < n; k++) {
    RtInt idx = ((hole.rightmostLocal + step * k) % n + n) % n;
    bridged.push_back(ids[idx]);
  }
  bridged.push_back(mId);
  bridged.push_back(bridgeTarget);
  for (RtInt e = targetSlot + 1; e < ringSize; e++) {
    bridged.push_back(ring[e]);
  }
  ring = bridged;
}

// Bridges every non-degenerate loop in loops[1..] into loops[0], the outer
// boundary, by Eberly's method ("Triangulation by Ear Clipping", Geometric
// Tools S3-4): each hole becomes a doubled edge into the boundary (or an
// already-bridged hole) it cuts out, so triangulateEarClipping needs no
// change to consume the result. Judged entirely in the outer loop's own
// frame (outerFrame). collectHoles gathers and orders the holes to bridge;
// each then finds its crossed ring edge (nearestCrossedEdge), its bridge
// vertex (chooseBridgeTarget) and the ring occurrence to bridge to
// (chooseBridgeSlot), then joins the ring (spliceHole).
//
// Free of GMANOptions/GMANAttributes/GMANParameterList/GMANTransform, and
// nothing here depends on the object manager despite living in this file.
//
// loopSlots mirrors loops' own shape, one flat "P"-order index per point --
// plain provenance, not a texture coordinate, which is what keeps this
// function free of GMANParameterList (see above). loops[0] must already be
// checked non-degenerate by the caller. On return, vertexPositions holds one
// entry per kept vertex of every kept loop -- loops[0] first, then each
// successfully bridged hole, each still in its own loop's "P" order -- ring
// is the merged boundary as indices into vertexPositions, and vertexSlots
// (index-aligned with vertexPositions) carries each kept vertex's original
// loopSlots entry, since bridging commits holes in descending-rightmostU
// order, not input order.
void bridgeHoles(const std::vector<std::vector<GMANPoint>>& loops, const std::vector<std::vector<RtInt>>& loopSlots,
                 const GMANVector& normalVec, RtFloat outerBboxSide, std::vector<GMANPoint>& vertexPositions,
                 std::vector<RtInt>& vertexSlots, std::vector<RtInt>& ring) {
  const std::vector<GMANPoint>& outer = loops[0];
  PlanarFrame const frame = outerFrame(outer, normalVec, outerBboxSide);

  // Outer vertices keep ids 0..outer.size()-1, in "P" order -- the mapping
  // a one-loop face's own vertex chain relies on.
  vertexPositions = outer;
  vertexSlots = loopSlots[0];
  ring.resize(outer.size());
  for (std::size_t i = 0; i < outer.size(); i++) {
    ring[i] = (RtInt)i;
  }

  std::vector<Hole> const holes = collectHoles(loops, loopSlots, normalVec, frame);

  for (Hole const& hole : holes) {
    GMANPoint const& m = hole.points[hole.rightmostLocal];

    RtInt const edgeStart = nearestCrossedEdge(vertexPositions, ring, frame, m);
    if (edgeStart < 0) {
      continue; // the hole's ray meets no ring edge: outside the outer
                // loop, dropped
    }

    RtInt const bridgeTarget = chooseBridgeTarget(vertexPositions, ring, edgeStart, m, frame, normalVec, outerBboxSide);
    RtInt const targetSlot = chooseBridgeSlot(vertexPositions, ring, bridgeTarget, m, normalVec);

    spliceHole(hole, targetSlot, vertexPositions, vertexSlots, ring);
  }
}

// getRSPolygonMesh's own tail, one face at a time: loops[0] is the outer
// boundary, loops[1..] holes bridged into it, then triangulated and
// shaded through buildPolygonObject. loopSlots (index-aligned with loops)
// names each vertex's own entry in pointTexCoords, the mesh's own point
// index into the shared "P".
//
// Degeneracy (gman::isDegeneratePolygon) is judged by a ratio, not an
// absolute area: twice the outer loop's area (its Newell normal's
// magnitude) against the square of its largest bounding-box side. A
// polygon a million times longer than it is wide is degenerate at any
// scale, and this ratio reads the same wherever the polygon sits.
//
// Returns false, leaving body and vertRoot untouched, for a degenerate or
// under-three-point outer loop -- the caller skips the face rather than
// treating it as fatal.
bool buildFace(std::vector<std::vector<GMANPoint>> const& loops, std::vector<std::vector<RtInt>> const& loopSlots,
               std::vector<PolygonVertexTexCoord> const& pointTexCoords, RtInt sides, RtToken orientation,
               gman::Appearance const& appearance, GMANMatrix4 const& cameraToWorld, RasterProjection const& dicing,
               GMANBody*& body, GMANVertex*& vertRoot) {
  const std::vector<GMANPoint>& outer = loops[0];
  if (gman::isDegeneratePolygon(outer)) {
    return false; // fully degenerate: no plane worth shading or filling
  }
  RtFloat outerBboxSide = gman::boundingBoxExtent(outer);
  GMANVector normalVec = gman::newellNormal(outer);
  RtFloat normalMagnitude = normalVec.magnitude();
  // Dividing by the magnitude already computed above, rather than calling
  // GMANVector::normalize(), matters here: that method silently leaves a
  // vector unchanged when its magnitude is below RI_EPSILON (1e-10), an
  // absolute threshold a small-but-valid polygon's raw (pre-normalized)
  // normal can fall under even though the ratio guard above has already
  // judged it non-degenerate. turnOrientation's sine identity needs
  // normal at true unit length regardless of the polygon's absolute
  // scale, so this divides unconditionally.
  normalVec /= normalMagnitude;

  std::vector<GMANPoint> vertexLocations;
  std::vector<RtInt> vertexSlots;
  std::vector<RtInt> ring;
  bridgeHoles(loops, loopSlots, normalVec, outerBboxSide, vertexLocations, vertexSlots, ring);

  std::vector<PolygonVertexTexCoord> texCoords(vertexLocations.size());
  for (std::size_t i = 0; i < vertexSlots.size(); i++) {
    texCoords[i] = pointTexCoords[vertexSlots[i]];
  }

  FaceChains const chains = buildPolygonObject(vertexLocations, ring, normalVec, sides, orientation, appearance,
                                               cameraToWorld, texCoords, dicing);
  body = chains.body;
  vertRoot = chains.vertRoot;
  return true;
}

// Appends one surviving face's body and vertex chain onto a mesh's own
// running chains -- GMANObject's destructor already walks both
// (getNext() on each), so one object can own every face's worth once
// they are linked here: one primitive, many bodies. Contrast addFace: that
// adds a single face to a face list; appendFace joins a whole face's
// already-built body and vertex chains onto a mesh.
// A face contributes more than one vertex, unlike GMANBody's single node,
// so its own chain's tail has to be found by walking.
void appendFace(GMANBody* faceBody, GMANVertex* faceVert, GMANBody*& bodyHead, GMANBody*& bodyTail,
                GMANVertex*& vertHead, GMANVertex*& vertTail) {
  if (bodyTail) {
    bodyTail->setNext(faceBody);
  } else {
    bodyHead = faceBody;
  }
  bodyTail = faceBody;

  if (vertTail) {
    vertTail->setNext(faceVert);
  } else {
    vertHead = faceVert;
  }
  GMANVertex* last = faceVert;
  while (last->getNext()) {
    last = last->getNext();
  }
  vertTail = last;
}

} // namespace

/*
 * RenderMan API GMANPatchPolyObjectManager
 *
 */

// default constructor
GMANPatchPolyObjectManager::GMANPatchPolyObjectManager() : GMANObjectManager() {};

// default destructor
GMANPatchPolyObjectManager::~GMANPatchPolyObjectManager() {};

GMANPrimitive* GMANPatchPolyObjectManager::create(RtVoid) { return new GMANObject(); }

// One call for every polygon request: the mesh guarantees a non-null "P"
// and at least one loop per face, so this reads straight through to
// buildFace/appendFace over each of the mesh's own faces and loops.
GMANPrimitive* GMANPatchPolyObjectManager::getRSPolygonMesh(GMANPolygonMesh const& mesh, GMANOptions* opt,
                                                            GMANAttributes* attr, GMANTransform* t) {
  std::span<RtFloat const> const p = mesh.points();
  RtInt const pointCount = (RtInt)(p.size() / 3);

  std::vector<PolygonVertexTexCoord> pointTexCoords =
      resolvePolygonTextureCoordinates(mesh.parameters(), pointCount, p.data());

  RtInt sides = attr->getSides();
  RtToken orientation = attr->getOrientation();
  gman::Appearance const appearance = gman::appearanceOf(*attr);
  GMANMatrix4 const cameraToWorld = cameraToWorldOf(opt);
  RasterProjection const dicing = rasterProjectionFor(opt, attr);

  GMANBody *bodyHead = NULL, *bodyTail = NULL;
  GMANVertex *vertHead = NULL, *vertTail = NULL;

  for (std::size_t i = 0; i < mesh.faceCount(); i++) {
    std::span<RtInt const> const faceLoops = mesh.face(i);
    std::vector<std::vector<GMANPoint>> loops(faceLoops.size());
    std::vector<std::vector<RtInt>> loopSlots(faceLoops.size());
    for (std::size_t li = 0; li < faceLoops.size(); li++) {
      std::span<RtInt const> const indices = mesh.loop(i, li);
      loops[li].resize(indices.size());
      loopSlots[li].resize(indices.size());
      for (std::size_t j = 0; j < indices.size(); j++) {
        RtInt const pointIndex = indices[j];
        loops[li][j] = t->apply(GMANPoint(p[3 * pointIndex], p[3 * pointIndex + 1], p[3 * pointIndex + 2]));
        loopSlots[li][j] = pointIndex;
      }
    }

    GMANBody* faceBody;
    GMANVertex* faceVert;
    if (buildFace(loops, loopSlots, pointTexCoords, sides, orientation, appearance, cameraToWorld, dicing, faceBody,
                  faceVert)) {
      appendFace(faceBody, faceVert, bodyHead, bodyTail, vertHead, vertTail);
    }
  }

  if (!bodyHead) {
    return create();
  }
  GMANObject* object = new GMANObject();
  object->setBody(bodyHead);
  object->setVert(vertHead);
  return object;
}

GMANPrimitive* GMANPatchPolyObjectManager::getRSPatch(RtToken type, GMANParameterList pl, GMANOptions* opt,
                                                      GMANAttributes* attr, GMANTransform* t) {
  RtFloat* p = gman::floatArray(pl, RI_P);
  if (!p) {
    return create();
  }

  GMANTextureCoordinates corners = resolveParametricCorners(pl, attr);
  if (strcmp(type, RI_BILINEAR) == 0) {
    GMANPatch patch(type, p, pl);
    return createParametric(&patch, t, attr, corners, opt);
  }
  if (strcmp(type, RI_BICUBIC) == 0) {
    GMANBasis basis = attr->getUVBasis();
    GMANPatch patch(type, p, basis, pl);
    return createParametric(&patch, t, attr, corners, opt);
  }
  return create();
};

// Passes kIdentityCorners to createParametric rather than resolving
// RiTextureCoordinates or "s"/"t"/"st": a PatchMesh calls createParametric
// once for the whole mesh over [0, 1]^2, so per-sub-patch texture
// coordinates would need a periodic-wrap-aware (nupatches+1) x
// (nvpatches+1) grid of corners -- today's identity mapping stands until
// that grid is worth the reading.
GMANPrimitive* GMANPatchPolyObjectManager::getRSPatchMesh(RtToken type, RtInt nu, RtToken uwrap, RtInt nv,
                                                          RtToken vwrap, GMANParameterList pl, GMANOptions* opt,
                                                          GMANAttributes* attr, GMANTransform* t) {
  RtFloat* p = gman::floatArray(pl, RI_P);
  if (!p) {
    return create();
  }

  if (strcmp(type, RI_BILINEAR) == 0) {
    if (!validBilinearMeshDim(nu) || !validBilinearMeshDim(nv)) {
      warning("PatchMesh \"bilinear\": nu={} nv={} cannot form a patch; "
              "ignoring.",
              nu, nv);
      return create();
    }
    GMANPatchMesh mesh(type, p, nu, uwrap, nv, vwrap, pl);
    return createParametric(&mesh, t, attr, kIdentityCorners, opt);
  }
  if (strcmp(type, RI_BICUBIC) == 0) {
    GMANBasis basis = attr->getUVBasis();
    bool uPeriodic = strcmp(uwrap, RI_PERIODIC) == 0;
    bool vPeriodic = strcmp(vwrap, RI_PERIODIC) == 0;
    if (!validBicubicMeshDim(nu, uPeriodic, basis.getUStep()) ||
        !validBicubicMeshDim(nv, vPeriodic, basis.getVStep())) {
      warning("PatchMesh \"bicubic\": nu={} nv={} does not align to the "
              "current basis step; ignoring.",
              nu, nv);
      return create();
    }
    GMANPatchMesh mesh(type, p, nu, uwrap, nv, vwrap, basis, pl);
    return createParametric(&mesh, t, attr, kIdentityCorners, opt);
  }
  return create();
};

// kIdentityCorners stands in for resolveParametricCorners here, as it does
// for getRSPatchMesh: a NuPatch's varying values form a (nusegments+1) x
// (nvsegments+1) grid, which resolveParametricCorners' fixed four-corner
// shape only fits in the bilinear-equivalent case.
GMANPrimitive* GMANPatchPolyObjectManager::getRSNuPatch(RtInt nu, RtInt uorder, RtFloat uknot[], RtFloat umin,
                                                        RtFloat umax, RtInt nv, RtInt vorder, RtFloat vknot[],
                                                        RtFloat vmin, RtFloat vmax, GMANParameterList pl,
                                                        GMANOptions* opt, GMANAttributes* attr, GMANTransform* t) {
  // "Pw" wins over "P" when both are supplied; "P" alone means w = 1.
  RtFloat* pw = gman::floatArray(pl, RI_PW);
  bool rational = pw != NULL;
  RtFloat* p = rational ? pw : gman::floatArray(pl, RI_P);
  if (!p) {
    return create();
  }

  GMANNuPatch patch(nu, uorder, uknot, umin, umax, nv, vorder, vknot, vmin, vmax, p, rational, pl);
  return createParametric(&patch, t, attr, kIdentityCorners, opt);
};

GMANPrimitive* GMANPatchPolyObjectManager::getRSSphere(RtFloat radius, RtFloat zmin, RtFloat zmax, RtFloat tmax,
                                                       GMANParameterList pl, GMANOptions* opt, GMANAttributes* attr,
                                                       GMANTransform* t) {
  GMANSphere sphere(radius, zmin, zmax, tmax, pl);
  return createParametric(&sphere, t, attr, resolveParametricCorners(pl, attr), opt);
};

GMANPrimitive* GMANPatchPolyObjectManager::getRSCone(RtFloat height, RtFloat radius, RtFloat tmax, GMANParameterList pl,
                                                     GMANOptions* opt, GMANAttributes* attr, GMANTransform* t) {
  GMANCone cone(height, radius, tmax, pl);
  return createParametric(&cone, t, attr, resolveParametricCorners(pl, attr), opt);
};

GMANPrimitive* GMANPatchPolyObjectManager::getRSCylinder(RtFloat radius, RtFloat zmin, RtFloat zmax, RtFloat tmax,
                                                         GMANParameterList pl, GMANOptions* opt, GMANAttributes* attr,
                                                         GMANTransform* t) {
  GMANCylinder cylinder(radius, zmin, zmax, tmax, pl);
  return createParametric(&cylinder, t, attr, resolveParametricCorners(pl, attr), opt);
};

GMANPrimitive* GMANPatchPolyObjectManager::getRSHyperboloid(RtPoint point1, RtPoint point2, RtFloat tmax,
                                                            GMANParameterList pl, GMANOptions* opt,
                                                            GMANAttributes* attr, GMANTransform* t) {
  GMANHyperboloid hyperboloid(point1, point2, tmax, pl);
  return createParametric(&hyperboloid, t, attr, resolveParametricCorners(pl, attr), opt);
};

GMANPrimitive* GMANPatchPolyObjectManager::getRSParaboloid(RtFloat rmax, RtFloat zmin, RtFloat zmax, RtFloat tmax,
                                                           GMANParameterList pl, GMANOptions* opt, GMANAttributes* attr,
                                                           GMANTransform* t) {
  GMANParaboloid paraboloid(rmax, zmin, zmax, tmax, pl);
  return createParametric(&paraboloid, t, attr, resolveParametricCorners(pl, attr), opt);
};

GMANPrimitive* GMANPatchPolyObjectManager::getRSDisk(RtFloat height, RtFloat radius, RtFloat tmax, GMANParameterList pl,
                                                     GMANOptions* opt, GMANAttributes* attr, GMANTransform* t) {
  GMANDisk disk(height, radius, tmax, pl);
  return createParametric(&disk, t, attr, resolveParametricCorners(pl, attr), opt);
};

GMANPrimitive* GMANPatchPolyObjectManager::getRSTorus(RtFloat majrad, RtFloat minrad, RtFloat phimin, RtFloat phimax,
                                                      RtFloat tmax, GMANParameterList pl, GMANOptions* opt,
                                                      GMANAttributes* attr, GMANTransform* t) {
  GMANTorus torus(majrad, minrad, phimin, phimax, tmax, pl);
  return createParametric(&torus, t, attr, resolveParametricCorners(pl, attr), opt);
};

GMANPrimitive* GMANPatchPolyObjectManager::getRSBlobby(RtInt /*nleaf*/, RtInt /*ncode*/, RtInt /*code*/[],
                                                       RtInt /*nflt*/, RtFloat /*flt*/[], RtInt /*nstr*/,
                                                       RtToken /*str*/[], GMANParameterList /*pl*/,
                                                       GMANOptions* /*opt*/, GMANAttributes* /*attr*/,
                                                       GMANTransform* /*t*/) {
  return create();
};

GMANPrimitive* GMANPatchPolyObjectManager::getRSPoints(RtInt /*npoints*/, GMANParameterList /*pl*/,
                                                       GMANOptions* /*opt*/, GMANAttributes* /*attr*/,
                                                       GMANTransform* /*t*/) {
  return create();
};

GMANPrimitive* GMANPatchPolyObjectManager::getRSCurves(RtToken /*type*/, RtInt /*ncurves*/, RtInt /*nvertices*/[],
                                                       RtToken /*wrap*/, GMANParameterList /*pl*/, GMANOptions* /*opt*/,
                                                       GMANAttributes* /*attr*/, GMANTransform* /*t*/) {
  return create();
};

GMANPrimitive* GMANPatchPolyObjectManager::getRSSubdivisionMesh(RtToken /*mask*/, RtInt /*nf*/, RtInt /*nverts*/[],
                                                                RtInt /*verts*/[], RtInt /*ntags*/, RtToken /*tags*/[],
                                                                RtInt /*numargs*/[], RtInt /*intargs*/[],
                                                                RtFloat /*floatargs*/[], GMANParameterList /*pl*/,
                                                                GMANOptions* /*opt*/, GMANAttributes* /*attr*/,
                                                                GMANTransform* /*t*/) {
  return create();
};

GMANObject* GMANPatchPolyObjectManager::createParametric(GMANParametric* p, GMANTransform* t, GMANAttributes* attr,
                                                         GMANTextureCoordinates const& corners,
                                                         GMANOptions const* opt) {
  int i, j;
  RtInt sides = attr->getSides();
  RtToken orientation = attr->getOrientation();

  // The vertex shading normal is object-space (p->getNormal), unlike the
  // face's geometric normal below, which gets to camera space for free as
  // a side effect of crossing already-transformed edges. A plain normal
  // does not get that gift: it needs the CTM's inverse transpose, computed
  // once per primitive rather than once per vertex. Row-vector convention
  // (p*M, translation in row 3) makes the inverse-transpose of M's linear
  // part exactly Minv's own upper-left 3x3 block used as n*Minv.
  GMANMatrix4 ctmInv = t->interpolate(0.0);
  ctmInv.invert();

  // Shading setup, resolved once per primitive rather than once per vertex.
  gman::Appearance const appearance = gman::appearanceOf(*attr);
  GMANMatrix4 const cameraToWorld = cameraToWorldOf(opt);

  std::vector<GMANVertex*> vertices((kParametricDiceU + 1) * (kParametricDiceV + 1));
  std::vector<GMANFace*> faces;
  faces.reserve((std::size_t)kParametricDiceU * (std::size_t)kParametricDiceV);
  GMANBody* body = new GMANBody(GMANColor(), GMANColor());
  GMANSurface* surface = new GMANSurface(body);
  body->setSurface(surface);

  for (i = 0; i < (kParametricDiceU + 1) * (kParametricDiceV + 1); i++) {
    vertices[i] = new GMANVertex();
  }

  for (i = 0; i <= kParametricDiceU; i++) {
    for (j = 0; j <= kParametricDiceV; j++) {
      // Create a vertex
      double u = i / (double)kParametricDiceU;
      double v = j / (double)kParametricDiceV;
      GMANPoint location = t->apply(p->getLocation(u, v));
      GMANVector objectNormal = p->getNormal(u, v);
      GMANVector normal(
          ctmInv[0][0] * objectNormal.getX() + ctmInv[0][1] * objectNormal.getY() + ctmInv[0][2] * objectNormal.getZ(),
          ctmInv[1][0] * objectNormal.getX() + ctmInv[1][1] * objectNormal.getY() + ctmInv[1][2] * objectNormal.getZ(),
          ctmInv[2][0] * objectNormal.getX() + ctmInv[2][1] * objectNormal.getY() + ctmInv[2][2] * objectNormal.getZ());
      normal.normalize();
      GMANVertex* vertex = vertices[(kParametricDiceU + 1) * i + j];
      vertex->setLocation(location);
      vertex->setNormal(normal);

      GMANNormal shadingNormal(normal.getX(), normal.getY(), normal.getZ());
      RtFloat s = bilerpCorner(u, v, corners.s1, corners.s2, corners.s3, corners.s4);
      RtFloat texT = bilerpCorner(u, v, corners.t1, corners.t2, corners.t3, corners.t4);
      shadeVertex(*vertex, shadingNormal, (RtFloat)u, (RtFloat)v, s, texT, appearance, cameraToWorld);
    }
  }

  // A second pass, now that every vertex in the grid has a finalized
  // location: face(i,j) touches vertices (i,j+1) and (i+1,*), which are
  // not visited yet when (i,j) is, so calcNormal() run in the same pass
  // as vertex creation would cross-product against up to three
  // still-default-constructed (0,0,0) vertices. The renderer's own
  // rasterization always reads vertex positions fresh at render time,
  // long after this function returns, so geometry is unaffected either
  // way -- only RiSides 1 culling would read the resulting near-zero,
  // direction-free normal and cull or keep faces at random.
  for (i = 0; i < kParametricDiceU; i++) {
    for (j = 0; j < kParametricDiceV; j++) {
      GMANVertex* faceVertices[4];
      faceVertices[0] = vertices[(kParametricDiceU + 1) * i + j];
      faceVertices[1] = vertices[(kParametricDiceU + 1) * i + (j + 1)];
      faceVertices[2] = vertices[(kParametricDiceU + 1) * (i + 1) + (j + 1)];
      faceVertices[3] = vertices[(kParametricDiceU + 1) * (i + 1) + j];
      // Geometric normal, computed from the already-transformed (camera
      // space) vertices: cross(e1', e2') for e'=e*M is proportional to
      // (e1 x e2) transformed by M's inverse transpose, so this needs no
      // separate normal transform. RiSides/RiOrientation travel with the
      // face so visible() can answer without depending on renderer-global
      // state that may differ across attribute blocks.
      addFace(faces, faceVertices, surface, sides, orientation);
    }
  }

  for (i = 0; i < (kParametricDiceU + 1) * (kParametricDiceV + 1) - 1; i++) {
    vertices[i]->setNext(vertices[i + 1]);
  }
  for (i = 0; i < kParametricDiceU * kParametricDiceV - 1; i++) {
    faces[i]->setNext(faces[i + 1]);
  }
  surface->setFace(faces[0]);

  GMANObject* object = (GMANObject*)create();
  object->setVert(vertices[0]);
  object->setBody(body);

  return object;
}
