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
 * A vertex the clipper introduces at a plane crossing carries the colour
 * and alpha interpolated at the crossing: the end vertices' values weighted
 * by the crossing's fraction t along the edge.
 *
 * Under fov=90 the LEFT plane's boundary at z=5 is x=-5. A quad spans
 * x=-7 (colour 0, alpha 0.2) to x=-4 (colour 1, alpha 0.8), so each edge
 * crosses at t = (-5 - -7) / (-4 - -7) = 2/3 and the two clip vertices
 * read colour 0 + (1 - 0) * 2/3 = 2/3 and alpha 0.2 + (0.8 - 0.2) * 2/3 =
 * 0.6. A weight that is the edge's end alpha instead gives colour 0.8 and
 * alpha 0.68.
 *
 * Revert check: restore GMANClipEdge::intersect's colour and alpha weights
 * to e.getAlpha() and the clip-vertex colour and alpha assertions go red.
 */

#include <cmath>

#include "check.h"
#include "gmancolor.h"
#include "gmanface.h"
#include "gmanmatrix4.h"
#include "gmanoptions.h"
#include "gmanoutputpolygon.h"
#include "gmanpoint.h"
#include "gmanpolygonclipper.h"
#include "gmanvertex.h"
#include "gmanvsperspective.h"

namespace {

constexpr RtFloat tolerance = 1e-3;

bool near(RtFloat a, RtFloat b) { return std::fabs(a - b) <= tolerance; }

bool colorIs(GMANColor const& c, RtFloat v) {
  return near(c.getRed(), v) && near(c.getGreen(), v) && near(c.getBlue(), v);
}

bool alphaIs(GMANAlpha const& a, RtFloat v) {
  return near(a.getRed(), v) && near(a.getGreen(), v) && near(a.getBlue(), v);
}

GMANOptions::ScreenWindowStruct squareWindow() {
  GMANOptions::ScreenWindowStruct sw;
  sw.left = -1.0;
  sw.right = 1.0;
  sw.bottom = -1.0;
  sw.top = 1.0;
  return sw;
}

void setVertex(GMANVertex& v, GMANPoint const& p, RtFloat color, RtFloat alpha) {
  v.setLocation(p);
  v.setColor(GMANColor(color));
  v.setColor(GMANAlpha(alpha));
}

void testClipVertexInterpolation() {
  GMANMatrix4 identity;
  gman::VSPerspective vs(100, 100, squareWindow(), identity, 90.0, 1.0, 100.0);

  GMANVertex v0, v1, v2, v3;
  setVertex(v0, GMANPoint(-7.0, -0.1, 5.0), 0.0, 0.2);
  setVertex(v1, GMANPoint(-4.0, -0.1, 5.0), 1.0, 0.8);
  setVertex(v2, GMANPoint(-4.0, 0.1, 5.0), 1.0, 0.8);
  setVertex(v3, GMANPoint(-7.0, 0.1, 5.0), 0.0, 0.2);
  GMANVertex* verts[4] = {&v0, &v1, &v2, &v3};
  GMANFace quad(verts, nullptr);

  GMANPolygonClipper clipper;
  GMANOutputPolygon out;
  const int n = clipper.clip(&quad, out, &vs);
  check(n == 4, "clip: a quad straddling the LEFT plane keeps 4 vertices");
  if (n != 4) {
    return;
  }

  int onPlane = 0;
  int inside = 0;
  for (int i = 0; i < n; ++i) {
    if (near(out.getVertexPosn(i).getX(), -1.0)) {
      ++onPlane;
      check(colorIs(out.getVertexColor(i), 2.0 / 3.0), "clip: a clip vertex reads the colour interpolated at t=2/3");
      check(alphaIs(out.getVertexAlpha(i), 0.6), "clip: a clip vertex reads the alpha interpolated at t=2/3");
    } else {
      ++inside;
      check(colorIs(out.getVertexColor(i), 1.0), "clip: an inside vertex keeps its colour");
      check(alphaIs(out.getVertexAlpha(i), 0.8), "clip: an inside vertex keeps its alpha");
    }
  }
  check(onPlane == 2 && inside == 2, "clip: two vertices land on the LEFT plane and two stay inside");
}

} // namespace

int main() {
  testClipVertexInterpolation();

  return checkSummary("clip colour holds");
}
