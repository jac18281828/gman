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
 * PatchMesh rasterization: GMANPatchMesh now implements GMANParametric,
 * RiPatchMeshV builds the request and GMANPatchPolyObjectManager::
 * getRSPatchMesh dispatches "bilinear" to a hand-written mapping and
 * "bicubic" to GMANBasis::bicubicMesh.
 *
 * Two proof mechanisms, mirroring normals_test.cpp (hand-computed points,
 * direct C++ instantiation) and patchnorender_test.cpp (rendered region
 * content, the getRSPatchMesh revert falsification):
 *
 *  - White-box: GMANPatchMesh instantiated directly against control grids
 *    simple enough to work out on paper, catching a transposed grid or a
 *    wrong sub-patch index that a plausible-looking render would not.
 *  - Render-level: each tests/rib/patchmesh_*.rib fixture rendered and
 *    read back, region content compared against what its own control net
 *    implies. Reverting getRSPatchMesh to `return create();` fails every
 *    "PatchMesh renders" assertion below while each fixture's control
 *    Sphere stays green, isolating the failure to PatchMesh.
 */

#include <tiffio.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <sys/wait.h>

#include "check.h"
#include "gmandictionary.h"
#include "gmanobject.h"
#include "gmanparameterlist.h"
#include "gmanpatchpolyobjectmanager.h"
#include "gmanprimitives.h"
#include "goldenimage.h"

