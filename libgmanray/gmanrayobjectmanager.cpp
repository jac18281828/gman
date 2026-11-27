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
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "gmanattributes.h"
#include "gmanbasis.h"
#include "gmandictionary.h"
#include "gmaninvertiblematrix.h"
#include "gmanlog.h"
#include "gmanobjectmanager.h"
#include "gmanoptions.h"
#include "gmanparameterlist.h"
#include "gmanpoint.h"
#include "gmanpolygon.h"
#include "gmanpolygonmesh.h"
#include "gmanprimitive.h"
#include "gmanprimitives.h"
#include "gmanraycone.h"
#include "gmanraycylinder.h"
#include "gmanraydisk.h"
#include "gmanrayhyperboloid.h"
#include "gmanrayobjectmanager.h"
#include "gmanrayparaboloid.h"
#include "gmanraypolygon.h"
#include "gmanraypolygonmesh.h"
#include "gmanrayprimitive.h"
#include "gmanraysphere.h"
#include "gmanraytorus.h"
#include "gmanshading.h"
#include "gmantransform.h"
#include "gmanvector.h"
#include "ri.h"

namespace {

constexpr RtInt kGridLines = gman::kParametricDiceCount + 1;

// One polygon request's face: loopVerts[0] the outer boundary, loopVerts[1..]
// holes, each a run of indices into the mesh's shared point pool. Rejects a
// degenerate outer loop exactly as the z-buffer's buildFace does; a hole
// needs no texCoords or normals, so only the outer loop's are gathered.
bool buildMeshFace(std::vector<std::vector<RtInt>> const& loopVerts, RtFloat const* p,
                   std::vector<std::pair<RtFloat, RtFloat>> const& pointTexCoords,
                   std::vector<GMANVector> const& pointNormals, GMANTransform* t,
                   std::unique_ptr<GMANRayPolygon>& outFace) {
  std::vector<std::vector<GMANPoint>> loops(loopVerts.size());
  for (std::size_t li = 0; li < loopVerts.size(); li++) {
    loops[li].resize(loopVerts[li].size());
    for (std::size_t j = 0; j < loopVerts[li].size(); j++) {
      RtInt const pointIndex = loopVerts[li][j];
      loops[li][j] = t->apply(GMANPoint(p[3 * pointIndex], p[3 * pointIndex + 1], p[3 * pointIndex + 2]));
    }
  }
  if (loops.empty() || gman::isDegeneratePolygon(loops[0])) {
    return false;
  }

  std::vector<std::pair<RtFloat, RtFloat>> outerTexCoords(loopVerts[0].size());
  for (std::size_t j = 0; j < loopVerts[0].size(); j++) {
    outerTexCoords[j] = pointTexCoords[loopVerts[0][j]];
  }

  std::vector<GMANVector> outerNormals;
  if (!pointNormals.empty()) {
    outerNormals.reserve(loopVerts[0].size());
    for (RtInt const pointIndex : loopVerts[0]) {
      outerNormals.push_back(pointNormals[pointIndex]);
    }
  }

  std::vector<GMANPoint> outer = std::move(loops[0]);
  std::vector<std::vector<GMANPoint>> holes(std::make_move_iterator(loops.begin() + 1),
                                            std::make_move_iterator(loops.end()));
  outFace = std::make_unique<GMANRayPolygon>(std::move(outer), std::move(holes), std::move(outerTexCoords),
                                             std::move(outerNormals));
  return true;
}

using TexCoord = std::pair<RtFloat, RtFloat>;

struct GridVertex {
  GMANPoint position;
  GMANVector normal;
  TexCoord texCoord;
};

struct Wrap {
  bool u;
  bool v;
};

struct MeshAxis {
  RtInt patches;
  bool periodic;
  RtInt corners() const { return periodic ? patches : patches + 1; }
};

struct MeshCell {
  RtInt first;
  RtInt second;
  double local;
};

struct PatchMeshTexture {
  GMANTextureCoordinates corners;
  MeshAxis uAxis;
  MeshAxis vAxis;
  std::span<RtFloat const> st;
  std::span<RtFloat const> s;
  std::span<RtFloat const> t;
};

RtFloat bilerp(double u, double v, RtFloat c00, RtFloat c10, RtFloat c01, RtFloat c11) {
  return (RtFloat)((1.0 - u) * (1.0 - v) * c00 + u * (1.0 - v) * c10 + (1.0 - u) * v * c01 + u * v * c11);
}

TexCoord cornerTexCoord(GMANTextureCoordinates const& c, double u, double v) {
  return {bilerp(u, v, c.s1, c.s2, c.s3, c.s4), bilerp(u, v, c.t1, c.t2, c.t3, c.t4)};
}

RtInt bilinearPatchCount(RtInt n, bool periodic) { return periodic ? n : n - 1; }

RtInt bicubicPatchCount(RtInt n, bool periodic, RtInt step) {
  if (periodic) {
    return n / step;
  }
  return (n - 4) / step + 1;
}

MeshCell locate(MeshAxis const& axis, double parameter) {
  RtInt const start = std::clamp((RtInt)std::floor(parameter * axis.patches), 0, axis.patches - 1);
  RtInt next = start + 1;
  if (next >= axis.corners()) {
    next -= axis.corners();
  }
  return {start, next, parameter * axis.patches - start};
}

std::span<RtFloat const> varyingValues(GMANParameterList const& pl, std::string_view name, std::size_t perCorner,
                                       std::size_t corners) {
  std::optional<GMANTokenEntry::TokenClass> const storage = pl.storageClass(name);
  if (!storage || *storage != GMANTokenEntry::VARYING) {
    return {};
  }
  std::span<RtFloat const> const values = pl.floats(name);
  if (values.size() < perCorner * corners) {
    warning("PatchMesh \"{}\" holds {} floats, {} expected; ignoring it.", name, values.size(), perCorner * corners);
    return {};
  }
  return values;
}

PatchMeshTexture patchMeshTexture(GMANParameterList const& pl, GMANAttributes const& attr, MeshAxis const& uAxis,
                                  MeshAxis const& vAxis) {
  std::size_t const corners = (std::size_t)uAxis.corners() * (std::size_t)vAxis.corners();
  return {attr.getTextureCoordinates(),
          uAxis,
          vAxis,
          varyingValues(pl, "st", 2, corners),
          varyingValues(pl, "s", 1, corners),
          varyingValues(pl, "t", 1, corners)};
}

RtFloat blendVarying(std::span<RtFloat const> values, std::size_t stride, std::size_t component,
                     std::array<std::size_t, 4> const& corner, double u, double v) {
  return bilerp(u, v, values[stride * corner[0] + component], values[stride * corner[1] + component],
                values[stride * corner[2] + component], values[stride * corner[3] + component]);
}

TexCoord patchMeshTexCoord(PatchMeshTexture const& texture, double u, double v) {
  MeshCell const uCell = locate(texture.uAxis, u);
  MeshCell const vCell = locate(texture.vAxis, v);
  std::size_t const columns = (std::size_t)texture.uAxis.corners();
  std::array<std::size_t, 4> const corner = {
      (std::size_t)uCell.first + columns * (std::size_t)vCell.first,
      (std::size_t)uCell.second + columns * (std::size_t)vCell.first,
      (std::size_t)uCell.first + columns * (std::size_t)vCell.second,
      (std::size_t)uCell.second + columns * (std::size_t)vCell.second,
  };

  TexCoord texCoord = cornerTexCoord(texture.corners, u, v);
  if (!texture.st.empty()) {
    texCoord.first = blendVarying(texture.st, 2, 0, corner, uCell.local, vCell.local);
    texCoord.second = blendVarying(texture.st, 2, 1, corner, uCell.local, vCell.local);
  }
  if (!texture.s.empty()) {
    texCoord.first = blendVarying(texture.s, 1, 0, corner, uCell.local, vCell.local);
  }
  if (!texture.t.empty()) {
    texCoord.second = blendVarying(texture.t, 1, 0, corner, uCell.local, vCell.local);
  }
  return texCoord;
}

bool isFinite(GMANPoint const& p) {
  return std::isfinite(p.getX()) && std::isfinite(p.getY()) && std::isfinite(p.getZ());
}

bool isUsableNormal(GMANVector normal) {
  RtFloat const magnitude = normal.magnitude();
  return magnitude > (RtFloat)0.0 && std::isfinite(magnitude);
}

std::optional<std::vector<RtFloat>> controlPoints(std::string_view request, GMANParameterList const& pl,
                                                  std::size_t floatCount) {
  std::span<RtFloat const> const p = pl.floats("P");
  if (p.size() < floatCount) {
    warning("{}: \"P\" holds {} floats, {} expected; ignoring.", request, p.size(), floatCount);
    return std::nullopt;
  }
  std::span<RtFloat const> const used = p.first(floatCount);
  if (!std::ranges::all_of(used, [](RtFloat value) { return std::isfinite(value); })) {
    warning("{}: \"P\" holds a non-finite value; ignoring.", request);
    return std::nullopt;
  }
  return std::vector<RtFloat>(used.begin(), used.end());
}

std::unique_ptr<GMANRayPolygon> triangleFace(GridVertex const& a, GridVertex const& b, GridVertex const& c) {
  std::vector<GMANPoint> ring = {a.position, b.position, c.position};
  if (gman::isDegeneratePolygon(ring)) {
    return nullptr;
  }
  std::vector<GMANVector> normals;
  if (isUsableNormal(a.normal) && isUsableNormal(b.normal) && isUsableNormal(c.normal)) {
    normals = {a.normal, b.normal, c.normal};
  }
  return std::make_unique<GMANRayPolygon>(std::move(ring), std::vector<std::vector<GMANPoint>>{},
                                          std::vector<TexCoord>{a.texCoord, b.texCoord, c.texCoord},
                                          std::move(normals));
}

template <typename TexCoordAt>
std::optional<std::vector<GridVertex>> gridVertices(std::string_view request, GMANParametric& surface, Wrap wrap,
                                                    TexCoordAt const& texCoordAt, GMANTransform* t) {
  std::optional<gman::InvertibleMatrix> const placement = gman::InvertibleMatrix::of(t->interpolate(0.0));

  std::vector<GridVertex> grid;
  grid.reserve((std::size_t)kGridLines * (std::size_t)kGridLines);
  for (RtInt i = 0; i < kGridLines; i++) {
    for (RtInt j = 0; j < kGridLines; j++) {
      double const u = i / (double)gman::kParametricDiceCount;
      double const v = j / (double)gman::kParametricDiceCount;
      RtInt firstI = i;
      RtInt firstJ = j;
      if (wrap.u && i == gman::kParametricDiceCount) {
        firstI = 0;
      }
      if (wrap.v && j == gman::kParametricDiceCount) {
        firstJ = 0;
      }
      if (firstI != i || firstJ != j) {
        GridVertex closing = grid[(std::size_t)firstI * (std::size_t)kGridLines + (std::size_t)firstJ];
        closing.texCoord = texCoordAt(u, v);
        grid.push_back(closing);
        continue;
      }
      GMANPoint const position = t->apply(surface.getLocation(u, v));
      if (!isFinite(position)) {
        warning("{}: the surface holds a non-finite point; ignoring.", request);
        return std::nullopt;
      }
      GMANVector normal;
      if (placement) {
        normal = gman::transformNormal(*placement, surface.getNormal(u, v));
        normal.normalize();
      }
      grid.push_back({position, normal, texCoordAt(u, v)});
    }
  }
  return grid;
}

template <typename TexCoordAt>
std::unique_ptr<GMANRayPolygonMesh> tessellate(std::string_view request, GMANParametric& surface, Wrap wrap,
                                               TexCoordAt const& texCoordAt, GMANAttributes const& attr,
                                               GMANTransform* t) {
  std::optional<std::vector<GridVertex>> const grid = gridVertices(request, surface, wrap, texCoordAt, t);
  if (!grid) {
    return nullptr;
  }
  auto const vertexAt = [&](RtInt i, RtInt j) -> GridVertex const& {
    return (*grid)[(std::size_t)i * (std::size_t)kGridLines + (std::size_t)j];
  };

  gman::Appearance const appearance = gman::appearanceOf(attr);
  std::vector<std::unique_ptr<GMANRayPolygon>> faces;
  auto const addFace = [&](std::unique_ptr<GMANRayPolygon> face) {
    if (face) {
      face->setAppearance(appearance);
      faces.push_back(std::move(face));
    }
  };
  for (RtInt i = 0; i < gman::kParametricDiceCount; i++) {
    for (RtInt j = 0; j < gman::kParametricDiceCount; j++) {
      GridVertex const& a = vertexAt(i, j);
      GridVertex const& b = vertexAt(i + 1, j);
      GridVertex const& c = vertexAt(i, j + 1);
      GridVertex const& d = vertexAt(i + 1, j + 1);
      addFace(triangleFace(a, b, c));
      addFace(triangleFace(b, d, c));
    }
  }
  if (faces.empty()) {
    return nullptr;
  }
  auto mesh = std::make_unique<GMANRayPolygonMesh>(std::move(faces));
  mesh->setAppearance(appearance);
  return mesh;
}

std::unique_ptr<GMANRayPolygonMesh> tessellatePatchMesh(GMANParametric& surface, MeshAxis const& uAxis,
                                                        MeshAxis const& vAxis, GMANParameterList const& pl,
                                                        GMANAttributes const& attr, GMANTransform* t) {
  PatchMeshTexture const texture = patchMeshTexture(pl, attr, uAxis, vAxis);
  auto const texCoordAt = [&texture](double u, double v) { return patchMeshTexCoord(texture, u, v); };
  return tessellate("PatchMesh", surface, {uAxis.periodic, vAxis.periodic}, texCoordAt, attr, t);
}

} // namespace

