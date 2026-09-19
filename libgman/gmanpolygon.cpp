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

#include "gmanpolygon.h"

namespace {

// A pure number, not an area: every comparison it guards is a ratio (a
// Newell-normal magnitude divided by an extent squared) rather than a raw
// area, so it reads the same wherever a polygon sits or however large it
// is. float carries about seven decimal digits, so a ratio built from a
// cross-product sum and a division carries absolute error near 1e-7; this
// sits an order above that noise and far below any turn or offset a real
// polygon intends.
const RtFloat kDegeneracyTolerance = (RtFloat)1.0e-6;

// The ring's largest bounding-box side, in whichever of x, y or z spans it
// widest. isDegeneratePolygon judges the ring's area against this extent
// rather than against an absolute constant, so a sliver a million times
// longer than it is wide reads the same way at any scale.
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

} // namespace

namespace gman {

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

} // namespace gman