namespace {

bool near3(const GMANPoint &p, RtFloat x, RtFloat y, RtFloat z, RtFloat tol) {
  return std::fabs(p.getX() - x) <= tol && std::fabs(p.getY() - y) <= tol &&
         std::fabs(p.getZ() - z) <= tol;
}

bool pointsNear(const GMANPoint &a, const GMANPoint &b, RtFloat tol) {
  return near3(a, b.getX(), b.getY(), b.getZ(), tol);
}

// ---- white-box: bilinear, hand-computed ----
//
// nu=3 nv=2, nonperiodic both axes: two patches side by side sharing
// their middle column. Grid: point(i,j) = (i,j,0). At (u,v)=(0.75,0.5),
// u picks the second of two u sub-patches (patchUStart=1, newU=0.5) and v
// stays in the only v sub-patch (newV=0.5); bilinear interpolation of
// corners (1,0,0) (2,0,0) (1,1,0) (2,1,0) at newU=newV=0.5 averages the
// four corners: (1.5, 0.5, 0).
void testBilinearHandComputed() {
  RtFloat p[18] = {
      0, 0, 0,  1, 0, 0,  2, 0, 0,
      0, 1, 0,  1, 1, 0,  2, 1, 0};
  GMANParameterList pl;
  GMANPatchMesh mesh((RtToken) "bilinear", p, 3, (RtToken) RI_NONPERIODIC,
                      2, (RtToken) RI_NONPERIODIC, pl);

  GMANPoint loc = mesh.getLocation(0.75, 0.5);
  check(near3(loc, 1.5, 0.5, 0.0, 1e-5),
        "bilinear PatchMesh: getLocation(0.75,0.5) on a 3x2 nonperiodic "
        "grid matches the hand-computed (1.5,0.5,0)");
}

// ---- white-box: bilinear, periodic closure ----
//
// nu=3 "periodic" (a triangle in the xy plane), nv=2 "nonperiodic". A
// periodic axis wraps its last strip back to control point 0, so u=1.0
// must land on exactly the same point as u=0.0 -- not a nearby point on
// a different sub-patch, the same point.
void testBilinearPeriodicClosure() {
  RtFloat p[18];
  for (int j = 0; j < 2; ++j) {
    for (int i = 0; i < 3; ++i) {
      double angle = 2.0 * M_PI * i / 3.0;
      RtFloat *dst = &p[3 * (i + 3 * j)];
      dst[0] = (RtFloat) std::cos(angle);
      dst[1] = (RtFloat) std::sin(angle);
      dst[2] = (RtFloat) j;
    }
  }
  GMANParameterList pl;
  GMANPatchMesh mesh((RtToken) "bilinear", p, 3, (RtToken) RI_PERIODIC,
                      2, (RtToken) RI_NONPERIODIC, pl);

  GMANPoint atOne = mesh.getLocation(1.0, 0.25);
  GMANPoint atZero = mesh.getLocation(0.0, 0.25);
  check(pointsNear(atOne, atZero, 1e-4),
        "bilinear PatchMesh: a periodic u axis closes u=1.0 onto u=0.0");
}

// ---- white-box: bicubic, hand-computed corner ----
//
// nu=nv=4, nonperiodic both axes: bicubic's minimum, exactly one
// sub-patch, under the default Bezier basis (GMANBasis()'s own default).
// Control points are 0,1,2,...,47 in order, so point(3,3) -- the last of
// the 16 -- is (45,46,47). A textbook cubic Bezier interpolates its last
// control point exactly at t=1, so bicubicMesh(1,1,...) must equal
// (45,46,47) exactly: this is the case the original off-by-one computed
// one sub-patch past the last valid one, reading past the 48-float array
// (see gmanbasis.cpp's offset()/bicubicMesh() commentary).
void testBicubicCornerHandComputed() {
  RtFloat p[48];
  for (int i = 0; i < 48; ++i) {
    p[i] = (RtFloat) i;
  }
  GMANParameterList pl;
  GMANBasis basis;
  GMANPatchMesh mesh((RtToken) "bicubic", p, 4, (RtToken) RI_NONPERIODIC,
                      4, (RtToken) RI_NONPERIODIC, basis, pl);

  GMANPoint corner = mesh.getLocation(1.0, 1.0);
  check(near3(corner, 45.0, 46.0, 47.0, 1e-3),
        "bicubic PatchMesh: getLocation(1,1) on a 4x4 nonperiodic grid "
        "matches the last control point (45,46,47)");
}

// ---- white-box: bicubic, multi-sub-patch corner ----
//
// nu=7 (two u sub-patches sharing a column), nv=4 (one v sub-patch).
// Consecutive Bezier sub-patches in a mesh share their boundary control
// point, and a cubic Bezier interpolates both of its endpoints, so the
// same identity chains across sub-patches: the u=1.0 corner is still
// exactly the mesh's own last control point, point(6,3) = (81,82,83) for
// control points numbered 0..83 in order. This is the case a correct
// single-sub-patch fix could still get wrong -- clamping to the wrong
// sub-patch, or offset() wrapping when it should not.
void testBicubicMultiPatchCornerHandComputed() {
  RtFloat p[84];
  for (int i = 0; i < 84; ++i) {
    p[i] = (RtFloat) i;
  }
  GMANParameterList pl;
  GMANBasis basis;
  GMANPatchMesh mesh((RtToken) "bicubic", p, 7, (RtToken) RI_NONPERIODIC,
                      4, (RtToken) RI_NONPERIODIC, basis, pl);

  GMANPoint corner = mesh.getLocation(1.0, 1.0);
  check(near3(corner, 81.0, 82.0, 83.0, 1e-3),
        "bicubic PatchMesh: getLocation(1,1) on a 7x4 nonperiodic grid "
        "(two u sub-patches) matches the last control point (81,82,83)");
}

// ---- white-box: bicubic, periodic closure ----
//
// nu=6 "periodic" (two u sub-patches wrapping the last strip back to
// control point 0), nv=4 "nonperiodic". GMANBasis::offset's wraparound
// guard is exercised here: the last periodic sub-patch's own final
// control point offset lands exactly on index nu, which must wrap to 0,
// not read one past the nu points that exist.
void testBicubicPeriodicClosure() {
  RtFloat p[72];
  for (int i = 0; i < 72; ++i) {
    p[i] = (RtFloat) i;
  }
  GMANParameterList pl;
  GMANBasis basis;
  GMANPatchMesh mesh((RtToken) "bicubic", p, 6, (RtToken) RI_PERIODIC,
                      4, (RtToken) RI_NONPERIODIC, basis, pl);

  GMANPoint atOne = mesh.getLocation(1.0, 0.3);
  GMANPoint atZero = mesh.getLocation(0.0, 0.3);
  check(pointsNear(atOne, atZero, 1e-2),
        "bicubic PatchMesh: a periodic u axis closes u=1.0 onto u=0.0");
}

// ---- white-box: bicubic, periodic closure at a step=1 basis's minimum n ----
//
// Every other bicubic fixture above uses the default Bezier basis
// (step=3), where validBicubicMeshDim's periodic bound (n>=step) is
// already tight enough: n=step=3 is also the true safety floor. A step=1
// basis (b-spline here) is the case that bound got wrong -- it accepted
// n down to 1, but GMANBasis::offset's periodic wraparound (a single
// `i-=nu`, not a true modulo) only stays in range for n>=3 at step=1 (see
// gmanpatchpolyobjectmanager.cpp's validPeriodicBicubicMeshDim). nu=3
// periodic is exactly that floor: three sub-patches, each stepping by 1,
// the last one starting at astart=nu-step=2 and reaching index 2+3=5,
// which offset's single subtraction must land back at 5-3=2 -- in range
// only because nu=3 clears the n>=3 floor. Below that floor this read out
// of bounds (ASan heap-buffer-overflow, confirmed against the unfixed
// validator's n=1/n=2). u=1.0 must still close onto u=0.0 exactly, the
// same periodic identity testBicubicPeriodicClosure checks under Bezier.
void testBicubicBSplinePeriodicClosureAtMinimum() {
  GMANMatrix4 uMat, vMat;
  uMat.setBasis(RiBSplineBasis);
  vMat.setBasis(RiBSplineBasis);
  GMANBasis basis(uMat, 1, vMat, 1);

  // nu=3 periodic (the step=1 floor), nv=4 nonperiodic (one v sub-patch).
  RtFloat p[36];
  for (int i = 0; i < 36; ++i) {
    p[i] = (RtFloat) i;
  }
  GMANParameterList pl;
  GMANPatchMesh mesh((RtToken) "bicubic", p, 3, (RtToken) RI_PERIODIC,
                      4, (RtToken) RI_NONPERIODIC, basis, pl);

  GMANPoint atOne = mesh.getLocation(1.0, 0.4);
  GMANPoint atZero = mesh.getLocation(0.0, 0.4);
  check(pointsNear(atOne, atZero, 1e-2),
        "bicubic PatchMesh, step=1 basis: a periodic u axis at nu=3 (the "
        "step=1 safety floor) closes u=1.0 onto u=0.0");
}

// ---- white-box: mixed wrap, hand-computed corner ----
//
// nu=6 "periodic", nv=4 "nonperiodic" -- one periodic axis, one
// nonperiodic axis on the same mesh. At (u,v)=(1,1): the periodic u axis
// closes onto u=0 (u-index 0), the nonperiodic v axis reaches the
// trailing edge of its own last (only) sub-patch (v-index nv-1=3), so the
// corner is exactly point(0,3) = (54,55,56) for control points numbered
// 0..71 in order.
void testMixedWrapCornerHandComputed() {
  RtFloat p[72];
  for (int i = 0; i < 72; ++i) {
    p[i] = (RtFloat) i;
  }
  GMANParameterList pl;
  GMANBasis basis;
  GMANPatchMesh mesh((RtToken) "bicubic", p, 6, (RtToken) RI_PERIODIC,
                      4, (RtToken) RI_NONPERIODIC, basis, pl);

  GMANPoint corner = mesh.getLocation(1.0, 1.0);
  check(near3(corner, 54.0, 55.0, 56.0, 1e-3),
        "bicubic PatchMesh, mixed wrap: getLocation(1,1) matches "
        "point(0,3) = (54,55,56)");
}

// ---- white-box: nonperiodic bicubic, zero basis step rejected ----
//
// validBicubicMeshDim's nonperiodic branch computes (n-4)%step; a zero
// step divides by zero unless the branch guards it first, exactly as its
// periodic sibling already does. RiPatchMeshV never reaches
// getRSPatchMesh with such a step -- it checks uStep/vStep itself first
// -- so this drives GMANPatchPolyObjectManager directly, the one path
// this input can still reach.
void testNonperiodicBicubicZeroStepRejected() {
  RtFloat p[36];
  for (int i = 0; i < 36; ++i) {
    p[i] = (RtFloat) i;
  }
  GMANDictionary dictionary;
  RtToken tokens[1] = {RI_P};
  RtPointer parms[1] = {p};
  GMANParameterList pl(dictionary, 1, tokens, parms, /*vertex=*/12,
                       /*varying=*/12, /*uniform=*/1);

  GMANOptions options;
  GMANAttributes attr;
  attr.setUVBasis(RiBezierBasis, /*ustep=*/0, RiBezierBasis, /*vstep=*/3);
  GMANTransform transform;
  GMANPatchPolyObjectManager mgr;

  GMANPrimitive *prim =
      mgr.getRSPatchMesh((RtToken) "bicubic", 4, (RtToken) RI_NONPERIODIC, 3,
                         (RtToken) RI_PERIODIC, pl, &options, &attr,
                         &transform);
  GMANObject *object = dynamic_cast<GMANObject *>(prim);
  check(object != nullptr && object->getVert() == nullptr,
        "bicubic PatchMesh, nonperiodic ustep=0: getRSPatchMesh rejects "
        "the mesh instead of dividing by zero");
  delete prim;
}

// ---- render-level: each fixture rasterizes, reverting getRSPatchMesh
// falsifies every one of these ----

int runGman(const std::string &gman, const std::string &rib) {
  const std::string command =
      "\"" + gman + "\" \"" + rib + "\" >/dev/null 2>&1";
  int status = std::system(command.c_str());
  return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

bool regionHasContent(const GmanImage &img, uint32_t x0, uint32_t x1,
                      uint32_t y0, uint32_t y1) {
  const uint32_t bg = img.at(0, 0);
  for (uint32_t y = y0; y < y1; ++y) {
    for (uint32_t x = x0; x < x1; ++x) {
      uint32_t p = img.at(x, y);
      if (std::abs(int(TIFFGetR(p)) - int(TIFFGetR(bg))) > 8 ||
          std::abs(int(TIFFGetG(p)) - int(TIFFGetG(bg))) > 8 ||
          std::abs(int(TIFFGetB(p)) - int(TIFFGetB(bg))) > 8) {
        return true;
      }
    }
  }
  return false;
}

// Every fixture below puts its PatchMesh in the left half of a 200x200
// frame (Translate -1 0 0) and a control Sphere in the right half
// (Translate 1 0 0) -- the Sphere renders in every case, including the
// invalid-dimension fixture, so a left-half failure is PatchMesh's own.
void checkFixtureRenders(const std::string &gman, const std::string &ribDir,
                         const std::string &name) {
  const std::string rib = ribDir + "/" + name + ".rib";
  check(runGman(gman, rib) == 0, name + ".rib renders (exit 0)");

  GmanImage img = readGmanTIFF(name + ".tif");
  check(img.ok, name + ".rib: TIFF read back");
  if (!img.ok) {
    return;
  }
  const uint32_t mid = img.width / 2;
  check(regionHasContent(img, 0, mid, 0, img.height),
        name + ": PatchMesh renders (left half)");
  check(regionHasContent(img, mid, img.width, 0, img.height),
        name + ": control Sphere renders (right half)");
}

void testInvalidDimensionFallback(const std::string &gman,
                                  const std::string &ribDir) {
  const std::string rib = ribDir + "/malformed/patchmesh_baddim.rib";
  check(runGman(gman, rib) == 0,
        "patchmesh_baddim.rib renders without crashing (exit 0)");

  GmanImage img = readGmanTIFF("patchmesh_baddim.tif");
  check(img.ok, "patchmesh_baddim.rib: TIFF read back");
  if (!img.ok) {
    return;
  }
  const uint32_t mid = img.width / 2;
  check(! regionHasContent(img, 0, mid, 0, img.height),
        "patchmesh_baddim: nu=5 fails the Bezier step's alignment, so "
        "getRSPatchMesh warns and falls back to create() -- nothing "
        "renders on the PatchMesh side");
  check(regionHasContent(img, mid, img.width, 0, img.height),
        "patchmesh_baddim: control Sphere still renders, isolating the "
        "fallback to PatchMesh rather than the renderer as a whole");
}

// A step=1 basis's periodic axis has a tighter safety floor than
// n>=step alone; nu=1 falls below it and must warn and fall back to
// create(), exactly like patchmesh_baddim.rib's nonperiodic case above.
void testInvalidBSplinePeriodicDimensionFallback(const std::string &gman,
                                                 const std::string &ribDir) {
  const std::string rib =
      ribDir + "/malformed/patchmesh_bspline_periodic_baddim.rib";
  check(runGman(gman, rib) == 0,
        "patchmesh_bspline_periodic_baddim.rib renders without crashing "
        "(exit 0)");

  GmanImage img = readGmanTIFF("patchmesh_bspline_periodic_baddim.tif");
  check(img.ok, "patchmesh_bspline_periodic_baddim.rib: TIFF read back");
  if (!img.ok) {
    return;
  }
  const uint32_t mid = img.width / 2;
  check(! regionHasContent(img, 0, mid, 0, img.height),
        "patchmesh_bspline_periodic_baddim: nu=1 is below a step=1 "
        "basis's periodic safety floor, so getRSPatchMesh warns and "
        "falls back to create() -- nothing renders on the PatchMesh side");
  check(regionHasContent(img, mid, img.width, 0, img.height),
        "patchmesh_bspline_periodic_baddim: control Sphere still renders, "
        "isolating the fallback to PatchMesh rather than the renderer as "
        "a whole");
}

// A zero-step basis reaches RiPatchMeshV's nupatches/nvpatches division
// before getRSPatchMesh's own validation runs; it must warn and fall back
// to create() there, the same shape as the two fallbacks above.
void testZeroStepBasisFallback(const std::string &gman,
                               const std::string &ribDir) {
  const std::string rib = ribDir + "/malformed/patchmesh_zero_step_basis.rib";
  check(runGman(gman, rib) == 0,
        "patchmesh_zero_step_basis.rib renders without crashing (exit 0)");

  GmanImage img = readGmanTIFF("patchmesh_zero_step_basis.tif");
  check(img.ok, "patchmesh_zero_step_basis.rib: TIFF read back");
  if (!img.ok) {
    return;
  }
  const uint32_t mid = img.width / 2;
  check(! regionHasContent(img, 0, mid, 0, img.height),
        "patchmesh_zero_step_basis: a zero basis step warns and falls "
        "back to create() -- nothing renders on the PatchMesh side");
  check(regionHasContent(img, mid, img.width, 0, img.height),
        "patchmesh_zero_step_basis: control Sphere still renders, "
        "isolating the fallback to PatchMesh rather than the renderer as "
        "a whole");
}

}  // namespace

int main(int argc, char *argv[]) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s <gman-binary> <tests/rib-dir>\n", argv[0]);
    return 2;
  }
  const std::string gman = argv[1];
  const std::string ribDir = argv[2];

  testBilinearHandComputed();
  testBilinearPeriodicClosure();
  testBicubicCornerHandComputed();
  testBicubicMultiPatchCornerHandComputed();
  testBicubicPeriodicClosure();
  testBicubicBSplinePeriodicClosureAtMinimum();
  testMixedWrapCornerHandComputed();
  testNonperiodicBicubicZeroStepRejected();

  checkFixtureRenders(gman, ribDir, "patchmesh_bilinear");
  checkFixtureRenders(gman, ribDir, "patchmesh_bilinear_periodic");
  checkFixtureRenders(gman, ribDir, "patchmesh_bicubic");
  checkFixtureRenders(gman, ribDir, "patchmesh_bicubic_periodic");
  checkFixtureRenders(gman, ribDir, "patchmesh_mixed_wrap");
  checkFixtureRenders(gman, ribDir, "patchmesh_bspline_periodic");
  testInvalidDimensionFallback(gman, ribDir);
  testInvalidBSplinePeriodicDimensionFallback(gman, ribDir);
  testZeroStepBasisFallback(gman, ribDir);

  return checkSummary("PatchMesh bilinear and bicubic both rasterize");
}
