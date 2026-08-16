/* SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Copyright (c) 2026 John Cairns <john@2ad.com>
 */

/*
 * Phase 1, proof items 1, 2, 4 and 5: a point behind the camera clips
 * rather than wrapping to a plausible pixel; a known world-space point
 * traced through world -> camera -> screen -> NDC -> raster lands on a
 * hand-computed pixel; a polygon straddling the near plane clips to the
 * expected vertex count and intersection point; and backface culling
 * gates on RiSides/RiOrientation rather than culling unconditionally.
 *
 * Revert checks (verified by actually reverting, not asserted):
 *   - Revert GMANTransform::apply to `return p;` (step 1): the full-chain
 *     test's camera-space point stops moving with the object's Translate,
 *     and the raster-pixel assertions go red.
 *   - Revert GMANClipEdge::intersect's `p = s.getCoord() + r*t;` (drop it,
 *     restoring the uninitialized-vector bug): the near-plane vertex count
 *     stays 4 but its NDC z is garbage, not -1.
 *   - Revert GMANClipEdge::clip's `output(isect, out)` back to
 *     `output(current, out)`: the near-plane intersection assertion goes
 *     red because no output vertex has NDC z near -1.
 *   - Revert GMANFace::calcNormal's call site in createParametric, or make
 *     GMANVSPerspective::visible() unconditional again: the backface test
 *     goes red for both winding orders (see the backface culling section).
 */

#include <cmath>
#include <cstdio>
#include <string>

#include "gmanmatrix4.h"
#include "gmantransform.h"
#include "gmanpoint.h"
#include "gmanvertex.h"
#include "gmanface.h"
#include "gmanpolygonclipper.h"
#include "gmanoutputpolygon.h"
#include "gmanvsperspective.h"
#include "gmanoptions.h"

