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

#include <cstddef>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "gmandictionary.h"
#include "gmaninvertiblematrix.h"
#include "gmanlog.h"
#include "gmanparameterlist.h"
#include "gmanpoint.h"
#include "gmanpolygon.h"
#include "gmantransform.h"
#include "gmanvector.h"
#include "ri.h"

namespace {

// A pure number, not an area: every comparison it guards is a ratio (a
// Newell-normal magnitude divided by an extent squared) rather than a raw
// area, so it reads the same wherever a polygon sits or however large it
// is. float carries about seven decimal digits, so a ratio built from a
// cross-product sum and a division carries absolute error near 1e-7; this
// sits an order above that noise and far below any turn or offset a real
// polygon intends.
const RtFloat kDegeneracyTolerance = (RtFloat)1.0e-6;

std::string_view storageClassName(GMANTokenEntry::TokenClass storage) {
  switch (storage) {
  case GMANTokenEntry::CONSTANT:
    return "constant";
  case GMANTokenEntry::UNIFORM:
    return "uniform";
  case GMANTokenEntry::VARYING:
    return "varying";
  case GMANTokenEntry::VERTEX:
    return "vertex";
  case GMANTokenEntry::FACEVARYING:
    return "facevarying";
  }
  return "unknown";
}

} // namespace

namespace gman {

RtFloat boundingBoxExtent(std::vector<GMANPoint> const& ring) {
  RtFloat minX = ring[0].getX(), maxX = minX;
  RtFloat minY = ring[0].getY(), maxY = minY;
  RtFloat minZ = ring[0].getZ(), maxZ = minZ;
  for (std::size_t i = 1; i < ring.size(); i++) {
    GMANPoint const& pt = ring[i];
    if (pt.getX() < minX)
      minX = pt.getX();
    if (pt.getX() > maxX)
      maxX = pt.getX();
    if (pt.getY() < minY)
      minY = pt.getY();
    if (pt.getY() > maxY)
      maxY = pt.getY();
    if (pt.getZ() < minZ)
      minZ = pt.getZ();
    if (pt.getZ() > maxZ)
      maxZ = pt.getZ();
  }
  RtFloat extent = maxX - minX;
  if (maxY - minY > extent)
    extent = maxY - minY;
  if (maxZ - minZ > extent)
    extent = maxZ - minZ;
  return extent;
}

GMANVector newellNormal(std::vector<GMANPoint> const& ring) {
  GMANVector sum;
  std::size_t const n = ring.size();
  for (std::size_t i = 0; i < n; i++) {
    GMANPoint const& cur = ring[i];
    GMANPoint const& next = ring[(i + 1) % n];
    sum.setX(sum.getX() + (cur.getY() - next.getY()) * (cur.getZ() + next.getZ()));
    sum.setY(sum.getY() + (cur.getZ() - next.getZ()) * (cur.getX() + next.getX()));
    sum.setZ(sum.getZ() + (cur.getX() - next.getX()) * (cur.getY() + next.getY()));
  }
  return sum;
}

bool isDegeneratePolygon(std::vector<GMANPoint> const& ring) {
  if (ring.size() < 3) {
    return true;
  }
  RtFloat const extent = boundingBoxExtent(ring);
  if (extent == (RtFloat)0.0) {
    return true; // every vertex identical: degenerate by definition
  }
  GMANVector normal = newellNormal(ring);
  return normal.magnitude() < kDegeneracyTolerance * extent * extent;
}

GMANDictionary& standardDictionary() {
  static GMANDictionary d;
  return d;
}

std::vector<std::pair<RtFloat, RtFloat>> polygonTexCoords(GMANParameterList const& pl, RtFloat const* p,
                                                          std::size_t count) {
  RtFloat* sArr = gman::floatArray(pl, RI_S);
  RtFloat* tArr = gman::floatArray(pl, RI_T);
  RtFloat* stArr = gman::floatArray(pl, RI_ST);

  std::vector<std::pair<RtFloat, RtFloat>> coords(count);
  for (std::size_t i = 0; i < count; i++) {
    RtFloat s = p[3 * i];
    RtFloat t = p[3 * i + 1];
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
    coords[i] = {s, t};
  }
  return coords;
}

std::vector<GMANVector> cameraSpacePointNormals(GMANParameterList const& parameters, std::size_t pointCount,
                                                GMANTransform* t) {
  std::optional<GMANTokenEntry::TokenClass> const storage = parameters.storageClass(RI_N);
  if (!storage) {
    return {};
  }
  if (*storage != GMANTokenEntry::VERTEX && *storage != GMANTokenEntry::VARYING) {
    warning("Polygon \"N\" is {}, not vertex or varying: shading flat.", storageClassName(*storage));
    return {};
  }
  std::span<RtFloat const> const n = parameters.floats(RI_N);
  if (n.size() != 3 * pointCount) {
    warning("Polygon \"N\" holds {} floats, {} expected for {} points: shading flat.", n.size(), 3 * pointCount,
            pointCount);
    return {};
  }
  std::optional<gman::InvertibleMatrix> const placement = gman::InvertibleMatrix::of(t->interpolate(0.0));
  if (!placement) {
    return {};
  }

  std::vector<GMANVector> normals(pointCount);
  for (std::size_t i = 0; i < pointCount; i++) {
    normals[i] = gman::transformNormal(*placement, GMANVector(n[3 * i], n[3 * i + 1], n[3 * i + 2]));
    normals[i].normalize();
  }
  return normals;
}

} // namespace gman
