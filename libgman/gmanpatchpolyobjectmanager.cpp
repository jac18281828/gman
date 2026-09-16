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
#include <cstring>
#include <vector>

/* Local Headers */
#include "ri.h"      /* RenderMan Interface */
#include "gmanobjectmanager.h" /* Super class */
#include "gmanpatchpolyobjectmanager.h" /* Declaration Header */
#include "gmanprimitives.h"
#include "gmanshaderenvironment.h"
#include "gmansurfaceshader.h"
#include "gmanloadableshader.h"
#include "gmanlightsourcemgr.h"

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
const RtFloat kTriangulationTolerance = (RtFloat) 1.0e-6;

// getRSPatchMesh's own corners: RiTextureCoordinates spans a single
// parametric surface's unit square, and a PatchMesh's sub-patches already
// share one such square end to end. getRSPatchMesh passes this identity
// mapping rather than resolving RiTextureCoordinates or "s"/"t"/"st" itself
// -- see its own comment.
const GMANTextureCoordinates kIdentityCorners = {0, 0, 1, 0, 0, 1, 1, 1};

// The RISpec's own default: a scene that never calls RiSurface still
// shades, as matte. One instance, loaded on first use and reused --
// dlopen once, not once per primitive.
GMANSurfaceShader *defaultSurfaceShader() {
  static GMANLoadableShader loader("libmatte.so");
  static GMANSurfaceShader *shader = loader.getSurface();
  return shader;
}

// Shared by getRSPolygon and getRSPatch to resolve "P" against. Its
// signature carries no GMANDictionary, and every GMANDictionary registers
// the same standard RI_* tokens in the same order
// (GMANDictionary::GMANDictionary), so a second instance resolves "P" to
// the same GMANTokenId the request's own parameter list was built against
// -- shaders/gmanshaderparams.h's dictionary() is the same idiom, for the
// same reason.
GMANDictionary &standardDictionary() {
  static GMANDictionary d;
  return d;
}

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
bool validBilinearMeshDim(RtInt n) {
  return n >= 2;
}

// Per-primitive shading inputs that don't vary per vertex: the surface
// shader (falling back to matte, per the RISpec's own default, when
// RiSurface was never called), the lights active in this attribute scope
// (RiIlluminate), and the primitive's own Cs/Os. Shared by createParametric
// and getRSPolygon, which otherwise duplicated this resolution verbatim.
struct GMANShadingContext {
  GMANSurfaceShader *shader;
  std::vector<const GMANLight *> activeLights;
  GMANColor Cs;
  GMANColor Os;
  GMANMatrix4 cameraToWorld;  // identity unless opt carries one
};

GMANShadingContext resolveShadingContext(GMANAttributes *attr,
                                          GMANOptions const *opt) {
  GMANShadingContext ctx;

  // getSurface's const pointer just reflects that GMANAttributes doesn't
  // want its shader pointer reseated through it; computeCi/computeOi are
  // not logically const on the shader instance itself, which is why this
  // casts rather than threading const through the shading call below.
  const GMANSurfaceShader *constShader = attr->getSurface(0.0);
  ctx.shader = constShader
      ? const_cast<GMANSurfaceShader *>(constShader)
      : defaultSurfaceShader();

  const std::list<RtLightHandle> &handles = attr->getLightList().getHandles();
  for (std::list<RtLightHandle>::const_iterator it = handles.begin();
       it != handles.end(); ++it) {
    const GMANLight *light = gmanLightSourceMgr().get(*it);
    if (light) {
      ctx.activeLights.push_back(light);
    }
  }

  ctx.Cs = attr->getColor();
  ctx.Os = attr->getOpacity();
  if (opt) {
    ctx.cameraToWorld = opt->getCameraToWorld();
  }
  return ctx;
}