/*
 * RenderMan API GMANRayObjectManager
 *
 */

// default constructor
GMANRayObjectManager::GMANRayObjectManager() : GMANObjectManager() {};

// default destructor
GMANRayObjectManager::~GMANRayObjectManager() {};

GMANPrimitive* GMANRayObjectManager::create(RtVoid) { return new GMANRayPrimitive(); }

// One call for every polygon request, a one-face Polygon included: one
// GMANRayPolygonMesh when a face survives, the empty stub when none does,
// never a bare GMANRayPolygon. GMANRayBVH::build flattens a mesh into the
// same entries a bare polygon gives, so a one-face mesh's box and
// appearance already equal its own face's.
GMANPrimitive* GMANRayObjectManager::getRSPolygonMesh(GMANPolygonMesh const& mesh, GMANOptions* /*opt*/,
                                                      GMANAttributes* attr, GMANTransform* t) {
  std::span<RtFloat const> const p = mesh.points();
  std::size_t const pointCount = p.size() / 3;

  std::vector<std::pair<RtFloat, RtFloat>> pointTexCoords = gman::resolvePointTexCoords(mesh.parameters(), pointCount);
  std::vector<GMANVector> const normals = gman::cameraSpacePointNormals(mesh.parameters(), pointCount, t);
  gman::Appearance const appearance = gman::appearanceOf(*attr);

  std::vector<std::unique_ptr<GMANRayPolygon>> faces;
  for (std::size_t i = 0; i < mesh.faceCount(); i++) {
    std::span<RtInt const> const faceLoops = mesh.face(i);
    std::vector<std::vector<RtInt>> loopVerts(faceLoops.size());
    for (std::size_t li = 0; li < faceLoops.size(); li++) {
      std::span<RtInt const> const indices = mesh.loop(i, li);
      loopVerts[li].assign(indices.begin(), indices.end());
    }

    std::unique_ptr<GMANRayPolygon> face;
    if (buildMeshFace(loopVerts, p.data(), pointTexCoords, normals, t, face)) {
      face->setAppearance(appearance);
      faces.push_back(std::move(face));
    }
  }

  if (faces.empty()) {
    return create();
  }
  const auto rayMesh = new GMANRayPolygonMesh(std::move(faces));
  rayMesh->setAppearance(appearance);
  return rayMesh;
}

