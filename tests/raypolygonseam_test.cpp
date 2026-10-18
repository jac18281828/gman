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
 * A ray through the boundary of a polygon's outer ring hits it, so two
 * polygons that share an edge leave no gap: a ceiling and two walls meet
 * along edges on each side of every projected axis, and a ray landing
 * exactly on such an edge hits one of its neighbours, whichever side of the
 * ring the edge is. A ray 1e-3 past the edge misses, and the closure's
 * tolerance of 1e-5 of the ring's extent sits between a ray 1e-5 past the
 * edge (a hit) and one 5e-5 past (a miss). Opening the boundary again
 * (comparing the point's distance to the ring against a zero tolerance, or
 * dropping the closure) fails the rows on the ring's max sides and the 1e-5
 * row.
 */

#include <string>

#include "check.h"
#include "gmanray.h"
#include "gmanraypolygon.h"

namespace {

// The ceiling in the plane y == 1, spanning x in [-1, 1] and z in [1, 3].
GMANRayPolygon ceiling() {
  std::vector<GMANPoint> verts = {GMANPoint(-1.0, 1.0, 1.0), GMANPoint(1.0, 1.0, 1.0), GMANPoint(1.0, 1.0, 3.0),
                                  GMANPoint(-1.0, 1.0, 3.0)};
  return GMANRayPolygon(verts, GMANParameterList());
}

// A wall in the plane x == wallX, spanning y in [-1, 1] and z in [1, 3].
GMANRayPolygon wall(RtFloat wallX) {
  std::vector<GMANPoint> verts = {GMANPoint(wallX, -1.0, 1.0), GMANPoint(wallX, 1.0, 1.0), GMANPoint(wallX, 1.0, 3.0),
                                  GMANPoint(wallX, -1.0, 3.0)};
  return GMANRayPolygon(verts, GMANParameterList());
}

bool anyHit(GMANRayPolygon const& c, GMANRayPolygon const& w, GMANRayPolygon const& wLeft, GMANRay const& ray) {
  GMANHit hit;
  return c.intersect(ray, hit) || w.intersect(ray, hit) || wLeft.intersect(ray, hit);
}

struct Row {
  char const* name;
  GMANPoint origin;
  GMANVector direction;
  bool expectHit;
  char const* what;
};

} // namespace

int main() {
  GMANRayPolygon const c = ceiling();
  GMANRayPolygon const w = wall(1.0);
  GMANRayPolygon const wLeft = wall(-1.0);

  GMANVector const alongX(1.0, 0.0, 0.0);
  GMANVector const alongY(0.0, 1.0, 0.0);

  // Integer origins along an axis land each hit exactly on a ring boundary.
  Row const rows[] = {
      {"a", GMANPoint(-5.0, 1.0, 2.0), alongX, true, "on the walls' y == 1 edge, first coordinate's max side"},
      {"b", GMANPoint(-5.0, -1.0, 2.0), alongX, true, "on the walls' y == -1 edge, first coordinate's min side"},
      {"c", GMANPoint(1.0, -5.0, 2.0), alongY, true, "on the ceiling's x == 1 edge, first coordinate's max side"},
      {"d", GMANPoint(-1.0, -5.0, 2.0), alongY, true, "on the ceiling's x == -1 edge, first coordinate's min side"},
      {"e", GMANPoint(-5.0, 0.0, 3.0), alongX, true, "on the walls' z == 3 edge, second coordinate's max side"},
      {"f", GMANPoint(-5.0, 0.0, 1.0), alongX, true, "on the walls' z == 1 edge, second coordinate's min side"},
      {"g", GMANPoint(0.0, -5.0, 3.0), alongY, true, "on the ceiling's z == 3 edge, second coordinate's max side"},
      {"h", GMANPoint(0.0, -5.0, 1.0), alongY, true, "on the ceiling's z == 1 edge, second coordinate's min side"},
      // The walls' extent is 2, so the tolerance is 2e-5. In float, 1.00001
      // lies 1.0e-5 past the edge, half the tolerance; 1.00005 lies 5.0e-5
      // past, 2.5 times the tolerance.
      {"j", GMANPoint(-5.0, 1.001, 2.0), alongX, false, "1e-3 past the walls' y == 1 edge"},
      {"k", GMANPoint(-5.0, 1.00001, 2.0), alongX, true, "1e-5 past the walls' y == 1 edge"},
      {"m", GMANPoint(-5.0, 1.00005, 2.0), alongX, false, "5e-5 past the walls' y == 1 edge"},
  };

  for (Row const& row : rows) {
    GMANRay const ray(row.origin, row.direction);
    bool const hit = anyHit(c, w, wLeft, ray);
    check(hit == row.expectHit, std::string("row ") + row.name + ": a ray " + row.what +
                                    (row.expectHit ? " hits a neighbour" : " misses every neighbour"));
  }

  return checkSummary("GMANRayPolygon::intersect closes the outer ring's boundary within 1e-5 of its extent");
}