// A parametric surface's four corner texture coordinates (RISpec 3.2's
// RiTextureCoordinates), resolved in precedence order, most specific last so
// it wins: attr's corners (the RiTextureCoordinates default, s=u/t=v unless
// the scene called it), then "st" (eight varying values, s/t per corner in
// RISpec corner order: (0,0), (1,0), (0,1), (1,1)), then "s"/"t" (four
// varying values each, overriding only their own component). Shared by every
// quadric and getRSPatch; getRSPatchMesh does not call this (see its own
// comment).
GMANTextureCoordinates resolveParametricCorners(GMANParameterList &pl,
                                                 GMANAttributes *attr) {
  GMANTextureCoordinates corners = attr->getTextureCoordinates();

  RtFloat *st = (RtFloat *) pl.getPointer(standardDictionary().getTokenId(RI_ST));
  if (st) {
    corners.s1 = st[0]; corners.t1 = st[1];
    corners.s2 = st[2]; corners.t2 = st[3];
    corners.s3 = st[4]; corners.t3 = st[5];
    corners.s4 = st[6]; corners.t4 = st[7];
  }
  RtFloat *s = (RtFloat *) pl.getPointer(standardDictionary().getTokenId(RI_S));
  if (s) {
    corners.s1 = s[0];
    corners.s2 = s[1];
    corners.s3 = s[2];
    corners.s4 = s[3];
  }
  RtFloat *tp = (RtFloat *) pl.getPointer(standardDictionary().getTokenId(RI_T));
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
RtFloat bilerpCorner(double u, double v, RtFloat c00, RtFloat c10,
                      RtFloat c01, RtFloat c11) {
  return (RtFloat) ((1.0 - u) * (1.0 - v) * c00 + u * (1.0 - v) * c10 +
                     (1.0 - u) * v * c01 + u * v * c11);
}

// Shades one vertex in camera space, with every input the shader needs
// already at hand -- this is what "shade per vertex and let [Gouraud
// interpolation] interpolate" (SPEC.md) means in practice: a clip-introduced
// vertex has no u,v of its own to shade with, but it does get a color,
// because GMANClipEdge::intersect already interpolates GMANVertex::color
// across a clipped edge (the same machinery phase 1 wired up for the vertex
// alpha blend). Eye sits at the camera-space origin (GMANVSPerspective::ray),
// so the incident direction is just the normalized surface point.
GMANColor shadeVertex(const GMANShadingContext &ctx, const GMANPoint &location,
		       const GMANNormal &normal, RtFloat u, RtFloat v,
		       RtFloat s, RtFloat t) {
  GMANSurfaceEnv env;
  env.Cs = ctx.Cs;
  env.Os = ctx.Os;
  env.P = location;
  env.N = normal;
  env.Ng = normal;  // no displacement this phase; the two never diverge
  env.I = GMANVector(location.getX(), location.getY(), location.getZ());
  env.I.normalize();
  env.E = GMANPoint(0.0, 0.0, 0.0);
  env.u = u;
  env.v = v;
  env.s = s;
  env.t = t;
  env.lights = ctx.activeLights;
  env.cameraToWorld = ctx.cameraToWorld;
  return ctx.shader->computeCi(env);
}

// Newell's method: the face normal as the sum of every edge's
// contribution, rather than the cross product of two edges at one
// arbitrarily chosen vertex. Correct for any simple planar polygon,
// including one where vertices 0, 1 and 2 form a reflex corner -- there, a
// three-vertex cross product points opposite the polygon's true face
// normal, while summing over every edge cannot, since each edge
// contributes in proportion to the area it bounds.
GMANVector newellNormal(const std::vector<GMANPoint> &ring) {
  GMANVector sum;
  const std::size_t n = ring.size();
  for (std::size_t i = 0; i < n; i++) {
    const GMANPoint &cur = ring[i];
    const GMANPoint &next = ring[(i + 1) % n];
    sum.setX(sum.getX() +
             (cur.getY() - next.getY()) * (cur.getZ() + next.getZ()));
    sum.setY(sum.getY() +
             (cur.getZ() - next.getZ()) * (cur.getX() + next.getX()));
    sum.setZ(sum.getZ() +
             (cur.getX() - next.getX()) * (cur.getY() + next.getY()));
  }
  return sum;
}

// The ring's largest bounding-box side, in whichever of x, y or z spans
// it widest. getRSPolygon's degeneracy guard judges the polygon's area
// against this extent rather than against an absolute constant, so a
// sliver a million times longer than it is wide reads the same way at
// any scale.
RtFloat boundingBoxExtent(const std::vector<GMANPoint> &ring) {
  RtFloat minX = ring[0].getX(), maxX = minX;
  RtFloat minY = ring[0].getY(), maxY = minY;
  RtFloat minZ = ring[0].getZ(), maxZ = minZ;
  for (std::size_t i = 1; i < ring.size(); i++) {
    const GMANPoint &pt = ring[i];
    if (pt.getX() < minX) minX = pt.getX();
    if (pt.getX() > maxX) maxX = pt.getX();
    if (pt.getY() < minY) minY = pt.getY();
    if (pt.getY() > maxY) maxY = pt.getY();
    if (pt.getZ() < minZ) minZ = pt.getZ();
    if (pt.getZ() > maxZ) maxZ = pt.getZ();
  }
  RtFloat extent = maxX - minX;
  if (maxY - minY > extent) extent = maxY - minY;
  if (maxZ - minZ > extent) extent = maxZ - minZ;
  return extent;
}

// The sine of the angle between a and b, judged against normal: a.cross(b)
// is an area (units of length squared); dividing by |a|*|b| turns it into
// a dimensionless quantity in [-1, 1] regardless of either vector's own
// scale, so a caller can compare it against a fixed tolerance wherever
// the geometry sits. normal must already be unit length -- the sine
// identity depends on it. A zero-length a or b would divide to NaN, which
// fails every comparison; returning 0 reads instead as the angle a
// vanishing vector cannot have a turn or a side of, which is what both
// callers below already treat a zero-length input as.
RtFloat dimensionlessCross(GMANVector a, GMANVector b,
                            const GMANVector &normal) {
  RtFloat lenA = a.magnitude();
  RtFloat lenB = b.magnitude();
  if (lenA == (RtFloat) 0.0 || lenB == (RtFloat) 0.0) {
    return (RtFloat) 0.0;
  }
  return a.cross(b).dot(normal) / (lenA * lenB);
}

// A ring vertex's turning direction relative to the polygon's own normal:
// positive is convex, negative reflex, zero for a collinear or duplicate
// vertex. Testing against this normal, rather than against the ring's own
// winding, keeps the result correct whichever way the ring winds --
// normal already followed that winding when Newell's method built it.
RtFloat turnOrientation(const GMANPoint &prev, const GMANPoint &cur,
                         const GMANPoint &next, const GMANVector &normal) {
  return dimensionlessCross(GMANVector(prev, cur), GMANVector(cur, next),
                             normal);
}

// The sine of the angle between one triangle edge and the vector from
// that edge's start to p, the same dimensionless quantity turnOrientation
// returns, so a boundary case reads the same near-zero value wherever the
// triangle sits, at any scale or rotation. p that lands exactly on the
// line through the edge -- the common case for a ring vertex bridged by a
// diagonal of its own polygon -- gives exactly 0 in exact arithmetic;
// comparing that against literal 0 instead of a tolerance band lets
// rounding alone decide which side p falls on.
RtFloat sideOf(const GMANPoint &from, const GMANPoint &to,
               const GMANPoint &p, const GMANVector &normal) {
  return dimensionlessCross(GMANVector(from, to), GMANVector(from, p),
                             normal);
}

// True when p lies inside or on the boundary of coplanar triangle
// (a, b, c): on the same side of every edge, judged by sideOf, so the
// test does not depend on which way the triangle happens to wind.
bool pointInTriangle(const GMANPoint &a, const GMANPoint &b,
                      const GMANPoint &c, const GMANPoint &p,
                      const GMANVector &normal) {
  RtFloat d0 = sideOf(a, b, p, normal);
  RtFloat d1 = sideOf(b, c, p, normal);
  RtFloat d2 = sideOf(c, a, p, normal);
  bool hasNeg = d0 < -kTriangulationTolerance || d1 < -kTriangulationTolerance ||
                d2 < -kTriangulationTolerance;
  bool hasPos = d0 > kTriangulationTolerance || d1 > kTriangulationTolerance ||
                d2 > kTriangulationTolerance;
  return !(hasNeg && hasPos);
}

// Ear clipping over a vertex ring: triangulates any simple planar polygon,
// concave included, into exactly ring.size() - 2 triangles. GeneralPolygon
// (a later task) bridges each hole into the outer loop and hands the
// combined ring to this same function, so it takes points and a normal
// rather than assuming any particular caller's vertex storage; the
// returned triples index into that same ring.
//
// Only a reflex vertex can lie inside a convex ear's triangle -- a
// standard property of simple polygons -- so each candidate's containment
// test runs against the reflex set alone, not every remaining vertex.
std::vector<std::array<RtInt, 3>> triangulateEarClipping(
    const std::vector<GMANPoint> &ring, const GMANVector &normal) {
  std::vector<std::array<RtInt, 3>> triangles;
  const RtInt n = (RtInt) ring.size();
  if (n < 3) {
    return triangles;
  }
  triangles.reserve(n - 2);

  std::vector<RtInt> remaining(n);
  for (RtInt i = 0; i < n; i++) {
    remaining[i] = i;
  }

  while (true) {
    // m is read once and reused for every index and the modulo below --
    // re-reading remaining.size() at the point of the '% m' left the
    // analyzer unable to connect it back to this loop's own continuation
    // check, which is what kept m>3 true here.
    const std::size_t m = remaining.size();
    if (m <= 3) {
      break;
    }

    std::vector<RtInt> reflex;
    std::vector<RtFloat> orient(m);
    for (std::size_t i = 0; i < m; i++) {
      const RtInt iPrev = remaining[(i + m - 1) % m];
      const RtInt iCur = remaining[i];
      const RtInt iNext = remaining[(i + 1) % m];
      orient[i] =
          turnOrientation(ring[iPrev], ring[iCur], ring[iNext], normal);
      if (orient[i] < -kTriangulationTolerance) {
        reflex.push_back(iCur);
      }
    }

    bool foundEar = false;
    std::size_t clipAt = 0;
    std::size_t fallbackAt = 0;
    RtFloat fallbackOrient = orient[0];
    for (std::size_t i = 0; i < m; i++) {
      if (orient[i] > fallbackOrient) {
        fallbackOrient = orient[i];
        fallbackAt = i;
      }
      if (orient[i] < -kTriangulationTolerance) {
        continue;  // reflex: never an ear
      }
      const RtInt iPrev = remaining[(i + m - 1) % m];
      const RtInt iCur = remaining[i];
      const RtInt iNext = remaining[(i + 1) % m];

      // A collinear or duplicate vertex (orient ~ 0) lies on its own
      // prev-next segment; clipping it changes neither the polygon's
      // shape nor its area, so it needs no containment test -- skipping
      // that test is what keeps a run of such vertices from stalling the
      // loop, since a zero-area "ear" can otherwise appear to contain its
      // own neighbors.
      bool degenerate = orient[i] <= kTriangulationTolerance;
      bool containsReflex = false;
      for (std::vector<RtInt>::const_iterator it = reflex.begin();
           !degenerate && !containsReflex && it != reflex.end(); ++it) {
        RtInt idx = *it;
        if (idx == iPrev || idx == iCur || idx == iNext) {
          continue;
        }
        containsReflex = pointInTriangle(ring[iPrev], ring[iCur],
                                          ring[iNext], ring[idx], normal);
      }
      if (degenerate || !containsReflex) {
        clipAt = i;
        foundEar = true;
        break;
      }
    }

    // No true ear found: only reachable from malformed (self-intersecting)
    // input, since a simple polygon always has one. Clip the
    // least-reflex candidate anyway -- degrade, do not hang.
    if (!foundEar) {
      clipAt = fallbackAt;
    }

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
// getRSPolygon and getRSGeneralPolygon never resolve it.
struct GMANPolygonVertexTexCoord {
  RtFloat u, v, s, t;
};

// p is the vertex's own flat "P" array (3 floats per vertex, object space,
// before the CTM); nverts is also "s"/"t"/"st"'s own declared length, so
// index i reads the same vertex from every one of them.
std::vector<GMANPolygonVertexTexCoord> resolvePolygonTextureCoordinates(
    GMANParameterList &pl, RtInt nverts, const RtFloat *p) {
  RtFloat *sArr = (RtFloat *) pl.getPointer(standardDictionary().getTokenId(RI_S));
  RtFloat *tArr = (RtFloat *) pl.getPointer(standardDictionary().getTokenId(RI_T));
  RtFloat *stArr = (RtFloat *) pl.getPointer(standardDictionary().getTokenId(RI_ST));

  std::vector<GMANPolygonVertexTexCoord> coords(nverts);
  for (RtInt i = 0; i < nverts; i++) {
    RtFloat objX = p[3 * i];
    RtFloat objY = p[3 * i + 1];
    RtFloat s = objX;
    RtFloat t = objY;
    if (stArr) {
      s = stArr[2 * i];
      t = stArr[2 * i + 1];
    }
    if (sArr) {
      s = sArr[i];
    }
    if (tArr) {
      t = tArr[i];
    }
    coords[i] = {objX, objY, s, t};
  }
  return coords;
}

// The tail shared by getRSPolygon and getRSGeneralPolygon: one GMANVertex
// per entry in vertexLocations, triangulated over ring (which may repeat an
// entry at a bridge -- triangulateEarClipping's own comment already covers
// the resulting duplicate position and zero-area corner), linked into a new
// GMANObject. ring indexes vertexLocations rather than a face's own vertex
// array, so the two ring slots a bridge duplicates share one GMANVertex and
// one shaded colour instead of splitting the surface. texCoords is
// index-aligned with vertexLocations, not with ring.
GMANObject *buildPolygonObject(
    const std::vector<GMANPoint> &vertexLocations,
    const std::vector<RtInt> &ring, const GMANVector &normalVec, RtInt sides,
    RtToken orientation, const GMANShadingContext &shading,
    const std::vector<GMANPolygonVertexTexCoord> &texCoords) {
  GMANNormal normal(normalVec.getX(), normalVec.getY(), normalVec.getZ());

  GMANBody *body = new GMANBody(GMANColor(), GMANColor());
  GMANSurface *surface = new GMANSurface(body);
  body->setSurface(surface);

  const RtInt nverts = (RtInt) vertexLocations.size();
  GMANVertex **vertices = new GMANVertex*[nverts];
  for (RtInt i = 0; i < nverts; i++) {
    vertices[i] = new GMANVertex();
    vertices[i]->setLocation(vertexLocations[i]);
    vertices[i]->setNormal(normalVec);

    const GMANPolygonVertexTexCoord &tc = texCoords[i];
    vertices[i]->setColor(
        shadeVertex(shading, vertexLocations[i], normal, tc.u, tc.v, tc.s, tc.t));
  }

  std::vector<GMANPoint> ringPoints(ring.size());
  for (std::size_t i = 0; i < ring.size(); i++) {
    ringPoints[i] = vertexLocations[ring[i]];
  }
  std::vector<std::array<RtInt, 3>> triangles =
      triangulateEarClipping(ringPoints, normalVec);

  // Ear-clip into GMANFace's fixed 4-vertex shape, the 4th slot duplicating
  // the 3rd -- calcArea/calcNormal both read [0][1][2] or collapse cleanly
  // when [2]==[3], the same idiom every quadric's pole face already uses
  // for a degenerate triangle. triangles[i] indexes ring, so it is mapped
  // through ring[...] to reach vertexLocations' own indexing.
  RtInt nfaces = (RtInt) triangles.size();
  GMANFace **faces = new GMANFace*[nfaces];
  for (RtInt i = 0; i < nfaces; i++) {
    GMANVertex *faceVertices[4];
    faceVertices[0] = vertices[ring[triangles[i][0]]];
    faceVertices[1] = vertices[ring[triangles[i][1]]];
    faceVertices[2] = vertices[ring[triangles[i][2]]];
    faceVertices[3] = vertices[ring[triangles[i][2]]];
    faces[i] = new GMANFace(faceVertices, surface);
    faces[i]->calcNormal();
    faces[i]->setSides(sides);
    faces[i]->setOrientation(orientation);
  }

  for (RtInt i = 0; i < nverts - 1; i++) {
    vertices[i]->setNext(vertices[i + 1]);
  }
  for (RtInt i = 0; i < nfaces - 1; i++) {
    faces[i]->setNext(faces[i + 1]);
  }
  surface->setFace(faces[0]);

  GMANObject *object = new GMANObject();
  object->setVert(vertices[0]);
  object->setBody(body);

  delete[] vertices;
  delete[] faces;

  return object;
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
bool inInteriorWedge(const GMANPoint &v, const GMANPoint &prev,
                      const GMANPoint &next, const GMANPoint &target,
                      const GMANVector &normal) {
  bool convex =
      turnOrientation(prev, v, next, normal) > -kTriangulationTolerance;
  bool leftOfIncoming = sideOf(prev, v, target, normal) > -kTriangulationTolerance;
  bool leftOfOutgoing = sideOf(v, next, target, normal) > -kTriangulationTolerance;
  return convex ? (leftOfIncoming && leftOfOutgoing)
                : (leftOfIncoming || leftOfOutgoing);
}

// Bridges every non-degenerate loop in loops[1..] into loops[0], the outer
// boundary, by Eberly's method ("Triangulation by Ear Clipping", Geometric
// Tools S3-4): each hole becomes a doubled edge into the boundary (or an
// already-bridged hole) it cuts out, so triangulateEarClipping needs no
// change to consume the result. Judged entirely in the outer loop's own
// (u, v) frame -- u the direction of its first edge of non-zero length, v
// = normal x u -- so the bridges a rotated or rescaled placement of the
// same polygon produces are the same bridges, not an artifact of whichever
// way a fixed world axis happened to point.
//
// Free of GMANOptions/GMANAttributes/GMANParameterList/GMANTransform: the
// 0.9 ray tracer will want this in its own translation unit, and nothing
// here depends on the object manager to make that move mechanical.
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
void bridgeHoles(const std::vector<std::vector<GMANPoint>> &loops,
                  const std::vector<std::vector<RtInt>> &loopSlots,
                  const GMANVector &normalVec, RtFloat outerBboxSide,
                  std::vector<GMANPoint> &vertexPositions,
                  std::vector<RtInt> &vertexSlots,
                  std::vector<RtInt> &ring) {
  const std::vector<GMANPoint> &outer = loops[0];

  // u is the first edge of non-zero length, so the frame rotates with the
  // polygon rather than sitting on a fixed world axis a rotated placement
  // would then bridge differently. The longest edge seen is kept as a
  // fallback only for an outer loop whose every edge falls under the
  // tolerance floor relative to its own bounding box -- unreachable once
  // the caller's own degeneracy guard has passed, since that guard already
  // requires a non-negligible area, but cheap insurance against a zero
  // uDir all the same.
  GMANVector uDir;
  bool foundEdge = false;
  RtFloat longestEdge = (RtFloat) 0.0;
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
  if (!foundEdge && longestEdge > (RtFloat) 0.0) {
    uDir /= longestEdge;
  }
  GMANVector vDir = normalVec.cross(uDir);
  const GMANPoint &origin = outer[0];

  auto projU = [&](const GMANPoint &pt) {
    return GMANVector(origin, pt).dot(uDir);
  };
  auto projV = [&](const GMANPoint &pt) {
    return GMANVector(origin, pt).dot(vDir);
  };

  // Outer vertices keep ids 0..outer.size()-1, in "P" order -- the mapping
  // getRSPolygon's own vertex chain already relies on when nloops == 1.
  vertexPositions = outer;
  vertexSlots = loopSlots[0];
  ring.resize(outer.size());
  for (std::size_t i = 0; i < outer.size(); i++) {
    ring[i] = (RtInt) i;
  }

  // A hole's own points stay in "P" order until it is actually bridged (see
  // below): assigning ids and appending to vertexPositions before knowing
  // whether the hole's ray meets the ring at all would strand an unused
  // GMANVertex for a dropped hole -- one no triangle ever references,
  // failing the coverage property every kept vertex must satisfy.
  struct Hole {
    std::vector<GMANPoint> points;
    std::vector<RtInt> slots;
    bool reversed;
    RtInt rightmostLocal;
    RtFloat rightmostU;
    RtInt loopIndex;
  };
  std::vector<Hole> holes;

  for (std::size_t loopIndex = 1; loopIndex < loops.size(); loopIndex++) {
    const std::vector<GMANPoint> &loop = loops[loopIndex];
    if (loop.size() < 3) {
      continue;  // encloses no area: dropped
    }
    RtFloat bboxSide = boundingBoxExtent(loop);
    GMANVector holeNewell = newellNormal(loop);
    RtFloat holeMag = holeNewell.magnitude();
    if (bboxSide == (RtFloat) 0.0 ||
        holeMag < kTriangulationTolerance * bboxSide * bboxSide) {
      continue;  // degenerate against its own extent: dropped
    }

    Hole hole;
    hole.loopIndex = (RtInt) loopIndex;
    hole.points = loop;
    hole.slots = loopSlots[loopIndex];
    // A hole wound the same way as the outer loop would add its area
    // instead of removing it; reversing its traversal order below is what
    // turns the bridge into a cut.
    hole.reversed = holeNewell.dot(normalVec) > (RtFloat) 0.0;

    hole.rightmostLocal = 0;
    hole.rightmostU = projU(loop[0]);
    for (std::size_t j = 1; j < loop.size(); j++) {
      RtFloat u = projU(loop[j]);
      if (u > hole.rightmostU) {
        hole.rightmostU = u;
        hole.rightmostLocal = (RtInt) j;
      }
    }
    holes.push_back(hole);
  }

  // Rule 1: descending largest u, so a ray cast from a not-yet-bridged
  // hole's rightmost vertex can only meet the boundary or a hole already
  // bridged -- never one still to come, which it could otherwise cross.
  std::sort(holes.begin(), holes.end(), [](const Hole &a, const Hole &b) {
    if (a.rightmostU != b.rightmostU) {
      return a.rightmostU > b.rightmostU;
    }
    return a.loopIndex < b.loopIndex;
  });

  for (const Hole &hole : holes) {
    const RtInt n = (RtInt) hole.points.size();
    const GMANPoint &M = hole.points[hole.rightmostLocal];
    const RtFloat mu = projU(M);
    const RtFloat mv = projV(M);

    // Rule 2: the nearest ring edge the +u ray from M crosses.
    RtInt edgeStart = -1;
    RtFloat nearestU = 0;
    const RtInt ringSize = (RtInt) ring.size();
    for (RtInt e = 0; e < ringSize; e++) {
      const GMANPoint &a = vertexPositions[ring[e]];
      const GMANPoint &b = vertexPositions[ring[(e + 1) % ringSize]];
      RtFloat av = projV(a), bv = projV(b);
      if ((av > mv) == (bv > mv)) {
        continue;  // does not cross the ray's line
      }
      RtFloat au = projU(a), bu = projU(b);
      RtFloat crossU = au + (mv - av) / (bv - av) * (bu - au);
      if (crossU <= mu) {
        continue;  // behind the ray's origin
      }
      if (edgeStart < 0 || crossU < nearestU) {
        edgeStart = e;
        nearestU = crossU;
      }
    }
    if (edgeStart < 0) {
      continue;  // the hole's ray meets no ring edge: outside the outer
                 // loop, dropped
    }

    const RtInt aId = ring[edgeStart];
    const RtInt bId = ring[(edgeStart + 1) % ringSize];
    const GMANPoint &a = vertexPositions[aId];
    const GMANPoint &b = vertexPositions[bId];
    const RtInt pId = (projU(a) > projU(b)) ? aId : bId;
    const RtFloat tParam = (mv - projV(a)) / (projV(b) - projV(a));
    const GMANPoint iPoint = a + (GMANPoint) (GMANVector(a, b) * tParam);

    const RtFloat coincideTol = kTriangulationTolerance * outerBboxSide;
    RtInt bridgeTarget;
    GMANVector distToA(iPoint, a);
    GMANVector distToB(iPoint, b);
    if (distToA.magnitude() <= coincideTol) {
      bridgeTarget = aId;
    } else if (distToB.magnitude() <= coincideTol) {
      bridgeTarget = bId;
    } else {
      // No ring vertex sits at I: the target is P, unless a reflex ring
      // vertex inside triangle (M, I, P) is a better -- nearer the ray --
      // bridge; bridging straight to P past such a vertex would cross it.
      const GMANPoint &pPoint = vertexPositions[pId];
      RtInt best = -1;
      RtFloat bestCos = 0;
      RtFloat bestDist = 0;
      for (RtInt e = 0; e < ringSize; e++) {
        RtInt vId = ring[e];
        if (vId == aId || vId == bId) {
          continue;  // the crossed edge's own endpoints, already considered
        }
        const GMANPoint &v = vertexPositions[vId];
        const GMANPoint &prev = vertexPositions[ring[(e + ringSize - 1) % ringSize]];
        const GMANPoint &next = vertexPositions[ring[(e + 1) % ringSize]];
        if (turnOrientation(prev, v, next, normalVec) >=
            -kTriangulationTolerance) {
          continue;  // only a reflex vertex can lie inside a visibility ear
        }
        if (!pointInTriangle(M, iPoint, pPoint, v, normalVec)) {
          continue;
        }
        GMANVector toV(M, v);
        RtFloat dist = toV.magnitude();
        if (dist == (RtFloat) 0.0) {
          continue;
        }
        RtFloat cosAngle = toV.dot(uDir) / dist;
        if (best < 0 || cosAngle > bestCos ||
            (cosAngle == bestCos && dist < bestDist)) {
          best = vId;
          bestCos = cosAngle;
          bestDist = dist;
        }
      }
      bridgeTarget = (best >= 0) ? best : pId;
    }

    // Rule 3: bridgeTarget may already occur twice in the ring (a previous
    // hole's own bridge point); the wrong occurrence's wedge does not
    // contain M, and bridging to it would cross into the wrong lobe.
    RtInt targetSlot = -1;
    for (RtInt e = 0; e < ringSize; e++) {
      if (ring[e] != bridgeTarget) {
        continue;
      }
      if (targetSlot < 0) {
        targetSlot = e;  // first occurrence: the default if none matches
      }
      const GMANPoint &v = vertexPositions[ring[e]];
      const GMANPoint &prev = vertexPositions[ring[(e + ringSize - 1) % ringSize]];
      const GMANPoint &next = vertexPositions[ring[(e + 1) % ringSize]];
      if (inInteriorWedge(v, prev, next, M, normalVec)) {
        targetSlot = e;
        break;
      }
    }

    // Commit the hole: only now, knowing it bridges, do its vertices get
    // ids and join vertexPositions (see the Hole struct's own comment).
    RtInt base = (RtInt) vertexPositions.size();
    std::vector<RtInt> ids(n);
    for (RtInt j = 0; j < n; j++) {
      ids[j] = base + j;
    }
    vertexPositions.insert(vertexPositions.end(), hole.points.begin(),
                            hole.points.end());
    vertexSlots.insert(vertexSlots.end(), hole.slots.begin(), hole.slots.end());
    const RtInt mId = ids[hole.rightmostLocal];

    // The bridge: ring[targetSlot], then the hole starting at M (reversed
    // traversal if the hole was wound the same way as the outer loop),
    // then M and ring[targetSlot] again -- a doubled edge, not a split
    // surface, since both occurrences share the same GMANVertex.
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
}

// The tail shared by getRSPolygon, getRSGeneralPolygon and every face of
// getRSPointsPolygon/getRSPointsGeneralPolygons: loops[0] is the outer
// boundary, loops[1..] holes bridged into it, then triangulated and
// shaded through buildPolygonObject. loopSlots (index-aligned with loops)
// names each vertex's own entry in pointTexCoords -- flat "P" order for a
// bare Polygon or GeneralPolygon, a face's own "verts" entries (indices
// into the shared "P") for a Points* request.
//
// Degeneracy is judged by a ratio, not an absolute area: twice the outer
// loop's area (normalVec's own magnitude, before normalizing) against the
// square of its largest bounding-box side. A polygon a million times
// longer than it is wide is degenerate at any scale, and this ratio reads
// the same wherever the polygon sits. A zero-extent ring -- every vertex
// identical -- is degenerate by definition; guarded directly rather than
// dividing by a zero-length side.
//
// Returns false, leaving body and vertRoot untouched, for a degenerate or
// under-three-point outer loop -- the caller skips the face rather than
// treating it as fatal.
bool buildFace(const std::vector<std::vector<GMANPoint>> &loops,
               const std::vector<std::vector<RtInt>> &loopSlots,
               const std::vector<GMANPolygonVertexTexCoord> &pointTexCoords,
               RtInt sides, RtToken orientation,
               const GMANShadingContext &shading, GMANBody *&body,
               GMANVertex *&vertRoot) {
  const std::vector<GMANPoint> &outer = loops[0];
  if (outer.size() < 3) {
    return false;
  }
  RtFloat outerBboxSide = boundingBoxExtent(outer);
  GMANVector normalVec = newellNormal(outer);
  RtFloat normalMagnitude = normalVec.magnitude();
  if (outerBboxSide == (RtFloat) 0.0 ||
      normalMagnitude <
          kTriangulationTolerance * outerBboxSide * outerBboxSide) {
    return false;  // fully degenerate: no plane worth shading or filling
  }
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
  bridgeHoles(loops, loopSlots, normalVec, outerBboxSide, vertexLocations,
              vertexSlots, ring);

  std::vector<GMANPolygonVertexTexCoord> texCoords(vertexLocations.size());
  for (std::size_t i = 0; i < vertexSlots.size(); i++) {
    texCoords[i] = pointTexCoords[vertexSlots[i]];
  }

  GMANObject *object = buildPolygonObject(vertexLocations, ring, normalVec,
                                           sides, orientation, shading,
                                           texCoords);
  body = object->getBody();
  vertRoot = object->getVert();
  object->setBody(NULL);
  object->setVert(NULL);
  delete object;
  return true;
}

// Appends one surviving face's body and vertex chain onto a mesh's own
// running chains -- GMANObject's destructor already walks both
// (getNext() on each), so one object can own every face's worth once
// they are linked here (settled decision "One primitive, many bodies").
// A face contributes more than one vertex, unlike GMANBody's single node,
// so its own chain's tail has to be found by walking.
void appendFace(GMANBody *faceBody, GMANVertex *faceVert,
                 GMANBody *&bodyHead, GMANBody *&bodyTail,
                 GMANVertex *&vertHead, GMANVertex *&vertTail) {
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
  GMANVertex *last = faceVert;
  while (last->getNext()) {
    last = last->getNext();
  }
  vertTail = last;
}

}  // namespace


/*
 * RenderMan API GMANPatchPolyObjectManager
 *
 */

// default constructor
GMANPatchPolyObjectManager::GMANPatchPolyObjectManager() : GMANObjectManager() { };


// default destructor 
GMANPatchPolyObjectManager::~GMANPatchPolyObjectManager() { };


GMANPrimitive* GMANPatchPolyObjectManager::create(RtVoid) {
  return new GMANObject();
}

GMANPrimitive * GMANPatchPolyObjectManager::getRSPolygon (RtInt nverts,
							  GMANParameterList pl,
							  GMANOptions *opt,
							  GMANAttributes *attr,
							  GMANTransform *t)
 {
  // A Polygon is required to be planar and simple, not necessarily convex.
  // triangulateEarClipping below handles concave input correctly; a fan
  // from vertex 0 would silently fill the wrong region the moment a
  // reflex vertex's diagonal left the polygon (see triangulateEarClipping
  // and newellNormal's own comments for why each is needed).
  //
  // nverts < 3 is degenerate input, and a "P" absent from this attribute
  // scope's parameter list is malformed RiPolygon/RiPolygonV input (public
  // API, callable with no "P" at all, bypassing whatever the RIB parser
  // enforces): both degrade to the same empty stub every other malformed
  // shape in this codebase falls back to, rather than indexing past data
  // that was never there.
  if (nverts < 3) {
    return create();
  }
  RtFloat *p = (RtFloat *)
      pl.getPointer(standardDictionary().getTokenId(RI_P));
  if (! p) {
    return create();
  }

  RtInt sides = attr->getSides();
  RtToken orientation = attr->getOrientation();
  GMANShadingContext shading = resolveShadingContext(attr, opt);

  std::vector<GMANPoint> location(nverts);
  std::vector<RtInt> slots(nverts);
  for (RtInt i = 0; i < nverts; i++) {
    location[i] = t->apply(GMANPoint(p[3 * i], p[3 * i + 1], p[3 * i + 2]));
    slots[i] = i;
  }

  // A Polygon is a one-loop GeneralPolygon: buildFace's own comment covers
  // the degeneracy guard, the bridging (a no-op with one loop and no
  // holes) and the triangulation this shares with every other polygon
  // face.
  std::vector<GMANPolygonVertexTexCoord> texCoords =
      resolvePolygonTextureCoordinates(pl, nverts, p);

  GMANBody *body;
  GMANVertex *vertRoot;
  if (! buildFace({location}, {slots}, texCoords, sides, orientation,
		  shading, body, vertRoot)) {
    return create();
  }
  GMANObject *object = new GMANObject();
  object->setBody(body);
  object->setVert(vertRoot);
  return object;
};

GMANPrimitive * GMANPatchPolyObjectManager::getRSGeneralPolygon (RtInt nloops,
								 RtInt nverts[],
								 GMANParameterList pl,
								 GMANOptions *opt,
								 GMANAttributes *attr,
								 GMANTransform *t)
 {
  // Loop 0 is the outer boundary; RiGeneralPolygonV rejects nloops < 1 and
  // any negative nverts[i] before this ever runs (see its own comment), so
  // this guard only matters to a direct, white-box caller.
  if (nloops < 1) {
    return create();
  }
  RtFloat *p = (RtFloat *)
      pl.getPointer(standardDictionary().getTokenId(RI_P));
  if (! p) {
    return create();
  }

  std::vector<std::vector<GMANPoint>> loops(nloops);
  std::vector<std::vector<RtInt>> loopSlots(nloops);
  RtInt offset = 0;
  for (RtInt i = 0; i < nloops; i++) {
    RtInt count = nverts[i] > 0 ? nverts[i] : 0;
    loops[i].resize(count);
    loopSlots[i].resize(count);
    for (RtInt j = 0; j < count; j++) {
      loops[i][j] =
	  t->apply(GMANPoint(p[3 * (offset + j)], p[3 * (offset + j) + 1],
			      p[3 * (offset + j) + 2]));
      loopSlots[i][j] = offset + j;
    }
    offset += count;
  }

  // Resolved once, over the whole flat "P" order every loop was unpacked
  // from above -- bridgeHoles (inside buildFace) then reports which of
  // these slots each committed vertex carries, since it commits holes in
  // descending-rightmostU order, not this order.
  std::vector<GMANPolygonVertexTexCoord> pointTexCoords =
      resolvePolygonTextureCoordinates(pl, offset, p);

  RtInt sides = attr->getSides();
  RtToken orientation = attr->getOrientation();
  GMANShadingContext shading = resolveShadingContext(attr, opt);

  GMANBody *body;
  GMANVertex *vertRoot;
  if (! buildFace(loops, loopSlots, pointTexCoords, sides, orientation,
		  shading, body, vertRoot)) {
    return create();
  }
  GMANObject *object = new GMANObject();
  object->setBody(body);
  object->setVert(vertRoot);
  return object;
};

GMANPrimitive * GMANPatchPolyObjectManager::getRSPointsPolygon (RtInt npolys,
								RtInt nverts[],
								RtInt verts[],
								GMANParameterList pl,
								GMANOptions *opt,
								GMANAttributes *attr,
								GMANTransform *t)
 {
  // Direct, white-box caller guard, as getRSGeneralPolygon's own
  // nloops < 1 guard is -- RiPointsPolygonsV rejects npolys < 0 before
  // this ever runs, and npolys == 0 draws nothing either way.
  if (npolys < 1) {
    return create();
  }
  RtFloat *p = (RtFloat *)
      pl.getPointer(standardDictionary().getTokenId(RI_P));
  if (! p) {
    return create();
  }

  RtInt totalVerts = 0;
  for (RtInt i = 0; i < npolys; i++) {
    totalVerts += nverts[i] > 0 ? nverts[i] : 0;
  }
  // 1 + max(verts): RiSpec's own vertex/varying count for this request --
  // one "P"/"s"/"t"/"st" entry per point the mesh actually references.
  RtInt pointCount = 0;
  for (RtInt i = 0; i < totalVerts; i++) {
    if (verts[i] + 1 > pointCount) {
      pointCount = verts[i] + 1;
    }
  }

  // Resolved once, over the shared "P" a point at a time -- not per face,
  // and not in "verts" order -- so a point three faces share still reads
  // the same "s"/"t"/"st" wherever it is referenced from.
  std::vector<GMANPolygonVertexTexCoord> pointTexCoords =
      resolvePolygonTextureCoordinates(pl, pointCount, p);

  RtInt sides = attr->getSides();
  RtToken orientation = attr->getOrientation();
  GMANShadingContext shading = resolveShadingContext(attr, opt);

  // Faceted (settled decision "Faces"): every face gathers its own
  // GMANVertex objects through "verts", one PointsPolygons face being a
  // one-loop GeneralPolygon (buildFace). A degenerate face is skipped,
  // not fatal; every surviving face's body and vertex chain joins one
  // GMANObject (settled decision "One primitive, many bodies").
  GMANBody *bodyHead = NULL, *bodyTail = NULL;
  GMANVertex *vertHead = NULL, *vertTail = NULL;

  RtInt offset = 0;
  for (RtInt i = 0; i < npolys; i++) {
    RtInt count = nverts[i] > 0 ? nverts[i] : 0;
    std::vector<GMANPoint> loop(count);
    std::vector<RtInt> slots(count);
    for (RtInt j = 0; j < count; j++) {
      RtInt pointIndex = verts[offset + j];
      loop[j] = t->apply(GMANPoint(p[3 * pointIndex], p[3 * pointIndex + 1],
				    p[3 * pointIndex + 2]));
      slots[j] = pointIndex;
    }
    offset += count;

    GMANBody *faceBody;
    GMANVertex *faceVert;
    if (buildFace({loop}, {slots}, pointTexCoords, sides, orientation,
		  shading, faceBody, faceVert)) {
      appendFace(faceBody, faceVert, bodyHead, bodyTail, vertHead, vertTail);
    }
  }

  if (! bodyHead) {
    return create();  // no face survived: the whole mesh is the empty stub
  }
  GMANObject *object = new GMANObject();
  object->setBody(bodyHead);
  object->setVert(vertHead);
  return object;
};

GMANPrimitive * GMANPatchPolyObjectManager::getRSPointsGeneralPolygons (RtInt npolys,
									RtInt nloops[],
									RtInt nverts[],
									RtInt verts[],
									GMANParameterList pl,
									GMANOptions *opt,
									GMANAttributes *attr,
									GMANTransform *t)
 {
  if (npolys < 1) {
    return create();
  }
  RtFloat *p = (RtFloat *)
      pl.getPointer(standardDictionary().getTokenId(RI_P));
  if (! p) {
    return create();
  }

  RtInt sumNloops = 0;
  for (RtInt i = 0; i < npolys; i++) {
    sumNloops += nloops[i] > 0 ? nloops[i] : 0;
  }
  RtInt totalVerts = 0;
  for (RtInt i = 0; i < sumNloops; i++) {
    totalVerts += nverts[i] > 0 ? nverts[i] : 0;
  }
  RtInt pointCount = 0;
  for (RtInt i = 0; i < totalVerts; i++) {
    if (verts[i] + 1 > pointCount) {
      pointCount = verts[i] + 1;
    }
  }

  std::vector<GMANPolygonVertexTexCoord> pointTexCoords =
      resolvePolygonTextureCoordinates(pl, pointCount, p);

  RtInt sides = attr->getSides();
  RtToken orientation = attr->getOrientation();
  GMANShadingContext shading = resolveShadingContext(attr, opt);

  GMANBody *bodyHead = NULL, *bodyTail = NULL;
  GMANVertex *vertHead = NULL, *vertTail = NULL;

  RtInt loopOffset = 0, vertOffset = 0;
  for (RtInt i = 0; i < npolys; i++) {
    RtInt faceLoops = nloops[i] > 0 ? nloops[i] : 0;
    if (faceLoops == 0) {
      continue;  // no outer loop at all: degenerate, skip
    }

    std::vector<std::vector<GMANPoint>> loops(faceLoops);
    std::vector<std::vector<RtInt>> loopSlots(faceLoops);
    for (RtInt li = 0; li < faceLoops; li++) {
      RtInt count = nverts[loopOffset + li] > 0 ? nverts[loopOffset + li] : 0;
      loops[li].resize(count);
      loopSlots[li].resize(count);
      for (RtInt j = 0; j < count; j++) {
	RtInt pointIndex = verts[vertOffset + j];
	loops[li][j] = t->apply(GMANPoint(p[3 * pointIndex],
					   p[3 * pointIndex + 1],
					   p[3 * pointIndex + 2]));
	loopSlots[li][j] = pointIndex;
      }
      vertOffset += count;
    }
    loopOffset += faceLoops;

    GMANBody *faceBody;
    GMANVertex *faceVert;
    if (buildFace(loops, loopSlots, pointTexCoords, sides, orientation,
		  shading, faceBody, faceVert)) {
      appendFace(faceBody, faceVert, bodyHead, bodyTail, vertHead, vertTail);
    }
  }

  if (! bodyHead) {
    return create();
  }
  GMANObject *object = new GMANObject();
  object->setBody(bodyHead);
  object->setVert(vertHead);
  return object;
};

GMANPrimitive * GMANPatchPolyObjectManager::getRSPatch (RtToken type,
							GMANParameterList pl,
							GMANOptions *opt,
							GMANAttributes *attr,
							GMANTransform *t)
 {
  RtFloat *p = (RtFloat *)
      pl.getPointer(standardDictionary().getTokenId(RI_P));
  if (! p) {
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
GMANPrimitive * GMANPatchPolyObjectManager::getRSPatchMesh (RtToken type,
							    RtInt nu,
							    RtToken uwrap,
							    RtInt nv,
							    RtToken vwrap,
							    GMANParameterList pl,
							    GMANOptions *opt,
							    GMANAttributes *attr,
							    GMANTransform *t)
 {
  RtFloat *p = (RtFloat *)
      pl.getPointer(standardDictionary().getTokenId(RI_P));
  if (! p) {
    return create();
  }

  if (strcmp(type, RI_BILINEAR) == 0) {
    if (! validBilinearMeshDim(nu) || ! validBilinearMeshDim(nv)) {
      warning("PatchMesh \"bilinear\": nu={} nv={} cannot form a patch; "
	      "ignoring.", nu, nv);
      return create();
    }
    GMANPatchMesh mesh(type, p, nu, uwrap, nv, vwrap, pl);
    return createParametric(&mesh, t, attr, kIdentityCorners, opt);
  }
  if (strcmp(type, RI_BICUBIC) == 0) {
    GMANBasis basis = attr->getUVBasis();
    bool uPeriodic = strcmp(uwrap, RI_PERIODIC) == 0;
    bool vPeriodic = strcmp(vwrap, RI_PERIODIC) == 0;
    if (! validBicubicMeshDim(nu, uPeriodic, basis.getUStep()) ||
	! validBicubicMeshDim(nv, vPeriodic, basis.getVStep())) {
      warning("PatchMesh \"bicubic\": nu={} nv={} does not align to the "
	      "current basis step; ignoring.", nu, nv);
      return create();
    }
    GMANPatchMesh mesh(type, p, nu, uwrap, nv, vwrap, basis, pl);
    return createParametric(&mesh, t, attr, kIdentityCorners, opt);
  }
  return create();
};

// Texture coordinates on NuPatch are out of scope (see the settled decision
// above getRSPatchMesh): kIdentityCorners stands in for
// resolveParametricCorners here too, for the same reason -- a NuPatch's
// varying values form a (nusegments+1) x (nvsegments+1) grid, which
// resolveParametricCorners' fixed four-corner shape only fits in the
// bilinear-equivalent case.
GMANPrimitive * GMANPatchPolyObjectManager::getRSNuPatch (RtInt nu,
							  RtInt uorder,
							  RtFloat uknot[],
							  RtFloat umin,
							  RtFloat umax,
							  RtInt nv,
							  RtInt vorder,
							  RtFloat vknot[],
							  RtFloat vmin,
							  RtFloat vmax,
							  GMANParameterList pl,
							  GMANOptions *opt,
							  GMANAttributes *attr,
							  GMANTransform *t)
 {
  // "Pw" wins over "P" when both are supplied; "P" alone means w = 1.
  RtFloat *pw = (RtFloat *)
      pl.getPointer(standardDictionary().getTokenId(RI_PW));
  bool rational = pw != NULL;
  RtFloat *p = rational ? pw : (RtFloat *)
      pl.getPointer(standardDictionary().getTokenId(RI_P));
  if (! p) {
    return create();
  }

  GMANNuPatch patch(nu, uorder, uknot, umin, umax, nv, vorder, vknot, vmin,
		     vmax, p, rational, pl);
  return createParametric(&patch, t, attr, kIdentityCorners, opt);
};

GMANPrimitive * GMANPatchPolyObjectManager::getRSSphere (RtFloat radius,
							 RtFloat zmin,
							 RtFloat zmax,
							 RtFloat tmax,
							 GMANParameterList pl,
							 GMANOptions *opt,
							 GMANAttributes *attr,
							 GMANTransform *t)
 {
  GMANSphere sphere(radius, zmin, zmax, tmax, pl);
  return createParametric(&sphere, t, attr, resolveParametricCorners(pl, attr), opt);
};

GMANPrimitive * GMANPatchPolyObjectManager::getRSCone (RtFloat height,
						       RtFloat radius,
						       RtFloat tmax,
						       GMANParameterList pl, 
						       GMANOptions *opt,
						       GMANAttributes *attr,
						       GMANTransform *t)
 {
  GMANCone cone(height, radius, tmax, pl);
  return createParametric(&cone, t, attr, resolveParametricCorners(pl, attr), opt);
};

GMANPrimitive * GMANPatchPolyObjectManager::getRSCylinder (RtFloat radius,
							   RtFloat zmin,
							   RtFloat zmax,
							   RtFloat tmax,
							   GMANParameterList pl,
							   GMANOptions *opt,
							   GMANAttributes *attr,
							   GMANTransform *t)
 {
  GMANCylinder cylinder(radius, zmin, zmax, tmax, pl);
  return createParametric(&cylinder, t, attr, resolveParametricCorners(pl, attr), opt);
};

GMANPrimitive * GMANPatchPolyObjectManager::getRSHyperboloid (RtPoint point1,
							      RtPoint point2,
							      RtFloat tmax,
							      GMANParameterList pl,
							      GMANOptions *opt,
							      GMANAttributes *attr,
							      GMANTransform *t)
 {
  GMANHyperboloid hyperboloid(point1, point2, tmax, pl);
  return createParametric(&hyperboloid, t, attr, resolveParametricCorners(pl, attr), opt);
};

GMANPrimitive * GMANPatchPolyObjectManager::getRSParaboloid (RtFloat rmax,
							     RtFloat zmin,
							     RtFloat zmax,
							     RtFloat tmax,
							     GMANParameterList pl,
							     GMANOptions *opt,
							     GMANAttributes *attr,
							     GMANTransform *t)
 {
  GMANParaboloid paraboloid(rmax, zmin, zmax, tmax, pl);
  return createParametric(&paraboloid, t, attr, resolveParametricCorners(pl, attr), opt);
};

GMANPrimitive * GMANPatchPolyObjectManager::getRSDisk (RtFloat height,
						       RtFloat radius,
						       RtFloat tmax,
						       GMANParameterList pl,
						       GMANOptions *opt,
						       GMANAttributes *attr,
						       GMANTransform *t)
 {
  GMANDisk disk(height, radius, tmax, pl);
  return createParametric(&disk, t, attr, resolveParametricCorners(pl, attr), opt);
};

GMANPrimitive * GMANPatchPolyObjectManager::getRSTorus (RtFloat majrad,
							RtFloat minrad,
							RtFloat phimin,
							RtFloat phimax,
							RtFloat tmax,
							GMANParameterList pl,
							GMANOptions *opt,
							GMANAttributes *attr,
							GMANTransform *t)
 {
  GMANTorus torus(majrad, minrad, phimin, phimax, tmax, pl);
  return createParametric(&torus, t, attr, resolveParametricCorners(pl, attr), opt);
};

GMANPrimitive * GMANPatchPolyObjectManager::getRSBlobby (RtInt /*nleaf*/,
							 RtInt /*ncode*/,
							 RtInt /*code*/[],
							 RtInt /*nflt*/,
							 RtFloat /*flt*/[],
							 RtInt /*nstr*/,
							 RtToken /*str*/[], 
							 GMANParameterList /*pl*/,
							 GMANOptions */*opt*/,
							 GMANAttributes */*attr*/,
							 GMANTransform */*t*/)
 {
  return create();
};

GMANPrimitive * GMANPatchPolyObjectManager::getRSPoints (RtInt /*npoints*/,
							 GMANParameterList /*pl*/,
							 GMANOptions */*opt*/,
							 GMANAttributes */*attr*/,
							 GMANTransform */*t*/)
 {
  return create();
};

GMANPrimitive * GMANPatchPolyObjectManager::getRSCurves (RtToken /*type*/,
							 RtInt /*ncurves*/, 
							 RtInt /*nvertices*/[],
							 RtToken /*wrap*/,
							 GMANParameterList /*pl*/,
							 GMANOptions */*opt*/,
							 GMANAttributes */*attr*/,
							 GMANTransform */*t*/)
 {
  return create();
};

GMANPrimitive * GMANPatchPolyObjectManager::getRSSubdivisionMesh (RtToken /*mask*/,
								  RtInt /*nf*/,
								  RtInt /*nverts*/[],
								  RtInt /*verts*/[],
								  RtInt /*ntags*/,
								  RtToken /*tags*/[],
								  RtInt /*numargs*/[],
								  RtInt /*intargs*/[],
								  RtFloat /*floatargs*/[],
								  GMANParameterList /*pl*/,
								  GMANOptions */*opt*/,
								  GMANAttributes */*attr*/,
								  GMANTransform */*t*/)
 {
  return create();
};


GMANObject* GMANPatchPolyObjectManager::createParametric (GMANParametric* p,
							  GMANTransform* t,
							  GMANAttributes* attr,
							  const GMANTextureCoordinates &corners,
							  GMANOptions const *opt)
{
#define URES 16
#define VRES 16
  int i, j;
  RtInt sides = attr->getSides();
  RtToken orientation = attr->getOrientation();

  // The vertex shading normal is object-space (p->getNormal), unlike the
  // face's geometric normal below, which gets to camera space for free as
  // a side effect of crossing already-transformed edges. A plain normal
  // does not get that gift: it needs the CTM's inverse transpose, computed
  // once per primitive rather than once per vertex. Row-vector convention
  // (p*M, translation in row 3) makes the inverse-transpose of M's linear
  // part exactly Minv's own upper-left 3x3 block used as n*Minv -- see
  // AGENTS.md's "Matrix convention" note and phase-3-REPORT.md.
  GMANMatrix4 ctmInv = t->interpolate(0.0);
  ctmInv.invert();

  // Shading setup, resolved once per primitive rather than once per vertex.
  GMANShadingContext shading = resolveShadingContext(attr, opt);

  GMANVertex** vertices = new GMANVertex*[(URES + 1) * (VRES + 1)];
  GMANFace** faces = new GMANFace*[URES * VRES];
  GMANBody* body = new GMANBody(GMANColor(), GMANColor());
  GMANSurface* surface = new GMANSurface(body);
  body->setSurface(surface);

  for (i = 0; i < (URES + 1) * (VRES + 1); i++)
  {
    vertices[i] = new GMANVertex();
  }

  for (i = 0; i <= URES; i++)
  {
    for (j = 0; j <= VRES; j++)
    {
      // Create a vertex
      double u = i / (double) URES;
      double v = j / (double) VRES;
      GMANPoint location = t->apply(p->getLocation(u, v));
      GMANVector objectNormal = p->getNormal(u, v);
      GMANVector normal(ctmInv[0][0] * objectNormal.getX() +
			 ctmInv[0][1] * objectNormal.getY() +
			 ctmInv[0][2] * objectNormal.getZ(),
			 ctmInv[1][0] * objectNormal.getX() +
			 ctmInv[1][1] * objectNormal.getY() +
			 ctmInv[1][2] * objectNormal.getZ(),
			 ctmInv[2][0] * objectNormal.getX() +
			 ctmInv[2][1] * objectNormal.getY() +
			 ctmInv[2][2] * objectNormal.getZ());
      normal.normalize();
      GMANVertex* vertex = vertices[(URES + 1) * i + j];
      vertex->setLocation(location);
      vertex->setNormal(normal);

      GMANNormal shadingNormal(normal.getX(), normal.getY(), normal.getZ());
      RtFloat s = bilerpCorner(u, v, corners.s1, corners.s2, corners.s3,
				corners.s4);
      RtFloat texT = bilerpCorner(u, v, corners.t1, corners.t2, corners.t3,
				   corners.t4);
      vertex->setColor(shadeVertex(shading, location, shadingNormal,
				    (RtFloat) u, (RtFloat) v, s, texT));
    }
  }

  // A second pass, now that every vertex in the grid has a finalized
  // location: face(i,j) touches vertices (i,j+1) and (i+1,*), which are
  // not visited yet when (i,j) is, so calcNormal() run in the same pass
  // as vertex creation would cross-product against up to three
  // still-default-constructed (0,0,0) vertices -- a real, if invisible,
  // pre-existing bug. Invisible because nothing before this phase used
  // the resulting near-zero, direction-free normal for anything: the
  // renderer's own rasterization always reads vertex positions fresh at
  // render time, long after this function returns, so geometry was never
  // affected -- only RiSides 1 culling, which silently culled and kept
  // faces close to at random. See phase-3-REPORT.md.
  for (i = 0; i < URES; i++)
  {
    for (j = 0; j < VRES; j++)
    {
      GMANVertex* faceVertices[4];
      faceVertices[0] = vertices[(URES + 1) * i + j];
      faceVertices[1] = vertices[(URES + 1) * i + (j + 1)];
      faceVertices[2] = vertices[(URES + 1) * (i + 1) + (j + 1)];
      faceVertices[3] = vertices[(URES + 1) * (i + 1) + j];
      faces[URES * i + j] = new GMANFace(faceVertices, surface);
      // Geometric normal, computed from the already-transformed (camera
      // space) vertices: cross(e1', e2') for e'=e*M is proportional to
      // (e1 x e2) transformed by M's inverse transpose, so this needs no
      // separate normal transform. RiSides/RiOrientation travel with the
      // face so visible() can answer without depending on renderer-global
      // state that may differ across attribute blocks.
      faces[URES * i + j]->calcNormal();
      faces[URES * i + j]->setSides(sides);
      faces[URES * i + j]->setOrientation(orientation);
    }
  }

  for (i = 0; i < (URES + 1) * (VRES + 1) - 1; i++)
  {
    vertices[i]->setNext(vertices[i + 1]);
  }
  for (i = 0; i < URES * VRES - 1; i++)
  {
    faces[i]->setNext(faces[i + 1]);
  }
  surface->setFace(faces[0]);

  GMANObject* object = (GMANObject*) create();
  object->setVert(vertices[0]);
  object->setBody(body);

  delete[] vertices;
  delete[] faces;

  return object;
}