GMANPrimitive* GMANRayObjectManager::getRSPatch(RtToken type, GMANParameterList pl, GMANOptions* /*opt*/,
                                                GMANAttributes* attr, GMANTransform* t) {
  if (pl.floats("P").empty()) {
    return create();
  }

  GMANTextureCoordinates const corners = gman::resolveParametricCorners(pl, *attr);
  auto const texCoordAt = [&corners](double u, double v) { return cornerTexCoord(corners, u, v); };
  std::unique_ptr<GMANRayPolygonMesh> mesh;
  if (std::string_view(type) == RI_BILINEAR) {
    std::optional<std::vector<RtFloat>> points = controlPoints("Patch \"bilinear\"", pl, 12);
    if (!points) {
      return create();
    }
    GMANPatch patch(type, points->data(), pl);
    mesh = tessellate("Patch \"bilinear\"", patch, {false, false}, texCoordAt, *attr, t);
  } else if (std::string_view(type) == RI_BICUBIC) {
    std::optional<std::vector<RtFloat>> points = controlPoints("Patch \"bicubic\"", pl, 48);
    if (!points) {
      return create();
    }
    GMANBasis const basis = attr->getUVBasis();
    GMANPatch patch(type, points->data(), basis, pl);
    mesh = tessellate("Patch \"bicubic\"", patch, {false, false}, texCoordAt, *attr, t);
  }
  if (!mesh) {
    return create();
  }
  return mesh.release();
};

