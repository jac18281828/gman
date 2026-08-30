/* SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Copyright (c) 2026 John Cairns <john@2ad.com>
 */

/*
 * Phase 3, proof items 1-3: every primitive's analytic getNormal(u,v)
 * agrees with a central finite difference of its own getLocation, all
 * seven agree on which way is outward, and a non-uniformly scaled
 * primitive's shading normal stays perpendicular to its (correspondingly
 * scaled) tangent plane -- the test a plain forward-transform of the
 * normal passes only for a uniform scale.
 *
 * Revert check (verified by actually reverting, not asserted): restoring
 * any of the seven getNormal bodies to `throw GMANError(...,"Not
 * implemented")` throws instead of returning, and this file's finite
 * -difference assertions never run to a pass/fail -- the process aborts.
 */

#include <cmath>
#include <cstdio>
#include <string>

#include "check.h"
#include "gmanattributes.h"
#include "gmanlinearworldmanager.h"
#include "gmanmatrix4.h"
#include "gmanobject.h"
#include "gmanoptions.h"
#include "gmanparameterlist.h"
#include "gmanpatchpolyobjectmanager.h"
#include "gmanprimitives.h"
#include "gmantransform.h"
#include "gmanvector.h"
#include "gmanvertex.h"

namespace {

bool near(RtFloat a, RtFloat b, RtFloat tol) {
  return std::fabs(a - b) <= tol;
}

// central difference of getLocation, cross(dP/du, dP/dv), normalized --
// the same convention every getNormal in gmanprimitives.cpp is written
// against (see phase-3-REPORT.md for the derivation).
GMANVector finiteDifferenceNormal(GMANParametric &p, double u, double v,
                                   double h) {
  GMANPoint pu0 = p.getLocation(u - h, v);
  GMANPoint pu1 = p.getLocation(u + h, v);
  GMANPoint pv0 = p.getLocation(u, v - h);
  GMANPoint pv1 = p.getLocation(u, v + h);

  GMANVector dPdu(pu0, pu1);
  GMANVector dPdv(pv0, pv1);
  GMANVector n = dPdu.cross(dPdv);
  n.normalize();
  return n;
}

void checkAnalyticMatchesFiniteDifference(GMANParametric &p, double u,
                                           double v, const std::string &name) {
  GMANVector analytic = p.getNormal(u, v);
  analytic.normalize();
  GMANVector numeric = finiteDifferenceNormal(p, u, v, 1e-4);

  RtFloat agreement = analytic.dot(numeric);
  check(agreement > 0.995,
        name + ": analytic normal agrees with a finite difference of "
        "getLocation at u=" + std::to_string(u) + " v=" + std::to_string(v) +
        " (dot=" + std::to_string(agreement) + ")");
}

// ---- proof item 1: analytic normals against finite differences ----
void testFiniteDifferences() {
  GMANParameterList pl;

  GMANCone cone(2.0, 1.0, 360.0, pl);
  GMANCylinder cylinder(1.0, -1.0, 1.0, 360.0, pl);
  GMANDisk disk(0.5, 1.0, 360.0, pl);
  RtPoint hp1 = {1.0, 0.0, 0.0};
  RtPoint hp2 = {0.5, 0.0, 2.0};
  GMANHyperboloid hyperboloid(hp1, hp2, 360.0, pl);
  GMANParaboloid paraboloid(1.0, 1.0, 2.0, 360.0, pl);
  GMANSphere sphere(1.0, -1.0, 1.0, 360.0, pl);
  GMANTorus torus(2.0, 0.5, -180.0, 180.0, 360.0, pl);

  // Interior (u,v) samples, plus samples close to each primitive's pole
  // (v near 0 or 1) where a coordinate-singular parameterization is most
  // likely to hide a sign or scale error.
  double us[] = {0.1, 0.35, 0.5, 0.75, 0.9};
  double vs[] = {0.05, 0.3, 0.5, 0.7, 0.95};

  for (double u : us) {
    for (double v : vs) {
      checkAnalyticMatchesFiniteDifference(cone, u, v, "cone");
      checkAnalyticMatchesFiniteDifference(cylinder, u, v, "cylinder");
      checkAnalyticMatchesFiniteDifference(disk, u, v, "disk");
      checkAnalyticMatchesFiniteDifference(hyperboloid, u, v, "hyperboloid");
      checkAnalyticMatchesFiniteDifference(paraboloid, u, v, "paraboloid");
      checkAnalyticMatchesFiniteDifference(sphere, u, v, "sphere");
      checkAnalyticMatchesFiniteDifference(torus, u, v, "torus");
    }
  }
}

// ---- proof item 2: every primitive agrees on which way is outward ----
void testOrientationConsistency() {
  GMANParameterList pl;

  // Sphere, cylinder, cone and torus all have an unambiguous physical
  // "outward": away from the center (sphere), away from the axis
  // (cylinder, cone) or away from the tube's center circle (torus). A
  // sphere and a cylinder disagreeing on that sign is exactly the bug
  // this test exists to catch.
  GMANSphere sphere(1.0, -1.0, 1.0, 360.0, pl);
  GMANCylinder cylinder(1.0, -1.0, 1.0, 360.0, pl);
  GMANCone cone(2.0, 1.0, 360.0, pl);
  GMANTorus torus(2.0, 0.5, -180.0, 180.0, 360.0, pl);

  // Hyperboloid and paraboloid are both surfaces of revolution around the
  // z axis, same as cylinder and cone, so the same away-from-axis test
  // applies. Disk is flat: it has no axis and no interior to be "away
  // from," so it gets a different check below.
  RtPoint hp1 = {1.0, 0.0, 0.0};
  RtPoint hp2 = {0.5, 0.0, 2.0};
  GMANHyperboloid hyperboloid(hp1, hp2, 360.0, pl);
  GMANParaboloid paraboloid(1.0, 1.0, 2.0, 360.0, pl);
  GMANDisk disk(0.5, 1.0, 360.0, pl);

  double us[] = {0.1, 0.4, 0.6, 0.9};
  double vs[] = {0.2, 0.4, 0.6, 0.8};

  GMANVector diskNormal0;
  bool haveDiskNormal0 = false;

  for (double u : us) {
    for (double v : vs) {
      GMANPoint p = sphere.getLocation(u, v);
      GMANVector n = sphere.getNormal(u, v);
      GMANVector radial(p.getX(), p.getY(), p.getZ());
      check(n.dot(radial) > 0.0,
            "sphere: normal points away from the center, not into it");

      p = cylinder.getLocation(u, v);
      n = cylinder.getNormal(u, v);
      GMANVector axisRadial(p.getX(), p.getY(), 0.0);
      check(n.dot(axisRadial) > 0.0,
            "cylinder: normal points away from the axis, not into it");

      p = cone.getLocation(u, v);
      n = cone.getNormal(u, v);
      GMANVector coneRadial(p.getX(), p.getY(), 0.0);
      check(n.dot(coneRadial) > 0.0,
            "cone: normal's radial component points away from the axis");

      p = torus.getLocation(u, v);
      n = torus.getNormal(u, v);
      // Outward from the tube's own center circle (major radius 2, in the
      // xy plane): tubeCenter is p projected onto that circle.
      RtFloat r = std::sqrt(p.getX() * p.getX() + p.getY() * p.getY());
      GMANVector tubeCenter(p.getX() * 2.0 / r, p.getY() * 2.0 / r, 0.0);
      GMANVector fromTube(p.getX() - tubeCenter.getX(),
                           p.getY() - tubeCenter.getY(), p.getZ());
      check(n.dot(fromTube) > 0.0,
            "torus: normal points away from the tube's own center circle");

      p = hyperboloid.getLocation(u, v);
      n = hyperboloid.getNormal(u, v);
      GMANVector hyperboloidRadial(p.getX(), p.getY(), 0.0);
      check(n.dot(hyperboloidRadial) > 0.0,
            "hyperboloid: normal points away from the axis, not into it");

      p = paraboloid.getLocation(u, v);
      n = paraboloid.getNormal(u, v);
      GMANVector paraboloidRadial(p.getX(), p.getY(), 0.0);
      check(n.dot(paraboloidRadial) > 0.0,
            "paraboloid: normal points away from the axis, not into it");

      // Disk: a flat plate has exactly one outward direction, the same at
      // every (u,v) -- there is no center or axis to point "away from,"
      // so the physical check here is that every sampled normal is the
      // same direction, not that it points away from something.
      n = disk.getNormal(u, v);
      if (!haveDiskNormal0) {
        diskNormal0 = n;
        haveDiskNormal0 = true;
      }
      check(n.dot(diskNormal0) > 0.0,
            "disk: normal is the same outward direction at every (u,v)");
    }
  }
}

// ---- proof item 3: inverse-transpose under a non-uniform scale ----
void testInverseTransposeUnderNonUniformScale() {
  GMANOptions options;
  GMANAttributes attr;
  GMANPatchPolyObjectManager mgr;
  GMANParameterList pl;

  // A deliberately non-uniform scale: a plain forward transform of the
  // normal (instead of the inverse transpose) stays perpendicular under a
  // uniform scale by coincidence, which is why this has to be non-uniform
  // to catch the bug.
  GMANMatrix4 scale;
  scale.scale(3.0, 1.0, 0.5);
  GMANOneMatrix storage(scale);
  GMANTransform transform(storage);

  GMANPrimitive *prim = mgr.getRSSphere(1.0, -1.0, 1.0, 360.0, pl, &options,
                                         &attr, &transform);
  // Nothing in this tree ever frees a tessellated primitive's face/vertex
  // graph -- production code relies on process exit to reclaim it
  // (GMANRenderManImpl::RiEnd leaves worldManager/objectManager
  // deliberately unfreed) and LeakSanitizer does not flag that, because
  // GMANWorldManager keeps every primitive it holds reachable up to exit.
  // A local variable discarded at the end of this function would make the
  // same allocation genuinely unreachable instead -- a real leak, not a
  // benign one -- so this keeps prim reachable the same way production
  // does, via a world manager, rather than reproducing this tree's
  // "leak is fine, we're about to exit" assumption in a way that
  // contradicts it.
  static GMANLinearWorldManager worldMgr;
  worldMgr.add(prim);

  GMANObject *object = dynamic_cast<GMANObject *>(prim);
  check(object != nullptr, "inverse-transpose: getRSSphere returns an object");

  // Walk the tessellated grid (URES=VRES=16, gmanpatchpolyobjectmanager.cpp)
  // by construction order: vertex k is (i=k/(VRES+1), j=k%(VRES+1)),
  // u=i/16, v=j/16. Skip the pole rows (j=0, j=16): the sphere's own (u,v)
  // parameterization is coordinate-singular there (dP/du -> 0), which is a
  // property of the parameterization, not of this transform.
  const int URES = 16, VRES = 16;
  int k = 0;
  int checkedSome = false;
  for (GMANVertex *vtx = object->getVert(); vtx != nullptr;
       vtx = vtx->getNext(), ++k) {
    int i = k / (VRES + 1);
    int j = k % (VRES + 1);
    if (j == 0 || j == VRES) {
      continue;
    }
    double u = i / (double) URES;
    double v = j / (double) VRES;

    // Independent camera-space tangent vectors: finite-differencing the
    // *transformed* point folds in the forward transform automatically
    // (the translation -- none here -- cancels in the subtraction), so
    // this does not reuse any part of createParametric's own normal
    // computation.
    double h = 1e-4;
    GMANSphere sphere(1.0, -1.0, 1.0, 360.0, pl);
    GMANPoint pu0 = transform.apply(sphere.getLocation(u - h, v));
    GMANPoint pu1 = transform.apply(sphere.getLocation(u + h, v));
    GMANPoint pv0 = transform.apply(sphere.getLocation(u, v - h));
    GMANPoint pv1 = transform.apply(sphere.getLocation(u, v + h));
    GMANVector dPdu(pu0, pu1);
    GMANVector dPdv(pv0, pv1);

    GMANVector n = vtx->getNormal();
    RtFloat magnitude = n.magnitude();
    if (magnitude < 1e-6) {
      continue;
    }

    RtFloat duDot = n.dot(dPdu) / (magnitude * dPdu.magnitude());
    RtFloat dvDot = n.dot(dPdv) / (magnitude * dPdv.magnitude());
    check(near(duDot, 0.0, 0.01),
          "inverse-transpose: shading normal stays perpendicular to "
          "dP/du under a non-uniform scale (u=" + std::to_string(u) +
          " v=" + std::to_string(v) + ")");
    check(near(dvDot, 0.0, 0.01),
          "inverse-transpose: shading normal stays perpendicular to "
          "dP/dv under a non-uniform scale (u=" + std::to_string(u) +
          " v=" + std::to_string(v) + ")");
    checkedSome = true;
  }
  check(checkedSome, "inverse-transpose: at least one interior vertex was "
                      "actually checked");
}

}  // namespace

int main() {
  testFiniteDifferences();
  testOrientationConsistency();
  testInverseTransposeUnderNonUniformScale();

  return checkSummary("normals hold");
}
