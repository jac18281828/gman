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
 * GMANRayObjectManager::getRSSphere builds a GMANRaySphere that keeps
 * its parameters and a copied transform, rather than a bare
 * GMANRayInterface. Each check builds a sphere through the factory,
 * exactly as RiSphereV does, and intersects it with a camera-space ray.
 */

#include <cmath>

#include "check.h"
#include "gmanattributes.h"
#include "gmanray.h"
#include "gmanrayobjectmanager.h"
#include "gmanraysphere.h"

namespace {

constexpr RtFloat kTolerance = 1e-4f;

bool near(RtFloat a, RtFloat b, RtFloat tol = kTolerance) { return std::fabs(a - b) <= tol; }

GMANTransform makeTransform(GMANMatrix4 matrix) {
  GMANOneMatrix storage(matrix);
  return GMANTransform(storage);
}

// getRSSphere ignores opt, so it is null here; attr is a default-constructed
// GMANAttributes, as polygon_test.cpp passes, since getRSSphere resolves
// it into the sphere's appearance. Casts to GMANRaySphere rather than
// GMANRayInterface: the base's own bare create() also satisfies a
// GMANRayInterface cast, so that cast alone cannot distinguish a real
// GMANRaySphere from one.
GMANRaySphere* buildSphere(GMANRayObjectManager& manager, RtFloat radius, RtFloat zmin, RtFloat zmax, RtFloat tmax,
                           GMANTransform& transform) {
  GMANAttributes attr;
  GMANPrimitive* primitive =
      manager.getRSSphere(radius, zmin, zmax, tmax, GMANParameterList(), nullptr, &attr, &transform);
  return dynamic_cast<GMANRaySphere*>(primitive);
}

// ---- check 1: a translated sphere hits where the transform puts it ----
void testTranslate() {
  GMANRayObjectManager manager;
  GMANMatrix4 matrix;
  matrix.trans(3.0, 0.0, 0.0);
  GMANTransform transform = makeTransform(matrix);

  GMANRaySphere* sphere = buildSphere(manager, 1.0, -1.0, 1.0, 360.0, transform);
  check(sphere != nullptr, "translate: getRSSphere returns a GMANRaySphere");
  if (sphere == nullptr)
    return;

  GMANRay hitRay(GMANPoint(3.0, 0.0, -5.0), GMANVector(0.0, 0.0, 1.0));
  GMANHit hit;
  bool const hitFound = sphere->intersect(hitRay, hit);
  check(hitFound && near(hit.t, 4.0), "translate: a ray aimed at the translated sphere hits at t == 4");
  check(near(hit.point.getX(), 3.0) && near(hit.point.getY(), 0.0) && near(hit.point.getZ(), -1.0),
        "translate: point == (3, 0, -1)");
  check(near(hit.normal.getX(), 0.0) && near(hit.normal.getY(), 0.0) && near(hit.normal.getZ(), -1.0),
        "translate: normal == (0, 0, -1)");

  GMANRay missRay(GMANPoint(0.0, 0.0, -5.0), GMANVector(0.0, 0.0, 1.0));
  GMANHit missHit;
  check(!sphere->intersect(missRay, missHit), "translate: a ray aimed at the origin misses the translated sphere");

  delete sphere;
}

// ---- check 2: non-uniform scale, the inverse-transpose normal ----
void testNonUniformScaleNormal() {
  GMANRayObjectManager manager;
  GMANMatrix4 matrix;
  matrix.scale(2.0, 1.0, 1.0);
  GMANTransform transform = makeTransform(matrix);

  GMANRaySphere* sphere = buildSphere(manager, 1.0, -1.0, 1.0, 360.0, transform);
  check(sphere != nullptr, "non-uniform scale: getRSSphere returns a GMANRaySphere");
  if (sphere == nullptr)
    return;

  RtFloat const sqrt2 = (RtFloat)std::sqrt(2.0);
  RtFloat const sqrt5 = (RtFloat)std::sqrt(5.0);
  GMANRay ray(GMANPoint(sqrt2, 0.0, -5.0), GMANVector(0.0, 0.0, 1.0));
  GMANHit hit;
  bool const hitFound = sphere->intersect(ray, hit);
  check(hitFound && near(hit.t, 5.0 - sqrt2 / 2.0), "non-uniform scale: t == 5 - sqrt(2)/2");
  check(near(hit.point.getX(), sqrt2) && near(hit.point.getY(), 0.0) && near(hit.point.getZ(), -sqrt2 / 2.0),
        "non-uniform scale: point == (sqrt(2), 0, -sqrt(2)/2)");
  check(near(hit.normal.getX(), 1.0 / sqrt5) && near(hit.normal.getY(), 0.0) && near(hit.normal.getZ(), -2.0 / sqrt5),
        "non-uniform scale: normal == (1/sqrt(5), 0, -2/sqrt(5)), not the forward-transformed "
        "(2/sqrt(5), 0, -1/sqrt(5))");

  delete sphere;
}

// ---- check 3: non-uniform scale, the rescaled distance and interval ----
void testNonUniformScaleDistance() {
  GMANRayObjectManager manager;
  GMANMatrix4 matrix;
  matrix.scale(2.0, 1.0, 1.0);
  GMANTransform transform = makeTransform(matrix);

  GMANRaySphere* sphere = buildSphere(manager, 1.0, -1.0, 1.0, 360.0, transform);
  check(sphere != nullptr, "non-uniform scale distance: getRSSphere returns a GMANRaySphere");
  if (sphere == nullptr)
    return;

  GMANPoint const origin(-5.0, 0.0, 0.0);
  GMANVector const direction(1.0, 0.0, 0.0);

  GMANRay ray(origin, direction);
  GMANHit hit;
  bool const hitFound = sphere->intersect(ray, hit);
  check(hitFound && near(hit.t, 3.0), "non-uniform scale distance: t == 3 in camera space, not the object-space 1.5");
  check(near(hit.point.getX(), -2.0) && near(hit.point.getY(), 0.0) && near(hit.point.getZ(), 0.0),
        "non-uniform scale distance: point == (-2, 0, 0)");
  check(near(hit.normal.getX(), -1.0) && near(hit.normal.getY(), 0.0) && near(hit.normal.getZ(), 0.0),
        "non-uniform scale distance: normal == (-1, 0, 0)");

  GMANRay shortRay(origin, direction, RI_EPSILON, 2.5);
  GMANHit shortHit;
  check(!sphere->intersect(shortRay, shortHit),
        "non-uniform scale distance: tmax == 2.5 misses; an unscaled interval would let it hit");

  delete sphere;
}

// ---- check 4: the parameters survive under the identity transform ----
void testParametersSurvive() {
  GMANRayObjectManager manager;
  GMANMatrix4 identity;
  GMANTransform transform = makeTransform(identity);

  GMANRaySphere* clippedSphere = buildSphere(manager, 1.0, -1.0, 0.0, 360.0, transform);
  check(clippedSphere != nullptr, "parameters survive: zmax == 0 sphere builds");
  if (clippedSphere != nullptr) {
    GMANRay clipRay(GMANPoint(0.0, 0.0, 5.0), GMANVector(0.0, 0.0, -1.0));
    GMANHit clipHit;
    bool const clipHitFound = clippedSphere->intersect(clipRay, clipHit);
    check(clipHitFound && near(clipHit.t, 6.0),
          "parameters survive: zmax == 0 hides the near cap, hitting the lower half from inside at t == 6");
    delete clippedSphere;
  }

  GMANRaySphere* wedgeSphere = buildSphere(manager, 1.0, -1.0, 1.0, 90.0, transform);
  check(wedgeSphere != nullptr, "parameters survive: thetamax == 90 sphere builds");
  if (wedgeSphere != nullptr) {
    GMANRay wedgeRay(GMANPoint(-5.0, 0.0, 0.0), GMANVector(1.0, 0.0, 0.0));
    GMANHit wedgeHit;
    bool const wedgeHitFound = wedgeSphere->intersect(wedgeRay, wedgeHit);
    check(wedgeHitFound && near(wedgeHit.t, 6.0),
          "parameters survive: thetamax == 90 skips the near root at theta == pi, hitting the far root at t == 6");
    delete wedgeSphere;
  }
}

// ---- check 5: a singular transform never hits, but never throws ----
void testSingularTransform() {
  GMANRayObjectManager manager;
  GMANMatrix4 matrix;
  matrix.scale(1.0, 1.0, 0.0);
  GMANTransform transform = makeTransform(matrix);

  GMANRaySphere* sphere = nullptr;
  bool threw = false;
  try {
    sphere = buildSphere(manager, 1.0, -1.0, 1.0, 360.0, transform);
  } catch (...) {
    threw = true;
  }
  check(!threw, "singular transform: getRSSphere constructs without throwing");
  check(sphere != nullptr, "singular transform: getRSSphere still returns a GMANRaySphere");
  if (sphere == nullptr)
    return;

  GMANRay ray(GMANPoint(0.0, 0.0, -5.0), GMANVector(0.0, 0.0, 1.0));
  GMANHit hit;
  check(!sphere->intersect(ray, hit), "singular transform: a sphere with no invertible object space never hits");

  delete sphere;
}

// ---- check 6: a rotation exercises the normal's transpose and the
// direction transform's own orientation, neither of which a translation
// or a diagonal scale can tell from its opposite (both are symmetric, so
// a transpose bug in either computation would still pass checks 1-5) ----
void testRotation() {
  GMANRayObjectManager manager;
  GMANMatrix4 matrix;
  matrix.rot(GMANRadians(90.0), 0.0, 0.0, 1.0);
  GMANTransform transform = makeTransform(matrix);

  GMANRaySphere* sphere = buildSphere(manager, 1.0, -1.0, 1.0, 90.0, transform);
  check(sphere != nullptr, "rotation: getRSSphere returns a GMANRaySphere");
  if (sphere == nullptr)
    return;

  RtFloat const sqrt075 = (RtFloat)std::sqrt(0.75);
  GMANRay ray(GMANPoint(-5.0, 0.5, 0.0), GMANVector(1.0, 0.0, 0.0));
  GMANHit hit;
  bool const hitFound = sphere->intersect(ray, hit);
  check(hitFound && near(hit.t, 5.0 - sqrt075), "rotation: t == 5 - sqrt(0.75)");
  check(near(hit.normal.getX(), -sqrt075) && near(hit.normal.getY(), 0.5) && near(hit.normal.getZ(), 0.0),
        "rotation: normal == (-sqrt(0.75), 0.5, 0)");

  delete sphere;
}

} // namespace

int main() {
  testTranslate();
  testNonUniformScaleNormal();
  testNonUniformScaleDistance();
  testParametersSurvive();
  testSingularTransform();
  testRotation();

  return checkSummary("GMANRayObjectManager::getRSSphere keeps a sphere's parameters and transform");
}