GMANPrimitive* GMANRayObjectManager::getRSPatchMesh(RtToken type, RtInt nu, RtToken uwrap, RtInt nv, RtToken vwrap,
                                                    GMANParameterList pl, GMANOptions* /*opt*/, GMANAttributes* attr,
                                                    GMANTransform* t) {
  if (pl.floats("P").empty()) {
    return create();
  }

  bool const uPeriodic = std::string_view(uwrap) == RI_PERIODIC;
  bool const vPeriodic = std::string_view(vwrap) == RI_PERIODIC;
  std::unique_ptr<GMANRayPolygonMesh> mesh;
  if (std::string_view(type) == RI_BILINEAR) {
    if (!gman::validBilinearMeshDim(nu) || !gman::validBilinearMeshDim(nv)) {
      warning("PatchMesh \"bilinear\": nu={} nv={} cannot form a patch; "
              "ignoring.",
              nu, nv);
      return create();
    }
    std::optional<std::vector<RtFloat>> points =
        controlPoints("PatchMesh \"bilinear\"", pl, (std::size_t)nu * (std::size_t)nv * 3);
    if (!points) {
      return create();
    }
    GMANPatchMesh patchMesh(type, points->data(), nu, uwrap, nv, vwrap, pl);
    mesh = tessellatePatchMesh(patchMesh, {bilinearPatchCount(nu, uPeriodic), uPeriodic},
                               {bilinearPatchCount(nv, vPeriodic), vPeriodic}, pl, *attr, t);
  } else if (std::string_view(type) == RI_BICUBIC) {
    GMANBasis const basis = attr->getUVBasis();
    if (!gman::validBicubicMeshDim(nu, uPeriodic, basis.getUStep()) ||
        !gman::validBicubicMeshDim(nv, vPeriodic, basis.getVStep())) {
      warning("PatchMesh \"bicubic\": nu={} nv={} does not align to the "
              "current basis step; ignoring.",
              nu, nv);
      return create();
    }
    std::optional<std::vector<RtFloat>> points =
        controlPoints("PatchMesh \"bicubic\"", pl, (std::size_t)nu * (std::size_t)nv * 3);
    if (!points) {
      return create();
    }
    GMANPatchMesh patchMesh(type, points->data(), nu, uwrap, nv, vwrap, basis, pl);
    mesh = tessellatePatchMesh(patchMesh, {bicubicPatchCount(nu, uPeriodic, basis.getUStep()), uPeriodic},
                               {bicubicPatchCount(nv, vPeriodic, basis.getVStep()), vPeriodic}, pl, *attr, t);
  }
  if (!mesh) {
    return create();
  }
  return mesh.release();
};