namespace {

int failures = 0;

void check(bool ok, const std::string &what) {
  std::printf("%s: %s\n", ok ? "ok" : "FAIL", what.c_str());
  if (!ok) {
    ++failures;
  }
}

bool near(RtFloat a, RtFloat b, RtFloat tol) {
  return std::fabs(a - b) <= tol;
}

GMANOptions::ScreenWindowStruct squareWindow() {
  GMANOptions::ScreenWindowStruct sw;
  sw.left = -1.0; sw.right = 1.0; sw.bottom = -1.0; sw.top = 1.0;
  return sw;
}

// ---- proof item 2: the full chain, a known point to a known pixel ----
void testFullChain() {
  GMANOptions::ScreenWindowStruct sw = squareWindow();

  // Camera setup: "Translate 0 0 5" before RiWorldBegin, matching
  // tests/rib/sphere.rib.
  GMANMatrix4 worldToCamera;
  worldToCamera.trans(0.0, 0.0, 5.0);

  GMANVSPerspective vs(100, 100, sw, worldToCamera, 90.0, 1.0, 100.0);

  // The primitive's own CTM: worldToCamera as it stood at RiWorldBegin,
  // concatenated with "Translate 1 0 0" declared inside the world block --
  // exactly how GMANGraphicState::buildTransform builds a primitive's
  // transform, since RiWorldBegin doesn't reset the CTM (it snapshots it).
  GMANMatrix4 localTranslate;
  localTranslate.trans(1.0, 0.0, 0.0);
  GMANOneMatrix w2cStorage(worldToCamera);
  GMANTransform objectCTM(w2cStorage);
  GMANOneMatrix localStorage(localTranslate);
  GMANTransform localXform(localStorage);
  objectCTM.concat(localXform);

  // The sphere's own local origin -- object space, before any transform.
  GMANPoint objectSpace(0.0, 0.0, 0.0);
  GMANPoint cameraSpace = objectCTM.apply(objectSpace);
  check(near(cameraSpace.getX(), 1.0, 1e-4) &&
        near(cameraSpace.getY(), 0.0, 1e-4) &&
        near(cameraSpace.getZ(), 5.0, 1e-4),
        "full chain: object CTM carries object space to camera space "
        "(camera Translate 0 0 5, object Translate 1 0 0 -> (1,0,5))");

  // fov=90 -> invT=1, so ndc = (x/z, y/z) = (0.2, 0). Screen window is
  // [-1,1]^2 and the raster is 100x100, so raster.x = 100*(0.2+1)/2 = 60,
  // raster.y = 100 - 100*(0+1)/2 = 50.
  GMANPoint raster = vs.project(cameraSpace);
  check(near(raster.getX(), 60.0, 0.5),
        "full chain: known point lands at the hand-computed raster x (60)");
  check(near(raster.getY(), 50.0, 0.5),
        "full chain: known point lands at the hand-computed raster y (50)");
}

// ---- proof item 1: a point behind the camera clips ----
void testBehindCameraClips() {
  GMANOptions::ScreenWindowStruct sw = squareWindow();
  GMANMatrix4 identity;
  GMANVSPerspective vs(100, 100, sw, identity, 90.0, 1.0, 100.0);

  // All four corners sit behind the camera (negative camera-space z);
  // none can survive near-plane clipping.
  GMANVertex a, b, c, d;
  a.setLocation(GMANPoint(-1.0, -1.0, -5.0));
  b.setLocation(GMANPoint( 1.0, -1.0, -5.0));
  c.setLocation(GMANPoint( 1.0,  1.0, -5.0));
  d.setLocation(GMANPoint(-1.0,  1.0, -5.0));
  GMANVertex *verts[4] = {&a, &b, &c, &d};
  GMANFace behind(verts, nullptr);

  GMANPolygonClipper clipper;
  GMANOutputPolygon out;
  int n = clipper.clip(&behind, out, &vs);
  check(n == 0,
        "clip: a face entirely behind the camera clips away completely, "
        "rather than wrapping to a plausible-looking pixel");
}

// ---- proof item 4: a polygon straddling the near plane ----
void testClipperNearPlane() {
  GMANOptions::ScreenWindowStruct sw = squareWindow();
  GMANMatrix4 identity;
  GMANVSPerspective vs(100, 100, sw, identity, 90.0, 1.0, 100.0);

  // A quad with two vertices too close (z=0.5, inside near=1) and two
  // safely in the frustum (z=5). x,y stay small enough that only the
  // FRONT (near) plane clips; hand-derived expected result: 4 output
  // vertices, two of them exactly on the near plane (NDC z = -1).
  GMANVertex v0, v1, v2, v3;
  v0.setLocation(GMANPoint(-0.1, -0.1, 0.5));
  v1.setLocation(GMANPoint( 0.1, -0.1, 0.5));
  v2.setLocation(GMANPoint( 0.1,  0.1, 5.0));
  v3.setLocation(GMANPoint(-0.1,  0.1, 5.0));
  GMANVertex *verts[4] = {&v0, &v1, &v2, &v3};
  GMANFace straddling(verts, nullptr);

  GMANPolygonClipper clipper;
  GMANOutputPolygon out;
  int n = clipper.clip(&straddling, out, &vs);
  check(n == 4,
        "clip: a quad straddling the near plane keeps 4 vertices "
        "(2 original + 2 new intersection points)");

  int onNearPlane = 0;
  for (int i = 0; i < n; ++i) {
    if (near(out.getVertexPosn(i).getZ(), -1.0, 1e-3)) {
      ++onNearPlane;
    }
  }
  check(onNearPlane == 2,
        "clip: exactly the two intersection vertices land on the near "
        "plane (NDC z = -1) -- catches both an unused interpolation "
        "parameter and a current/isect output swap");
}

// ---- proof item 5: backface culling gated on RiSides/RiOrientation ----
void testBackfaceCulling() {
  GMANOptions::ScreenWindowStruct sw = squareWindow();
  GMANMatrix4 identity;
  GMANVSPerspective vs(100, 100, sw, identity, 90.0, 1.0, 100.0);

  GMANVertex p0, p1, p2, p3;
  p0.setLocation(GMANPoint(-1.0, -1.0, 5.0));
  p1.setLocation(GMANPoint( 1.0, -1.0, 5.0));
  p2.setLocation(GMANPoint( 1.0,  1.0, 5.0));
  p3.setLocation(GMANPoint(-1.0,  1.0, 5.0));

  // Wound so cross(e1,e2) points toward +z -- facing the camera.
  GMANVertex *windingFacing[4] = {&p0, &p1, &p2, &p3};
  GMANFace facing(windingFacing, nullptr);
  facing.calcNormal();
  facing.setSides(1);
  facing.setOrientation(RI_OUTSIDE);
  check(near(facing.getNormal().getZ(), 1.0, 1e-4),
        "backface: sanity check, this winding's normal is +z");
  check(vs.visible(&facing),
        "backface: a face whose normal faces the camera is visible");

  // Reverse winding -- normal flips to -z, facing away.
  GMANVertex *windingAway[4] = {&p0, &p3, &p2, &p1};
  GMANFace away(windingAway, nullptr);
  away.calcNormal();
  away.setSides(1);
  away.setOrientation(RI_OUTSIDE);
  check(near(away.getNormal().getZ(), -1.0, 1e-4),
        "backface: sanity check, the reversed winding's normal is -z");
  check(!vs.visible(&away),
        "backface: a face whose normal faces away from the camera is culled");

  // RiSides 2: both windings stay visible -- the case that catches an
  // unconditional single-sided cull.
  away.setSides(2);
  check(vs.visible(&away),
        "backface: RiSides 2 keeps a face facing away from the camera");
  facing.setSides(2);
  check(vs.visible(&facing),
        "backface: RiSides 2 keeps a face facing the camera too");

  // RiOrientation "inside" inverts which winding counts as facing out.
  away.setSides(1);
  away.setOrientation(RI_INSIDE);
  check(vs.visible(&away),
        "backface: RiOrientation inside flips a culled face to visible");
}

} // namespace

int main() {
  testFullChain();
  testBehindCameraClips();
  testClipperNearPlane();
  testBackfaceCulling();

  if (failures != 0) {
    std::printf("%d assertion(s) failed\n", failures);
    return 1;
  }
  std::printf("space chain holds\n");
  return 0;
}
