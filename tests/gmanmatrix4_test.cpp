/* SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Copyright (c) 2026 John Cairns <john@2ad.com>
 */

/*
 * Phase 1, proof item 3: prjPersp and prjOrtho against hand-computed
 * matrices; invert against concat producing identity; determinant on known
 * inputs. Also covers p3m/p4m, which prior to this phase were dead code
 * inside a `#if 0` that didn't even compile against GMANMatrix4's actual
 * (2D-array) storage.
 *
 * Revert check: reverting GMANMatrix4::prjPersp to its pre-phase-1 form
 * (mtrx[3][3]=0 and nothing else in row 3) makes "w carries z" and the
 * near/far boundary checks below go red, since w is identically zero.
 */

#include <cmath>
#include <cstdio>
#include <string>

#include "check.h"
#include "gmanmatrix4.h"
#include "gmanvector4.h"
#include "gmanpoint.h"

namespace {

bool near(RtFloat a, RtFloat b, RtFloat tol = 1e-4) {
  return std::fabs(a - b) <= tol;
}

void testPrjPersp() {
  GMANMatrix4 m;
  m.prjPersp(90.0, 1.0, 11.0);

  // fov=90 -> tan(45deg)=1, so x and y pass through unscaled by the
  // projection matrix itself (aspect and screen-window scaling happen
  // later, in GMANViewingSystem::screenToRaster).
  GMANVector4 clip;
  clip.projTransform(GMANPoint(2.0, 3.0, 5.0), m.get());
  check(near(clip.getX(), 2.0), "prjPersp: x passes through at fov 90");
  check(near(clip.getY(), 3.0), "prjPersp: y passes through at fov 90");
  check(near(clip.getW(), 5.0), "prjPersp: w carries camera-space z");

  GMANPoint ndc;
  clip.perspective(ndc);
  check(near(ndc.getX(), 0.4), "prjPersp: ndc.x = x/z");
  check(near(ndc.getY(), 0.6), "prjPersp: ndc.y = y/z");
  check(near(ndc.getZ(), 0.76), "prjPersp: ndc.z hand-computed at z=5");

  // Near and far planes land exactly on the -1/+1 NDC z boundary.
  GMANVector4 atNear, atFar;
  atNear.projTransform(GMANPoint(0.0, 0.0, 1.0), m.get());
  atFar.projTransform(GMANPoint(0.0, 0.0, 11.0), m.get());
  GMANPoint ndcNear, ndcFar;
  atNear.perspective(ndcNear);
  atFar.perspective(ndcFar);
  check(near(ndcNear.getZ(), -1.0), "prjPersp: near plane maps to ndc.z=-1");
  check(near(ndcFar.getZ(), 1.0), "prjPersp: far plane maps to ndc.z=+1");

  // A point behind the camera has negative w -- the clip test (dot with a
  // w=1 plane normal) is what actually rejects it; here we just confirm w
  // carries the sign, which the pre-phase-1 all-zero row 3 could not.
  GMANVector4 behind;
  behind.projTransform(GMANPoint(0.0, 0.0, -3.0), m.get());
  check(behind.getW() < 0.0, "prjPersp: a point behind the camera has w<0");
}

void testPrjOrtho() {
  GMANMatrix4 m;
  m.prjOrtho(2.0, 10.0);

  GMANVector4 clip;
  clip.projTransform(GMANPoint(3.0, 4.0, 6.0), m.get());
  check(near(clip.getX(), 3.0), "prjOrtho: x passes through unscaled");
  check(near(clip.getY(), 4.0), "prjOrtho: y passes through unscaled");
  check(near(clip.getW(), 1.0), "prjOrtho: w is always 1 (no perspective)");

  GMANPoint ndc;
  clip.perspective(ndc);
  check(near(ndc.getZ(), 0.0), "prjOrtho: midpoint of [near,far] maps to 0");

  GMANVector4 atNear, atFar;
  atNear.projTransform(GMANPoint(0.0, 0.0, 2.0), m.get());
  atFar.projTransform(GMANPoint(0.0, 0.0, 10.0), m.get());
  check(near(atNear.getZ(), -1.0), "prjOrtho: near plane maps to ndc.z=-1");
  check(near(atFar.getZ(), 1.0), "prjOrtho: far plane maps to ndc.z=+1");
}

void testDeterminant() {
  GMANMatrix4 t;
  t.trans(5.0, -2.0, 3.0);
  check(near(t.determinant(), 1.0), "determinant: a pure translation is 1");

  GMANMatrix4 s;
  s.scale(2.0, 3.0, 4.0);
  check(near(s.determinant(), 24.0), "determinant: scale(2,3,4) is 24");

  GMANMatrix4 id;
  check(near(id.determinant(), 1.0), "determinant: identity is 1");
}

void testInvertConcat() {
  // A representative CTM: translate, then rotate, then a non-uniform scale.
  GMANMatrix4 m;
  m.trans(1.0, 2.0, 3.0);
  GMANMatrix4 r;
  r.rot(0.7, 0.0, 0.0, 1.0);
  m.concat(r);
  GMANMatrix4 s;
  s.scale(2.0, 0.5, 3.0);
  m.concat(s);

  GMANMatrix4 inv(m);
  inv.invert();

  GMANMatrix4 product(m);
  product.concat(inv);

  bool isIdentity = true;
  for (int i = 0; i < 4; ++i) {
    for (int j = 0; j < 4; ++j) {
      RtFloat expect = (i == j) ? 1.0 : 0.0;
      if (!near(product[i][j], expect, 1e-3)) {
        isIdentity = false;
      }
    }
  }
  check(isIdentity, "invert: m * invert(m) is the identity");

  // A singular matrix (zero scale collapses a whole axis) throws rather
  // than silently returning garbage.
  GMANMatrix4 singular;
  singular.scale(1.0, 0.0, 1.0);
  bool threw = false;
  try {
    singular.invert();
  } catch (...) {
    threw = true;
  }
  check(threw, "invert: a singular matrix throws instead of dividing by zero");
}

void testP3mRowVectorConvention() {
  // p3m must consume the same row-vector, translation-in-row-3 layout
  // trans()/rot()/scale()/concat() already build (GMANTransform::apply's
  // whole job), not GMANPoint::operator*='s translation-in-column-3 shape.
  GMANMatrix4 t;
  t.trans(5.0, 0.0, 0.0);
  RtFloat src[] = {1.0, 2.0, 3.0};
  RtFloat dst[3];
  t.p3m(1, src, dst);
  check(near(dst[0], 6.0) && near(dst[1], 2.0) && near(dst[2], 3.0),
        "p3m: a translation matrix translates the point");

  // Rotate (1,0,0) by +90deg about Z: row-vector p*R sends it to (0,1,0)
  // for GMANMatrix4::rot()'s sign convention (hand-derived from rot()'s
  // own [row][col] layout, independent of p3m).
  GMANMatrix4 r;
  r.rot(M_PI / 2.0, 0.0, 0.0, 1.0);
  RtFloat srcR[] = {1.0, 0.0, 0.0};
  RtFloat dstR[3];
  r.p3m(1, srcR, dstR);
  check(near(dstR[0], 0.0) && near(dstR[1], 1.0) && near(dstR[2], 0.0),
        "p3m: a +90deg Z rotation sends (1,0,0) to (0,1,0)");
}

void testP4mCarriesW() {
  // p4m is p3m's homogeneous pair for GMANTransform::apply -- the
  // object/world/camera CTM chain (row-vector, translation in row 3), not
  // the projection matrix (consumed via GMANVector4::projTransform
  // instead, which uses the other layout; see testPrjPersp).
  GMANMatrix4 t;
  t.trans(5.0, 0.0, 0.0);

  // A point (w=1): p3m and p4m must agree on x,y,z, and p4m leaves w=1
  // rather than normalizing it away.
  RtFloat srcPoint[] = {1.0, 2.0, 3.0, 1.0};
  RtFloat dstPoint[4];
  t.p4m(1, srcPoint, dstPoint);
  check(near(dstPoint[0], 6.0) && near(dstPoint[1], 2.0) &&
        near(dstPoint[2], 3.0) && near(dstPoint[3], 1.0),
        "p4m: a translated point matches p3m's result, w left at 1");

  // A direction (w=0) is translation-invariant -- the whole reason
  // homogeneous coordinates distinguish points from vectors.
  RtFloat srcDir[] = {1.0, 0.0, 0.0, 0.0};
  RtFloat dstDir[4];
  t.p4m(1, srcDir, dstDir);
  check(near(dstDir[0], 1.0) && near(dstDir[1], 0.0) &&
        near(dstDir[2], 0.0) && near(dstDir[3], 0.0),
        "p4m: a direction (w=0) is unaffected by translation");
}

} // namespace

int main() {
  testPrjPersp();
  testPrjOrtho();
  testDeterminant();
  testInvertConcat();
  testP3mRowVectorConvention();
  testP4mCarriesW();

  return checkSummary("gmanmatrix4 holds");
}