GMANPrimitive* GMANRayObjectManager::getRSNuPatch(RtInt /*nu*/, RtInt /*uorder*/, RtFloat /*uknot*/[], RtFloat /*umin*/,
                                                  RtFloat /*umax*/, RtInt /*nv*/, RtInt /*vorder*/, RtFloat /*vknot*/[],
                                                  RtFloat /*vmin*/, RtFloat /*vmax*/, GMANParameterList /*pl*/,
                                                  GMANOptions* /*opt*/, GMANAttributes* /*attr*/,
                                                  GMANTransform* /*t*/) {
  return create();
};

GMANPrimitive* GMANRayObjectManager::getRSSphere(RtFloat radius, RtFloat zmin, RtFloat zmax, RtFloat tmax,
                                                 GMANParameterList pl, GMANOptions* /*opt*/, GMANAttributes* attr,
                                                 GMANTransform* t) {
  const auto sphere = new GMANRaySphere(radius, zmin, zmax, tmax, pl, *t);
  sphere->setAppearance(gman::appearanceOf(*attr));
  return sphere;
};

GMANPrimitive* GMANRayObjectManager::getRSCone(RtFloat height, RtFloat radius, RtFloat tmax, GMANParameterList pl,
                                               GMANOptions* /*opt*/, GMANAttributes* attr, GMANTransform* t) {
  const auto cone = new GMANRayCone(height, radius, tmax, pl, *t);
  cone->setAppearance(gman::appearanceOf(*attr));
  return cone;
};

