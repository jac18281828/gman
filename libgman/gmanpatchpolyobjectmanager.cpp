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
};

GMANShadingContext resolveShadingContext(GMANAttributes *attr) {
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
  return ctx;
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

// A ring vertex's turning direction relative to the polygon's own normal:
// positive is convex, negative reflex, zero for a collinear or duplicate
// vertex. Testing against this normal, rather than against the ring's own
// winding, keeps the result correct whichever way the ring winds --
// normal already followed that winding when Newell's method built it.
RtFloat turnOrientation(const GMANPoint &prev, const GMANPoint &cur,
                         const GMANPoint &next, const GMANVector &normal) {
  GMANVector e1(prev, cur);
  GMANVector e2(cur, next);
  return e1.cross(e2).dot(normal);
}

// True when p lies inside or on the boundary of coplanar triangle
// (a, b, c): on the same side of every edge, judged by that edge's cross
// product with the vector to p, dotted against the polygon's normal so
// the test does not depend on which way the triangle happens to wind.
bool pointInTriangle(const GMANPoint &a, const GMANPoint &b,
                      const GMANPoint &c, const GMANPoint &p,
                      const GMANVector &normal) {
  RtFloat d0 = GMANVector(a, b).cross(GMANVector(a, p)).dot(normal);
  RtFloat d1 = GMANVector(b, c).cross(GMANVector(b, p)).dot(normal);
  RtFloat d2 = GMANVector(c, a).cross(GMANVector(c, p)).dot(normal);
  bool hasNeg = d0 < 0.0 || d1 < 0.0 || d2 < 0.0;
  bool hasPos = d0 > 0.0 || d1 > 0.0 || d2 > 0.0;
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

  while (remaining.size() > 3) {
    const RtInt m = (RtInt) remaining.size();

    std::vector<RtInt> reflex;
    std::vector<RtFloat> orient(m);
    for (RtInt i = 0; i < m; i++) {
      const RtInt iPrev = remaining[(i + m - 1) % m];
      const RtInt iCur = remaining[i];
      const RtInt iNext = remaining[(i + 1) % m];
      orient[i] =
          turnOrientation(ring[iPrev], ring[iCur], ring[iNext], normal);
      if (orient[i] < -RI_EPSILON) {
        reflex.push_back(iCur);
      }
    }

    RtInt clipAt = -1;
    RtInt fallbackAt = 0;
    RtFloat fallbackOrient = orient[0];
    for (RtInt i = 0; i < m; i++) {
      if (orient[i] > fallbackOrient) {
        fallbackOrient = orient[i];
        fallbackAt = i;
      }
      if (orient[i] < -RI_EPSILON) {
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
      bool degenerate = orient[i] <= RI_EPSILON;
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
        break;
      }
    }

    // No true ear tested empty: only reachable from malformed
    // (self-intersecting) input, since a simple polygon always has one.
    // Clip the least-reflex candidate anyway -- degrade, do not hang.
    if (clipAt < 0) {
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
							  GMANOptions */*opt*/,
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

  GMANShadingContext shading = resolveShadingContext(attr);

  std::vector<GMANPoint> location(nverts);
  for (RtInt i = 0; i < nverts; i++) {
    location[i] = t->apply(GMANPoint(p[3 * i], p[3 * i + 1], p[3 * i + 2]));
  }

  // One geometric normal for the whole polygon: a Polygon is required to
  // be planar, so unlike a quadric's curved grid there is no per-vertex
  // object-space normal to source and no inverse transpose to compute.
  // Newell's method (see its own comment above) rather than three chosen
  // vertices, run once here so every vertex can be shaded with it before
  // any face exists and the triangulator below has a reference to
  // classify ears against.
  GMANVector normalVec = newellNormal(location);
  if (normalVec.magnitude() < RI_EPSILON) {
    return create();  // fully degenerate: no plane to shade or fill
  }
  normalVec.normalize();
  GMANNormal normal(normalVec.getX(), normalVec.getY(), normalVec.getZ());

  GMANBody *body = new GMANBody(GMANColor(), GMANColor());
  GMANSurface *surface = new GMANSurface(body);
  body->setSurface(surface);

  GMANVertex **vertices = new GMANVertex*[nverts];
  for (RtInt i = 0; i < nverts; i++) {
    vertices[i] = new GMANVertex();
    vertices[i]->setLocation(location[i]);
    vertices[i]->setNormal(normalVec);

    // u/v/s/t have no meaning for a flat polygon; fixed rather than invented.
    vertices[i]->setColor(
	shadeVertex(shading, location[i], normal, 0.0, 0.0, 0.0, 0.0));
  }

  // Ear-clip into GMANFace's fixed 4-vertex shape, the 4th slot
  // duplicating the 3rd -- calcArea/calcNormal both read [0][1][2] or
  // collapse cleanly when [2]==[3], the same idiom every quadric's pole
  // face already uses for a degenerate triangle. A vertex count of
  // nverts always yields nverts - 2 triangles, concave or not.
  std::vector<std::array<RtInt, 3>> triangles =
      triangulateEarClipping(location, normalVec);
  RtInt nfaces = (RtInt) triangles.size();
  GMANFace **faces = new GMANFace*[nfaces];
  for (RtInt i = 0; i < nfaces; i++) {
    GMANVertex *faceVertices[4];
    faceVertices[0] = vertices[triangles[i][0]];
    faceVertices[1] = vertices[triangles[i][1]];
    faceVertices[2] = vertices[triangles[i][2]];
    faceVertices[3] = vertices[triangles[i][2]];
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

  GMANObject *object = (GMANObject *) create();
  object->setVert(vertices[0]);
  object->setBody(body);

  delete[] vertices;
  delete[] faces;

  return object;
};

GMANPrimitive * GMANPatchPolyObjectManager::getRSGeneralPolygon (RtInt /*nloops*/, 
								 RtInt /*nverts*/[], 
								 GMANParameterList /*pl*/,
								 GMANOptions */*opt*/,
								 GMANAttributes */*attr*/,
								 GMANTransform */*t*/)
 {
  return create();
};

GMANPrimitive * GMANPatchPolyObjectManager::getRSPointsPolygon (RtInt /*npolys*/, 
								RtInt /*nverts*/[], 
								RtInt /*verts*/[],
								GMANParameterList /*pl*/,
								GMANOptions */*opt*/,
								GMANAttributes */*attr*/,
								GMANTransform */*t*/)
 {
  return create();
};

GMANPrimitive * GMANPatchPolyObjectManager::getRSPointsGeneralPolygons (RtInt /*npolys*/, 
									RtInt /*nloops*/[],
									RtInt /*nverts*/[], 
									RtInt /*verts*/[],
									GMANParameterList /*pl*/,
									GMANOptions */*opt*/,
									GMANAttributes */*attr*/,
									GMANTransform */*t*/)
 {
  return create();
};

GMANPrimitive * GMANPatchPolyObjectManager::getRSPatch (RtToken type,
							GMANParameterList pl,
							GMANOptions */*opt*/,
							GMANAttributes *attr,
							GMANTransform *t)
 {
  RtFloat *p = (RtFloat *)
      pl.getPointer(standardDictionary().getTokenId(RI_P));
  if (! p) {
    return create();
  }

  if (strcmp(type, RI_BILINEAR) == 0) {
    GMANPatch patch(type, p, pl);
    return createParametric(&patch, t, attr);
  }
  if (strcmp(type, RI_BICUBIC) == 0) {
    GMANBasis basis = attr->getUVBasis();
    GMANPatch patch(type, p, basis, pl);
    return createParametric(&patch, t, attr);
  }
  return create();
};

GMANPrimitive * GMANPatchPolyObjectManager::getRSPatchMesh (RtToken type,
							    RtInt nu,
							    RtToken uwrap,
							    RtInt nv,
							    RtToken vwrap,
							    GMANParameterList pl,
							    GMANOptions */*opt*/,
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
      warning("PatchMesh \"bilinear\": nu=%d nv=%d cannot form a patch; "
	      "ignoring.", nu, nv);
      return create();
    }
    GMANPatchMesh mesh(type, p, nu, uwrap, nv, vwrap, pl);
    return createParametric(&mesh, t, attr);
  }
  if (strcmp(type, RI_BICUBIC) == 0) {
    GMANBasis basis = attr->getUVBasis();
    bool uPeriodic = strcmp(uwrap, RI_PERIODIC) == 0;
    bool vPeriodic = strcmp(vwrap, RI_PERIODIC) == 0;
    if (! validBicubicMeshDim(nu, uPeriodic, basis.getUStep()) ||
	! validBicubicMeshDim(nv, vPeriodic, basis.getVStep())) {
      warning("PatchMesh \"bicubic\": nu=%d nv=%d does not align to the "
	      "current basis step; ignoring.", nu, nv);
      return create();
    }
    GMANPatchMesh mesh(type, p, nu, uwrap, nv, vwrap, basis, pl);
    return createParametric(&mesh, t, attr);
  }
  return create();
};

GMANPrimitive * GMANPatchPolyObjectManager::getRSNuPatch (RtInt /*nu*/,
							  RtInt /*uorder*/,
							  RtFloat /*uknot*/[],
							  RtFloat /*umin*/,
							  RtFloat /*umax*/,
							  RtInt /*nv*/,
							  RtInt /*vorder*/,
							  RtFloat /*vknot*/[],
							  RtFloat /*vmin*/,
							  RtFloat /*vmax*/,
							  GMANParameterList /*pl*/,
							  GMANOptions */*opt*/,
							  GMANAttributes */*attr*/,
							  GMANTransform */*t*/)
 {
  return create();
};

GMANPrimitive * GMANPatchPolyObjectManager::getRSSphere (RtFloat radius,
							 RtFloat zmin,
							 RtFloat zmax,
							 RtFloat tmax,
							 GMANParameterList pl,
							 GMANOptions */*opt*/,
							 GMANAttributes *attr,
							 GMANTransform *t)
 {
  GMANSphere sphere(radius, zmin, zmax, tmax, pl);
  return createParametric(&sphere, t, attr);
};

GMANPrimitive * GMANPatchPolyObjectManager::getRSCone (RtFloat height,
						       RtFloat radius,
						       RtFloat tmax,
						       GMANParameterList pl, 
						       GMANOptions */*opt*/,
						       GMANAttributes *attr,
						       GMANTransform *t)
 {
  GMANCone cone(height, radius, tmax, pl);
  return createParametric(&cone, t, attr);
};

GMANPrimitive * GMANPatchPolyObjectManager::getRSCylinder (RtFloat radius,
							   RtFloat zmin,
							   RtFloat zmax,
							   RtFloat tmax,
							   GMANParameterList pl,
							   GMANOptions */*opt*/,
							   GMANAttributes *attr,
							   GMANTransform *t)
 {
  GMANCylinder cylinder(radius, zmin, zmax, tmax, pl);
  return createParametric(&cylinder, t, attr);
};

GMANPrimitive * GMANPatchPolyObjectManager::getRSHyperboloid (RtPoint point1,
							      RtPoint point2,
							      RtFloat tmax,
							      GMANParameterList pl,
							      GMANOptions */*opt*/,
							      GMANAttributes *attr,
							      GMANTransform *t)
 {
  GMANHyperboloid hyperboloid(point1, point2, tmax, pl);
  return createParametric(&hyperboloid, t, attr);
};

GMANPrimitive * GMANPatchPolyObjectManager::getRSParaboloid (RtFloat rmax,
							     RtFloat zmin,
							     RtFloat zmax,
							     RtFloat tmax,
							     GMANParameterList pl,
							     GMANOptions */*opt*/,
							     GMANAttributes *attr,
							     GMANTransform *t)
 {
  GMANParaboloid paraboloid(rmax, zmin, zmax, tmax, pl);
  return createParametric(&paraboloid, t, attr);
};

GMANPrimitive * GMANPatchPolyObjectManager::getRSDisk (RtFloat height,
						       RtFloat radius,
						       RtFloat tmax,
						       GMANParameterList pl,
						       GMANOptions */*opt*/,
						       GMANAttributes *attr,
						       GMANTransform *t)
 {
  GMANDisk disk(height, radius, tmax, pl);
  return createParametric(&disk, t, attr);
};

GMANPrimitive * GMANPatchPolyObjectManager::getRSTorus (RtFloat majrad,
							RtFloat minrad,
							RtFloat phimin,
							RtFloat phimax,
							RtFloat tmax,
							GMANParameterList pl,
							GMANOptions */*opt*/,
							GMANAttributes *attr,
							GMANTransform *t)
 {
  GMANTorus torus(majrad, minrad, phimin, phimax, tmax, pl);
  return createParametric(&torus, t, attr);
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
							  GMANAttributes* attr)
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
  GMANShadingContext shading = resolveShadingContext(attr);

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
      vertex->setColor(shadeVertex(shading, location, shadingNormal,
				    (RtFloat) u, (RtFloat) v,
				    (RtFloat) u, (RtFloat) v));
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
