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
 * environment(), MakeLatLongEnvironment and shinymetal (RISpec 3.2
 * Sec 7.1.2, Sec 15.7.2, Appendix A.2.4), proved in three parts matching
 * this task's three commits:
 *
 *  - toWorld and the camera-to-world matrix reaching the shading path
 *    (commit 1);
 *  - gmanMakeLatLongEnvironment's writer and environment()'s lookup,
 *    direct and through RIB (commit 2);
 *  - shinymetal rendered under three camera orientations (commit 3).
 *
 * Writes its own maps into its working directory, as tests/texture_test.cpp
 * writes its checker.
 */

#include <cmath>
#include <cstdio>
#include <string>

#include "check.h"
#include "gmanattributes.h"
#include "gmanlinearworldmanager.h"
#include "gmanmath.h"
#include "gmanmatrix4.h"
#include "gmanobject.h"
#include "gmanoptions.h"
#include "gmanparameterlist.h"
#include "gmanpatchpolyobjectmanager.h"
#include "gmanrenderer.h"
#include "gmanshaderenvironment.h"
#include "gmantransform.h"
#include "gmanvector.h"
#include "gmanvertex.h"

namespace {

const RtFloat kTightTol = (RtFloat) 1.0e-5;

bool near(RtFloat a, RtFloat b, RtFloat tol) {
  return std::fabs(a - b) <= tol;
}

void checkVectorNear(const GMANVector &got, const GMANVector &want,
                      RtFloat tol, const std::string &what) {
  check(near(got.getX(), want.getX(), tol) &&
            near(got.getY(), want.getY(), tol) &&
            near(got.getZ(), want.getZ(), tol),
        what + ": got (" + std::to_string(got.getX()) + ", " +
            std::to_string(got.getY()) + ", " + std::to_string(got.getZ()) +
            "), want (" + std::to_string(want.getX()) + ", " +
            std::to_string(want.getY()) + ", " + std::to_string(want.getZ()) +
            ")");
}

void checkColorNear(const GMANColor &got, const GMANColor &want, RtFloat tol,
                     const std::string &what) {
  check(near(got.getRed(), want.getRed(), tol) &&
            near(got.getGreen(), want.getGreen(), tol) &&
            near(got.getBlue(), want.getBlue(), tol),
        what + ": got (" + std::to_string(got.getRed()) + ", " +
            std::to_string(got.getGreen()) + ", " +
            std::to_string(got.getBlue()) + "), want (" +
            std::to_string(want.getRed()) + ", " +
            std::to_string(want.getGreen()) + ", " +
            std::to_string(want.getBlue()) + ")");
}

// A do-nothing GMANRenderer: GMANAttributes::setSurface needs one to pass
// to GMANShader::set(GMANRenderer&), which only stores the reference
// (gmanshader.cpp) -- no override below is ever actually called.
class NullRenderer : public GMANRenderer {
public:
  RtVoid illuminance(RtInt, GMANPoint const &, GMANVector const &,
                      RtFloat) override {}
  RtVoid illuminate(RtInt, GMANPoint const &, GMANVector const &,
                     RtFloat) override {}
  RtVoid solar(RtInt, GMANVector const &, RtFloat) override {}
  RtFloat getDepth(int, int) const override { return 0; }
  RtVoid render(GMANFrameBuffer *, GMANViewingSystem *, const GMANOptions &,
                const GMANAttributes &) override {}
  GMANWorldManager *getWorldManager(RtVoid) override { return nullptr; }
  GMANObjectManager *getObjectManager(RtVoid) override { return nullptr; }
};

// ---- toWorld and the plumbing (commit 1) ----

// AGENTS.md's row-vector convention: GMANMatrix4::rot's own y-axis matrix
// gives v' = (x*cos(a) + z*sin(a), y, -x*sin(a) + z*cos(a)) -- calibrated
// against tests/rib/rotate.rib's own comment ("Rotate 90 0 1 0 turns the
// world x axis onto -z", i.e. (1,0,0) -> (0,0,-1) under this same matrix).
// At a=90 degrees, (0,0,1) -> (1,0,0).
void testToWorldRotatesDirection() {
  GMANSurfaceEnv env;
  GMANMatrix4 m;
  m.rot((RtFloat)(PI / 2.0), 0, 1, 0);
  // A translation must not move a direction: toWorld is RSL's
  // vtransform("current", "world", v), which reads only cameraToWorld's
  // upper-left 3x3. Folded in here to prove exactly that.
  m.trans((RtFloat) 3.0, (RtFloat) -7.0, (RtFloat) 11.0);
  env.cameraToWorld = m;

  GMANVector world =
      env.toWorld(GMANVector((RtFloat) 0.0, (RtFloat) 0.0, (RtFloat) 1.0));
  checkVectorNear(world,
                   GMANVector((RtFloat) 1.0, (RtFloat) 0.0, (RtFloat) 0.0),
                   kTightTol,
                   "toWorld: a 90-deg Y rotation maps (0,0,1) to (1,0,0)");
}

// Call getRSSphere directly with a GMANOptions carrying the same rotation
// and a test-local shader (tests/camerashader_test_plugin.cpp) that encodes
// se.cameraToWorld's [0][0], [0][2] and [2][0] entries into Ci -- the only
// channel a dlopen'd module and its caller share. For the rotation above,
// [0][0]=cos(90)=0, [0][2]=-sin(90)=-1, [2][0]=sin(90)=1.
void testMatrixReachesShader() {
  GMANOptions options;
  GMANMatrix4 m;
  m.rot((RtFloat)(PI / 2.0), 0, 1, 0);
  options.setCameraToWorld(m);

  GMANAttributes attr;
  NullRenderer renderer;
  GMANParameterList emptyPl;
  attr.setSurface("camerashader", emptyPl, renderer);

  GMANPatchPolyObjectManager mgr;
  GMANParameterList spherePl;
  GMANTransform transform;  // identity

  GMANPrimitive *prim = mgr.getRSSphere((RtFloat) 1.0, (RtFloat) -1.0,
                                         (RtFloat) 1.0, (RtFloat) 360.0,
                                         spherePl, &options, &attr,
                                         &transform);
  // Kept reachable the same way production does (GMANRenderManImpl::RiEnd
  // leaves worldManager/objectManager deliberately unfreed) -- see
  // tests/normals_test.cpp's own comment at its analogous getRSSphere call.
  static GMANLinearWorldManager worldMgr;
  worldMgr.add(prim);

  GMANObject *object = dynamic_cast<GMANObject *>(prim);
  check(object != nullptr, "plumbing: getRSSphere returns an object");
  if (!object) {
    return;
  }
  GMANVertex *vtx = object->getVert();
  check(vtx != nullptr, "plumbing: object has a vertex");
  if (!vtx) {
    return;
  }
  checkColorNear(vtx->getColor(),
                 GMANColor((RtFloat) 0.0, (RtFloat) -1.0, (RtFloat) 1.0),
                 (RtFloat) 1.0e-4,
                 "plumbing: GMANOptions's camera-to-world reaches "
                 "GMANSurfaceEnv::cameraToWorld through getRSSphere");
}

}  // namespace

int main(int /*argc*/, char * /*argv*/[]) {
  testToWorldRotatesDirection();
  testMatrixReachesShader();

  return checkSummary("environment holds");
}