GMANPrimitive* GMANRayObjectManager::getRSCylinder(RtFloat radius, RtFloat zmin, RtFloat zmax, RtFloat tmax,
                                                   GMANParameterList pl, GMANOptions* /*opt*/, GMANAttributes* attr,
                                                   GMANTransform* t) {
  const auto cylinder = new GMANRayCylinder(radius, zmin, zmax, tmax, pl, *t);
  cylinder->setAppearance(gman::appearanceOf(*attr));
  return cylinder;
};

GMANPrimitive* GMANRayObjectManager::getRSHyperboloid(RtPoint point1, RtPoint point2, RtFloat tmax,
                                                      GMANParameterList pl, GMANOptions* /*opt*/, GMANAttributes* attr,
                                                      GMANTransform* t) {
  const auto hyperboloid = new GMANRayHyperboloid(point1, point2, tmax, pl, *t);
  hyperboloid->setAppearance(gman::appearanceOf(*attr));
  return hyperboloid;
};

GMANPrimitive* GMANRayObjectManager::getRSParaboloid(RtFloat rmax, RtFloat zmin, RtFloat zmax, RtFloat tmax,
                                                     GMANParameterList pl, GMANOptions* /*opt*/, GMANAttributes* attr,
                                                     GMANTransform* t) {
  const auto paraboloid = new GMANRayParaboloid(rmax, zmin, zmax, tmax, pl, *t);
  paraboloid->setAppearance(gman::appearanceOf(*attr));
  return paraboloid;
};

GMANPrimitive* GMANRayObjectManager::getRSDisk(RtFloat height, RtFloat radius, RtFloat tmax, GMANParameterList pl,
                                               GMANOptions* /*opt*/, GMANAttributes* attr, GMANTransform* t) {
  const auto disk = new GMANRayDisk(height, radius, tmax, pl, *t);
  disk->setAppearance(gman::appearanceOf(*attr));
  return disk;
};

GMANPrimitive* GMANRayObjectManager::getRSTorus(RtFloat majrad, RtFloat minrad, RtFloat phimin, RtFloat phimax,
                                                RtFloat tmax, GMANParameterList pl, GMANOptions* /*opt*/,
                                                GMANAttributes* attr, GMANTransform* t) {
  const auto torus = new GMANRayTorus(majrad, minrad, phimin, phimax, tmax, pl, *t);
  torus->setAppearance(gman::appearanceOf(*attr));
  return torus;
};

GMANPrimitive* GMANRayObjectManager::getRSBlobby(RtInt /*nleaf*/, RtInt /*ncode*/, RtInt /*code*/[], RtInt /*nflt*/,
                                                 RtFloat /*flt*/[], RtInt /*nstr*/, RtToken /*str*/[],
                                                 GMANParameterList /*pl*/, GMANOptions* /*opt*/,
                                                 GMANAttributes* /*attr*/, GMANTransform* /*t*/) {
  return create();
};

GMANPrimitive* GMANRayObjectManager::getRSPoints(RtInt /*npoints*/, GMANParameterList /*pl*/, GMANOptions* /*opt*/,
                                                 GMANAttributes* /*attr*/, GMANTransform* /*t*/) {
  return create();
};

GMANPrimitive* GMANRayObjectManager::getRSCurves(RtToken /*type*/, RtInt /*ncurves*/, RtInt /*nvertices*/[],
                                                 RtToken /*wrap*/, GMANParameterList /*pl*/, GMANOptions* /*opt*/,
                                                 GMANAttributes* /*attr*/, GMANTransform* /*t*/) {
  return create();
};

GMANPrimitive* GMANRayObjectManager::getRSSubdivisionMesh(RtToken /*mask*/, RtInt /*nf*/, RtInt /*nverts*/[],
                                                          RtInt /*verts*/[], RtInt /*ntags*/, RtToken /*tags*/[],
                                                          RtInt /*numargs*/[], RtInt /*intargs*/[],
                                                          RtFloat /*floatargs*/[], GMANParameterList /*pl*/,
                                                          GMANOptions* /*opt*/, GMANAttributes* /*attr*/,
                                                          GMANTransform* /*t*/) {
  return create();
};
