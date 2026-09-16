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
 * NuPatch: GMANNuPatch now implements GMANParametric with a Cox-de Boor
 * evaluator, RiNuPatchV validates and sizes the request, parseNuPatch
 * checks knot lengths and owns its arrays, and getRSNuPatch wires the
 * surface through createParametric.
 *
 * Four proof mechanisms:
 *
 *  - Evaluator (white-box): GMANNuPatch instantiated directly against
 *    closed-form cases (a Bezier-equivalent patch checked against
 *    GMANPatch, an exact NURBS circle swept into a cylinder), run at
 *    three scales to keep every tolerance dimensionless.
 *  - Rendering: an order-2x2 NuPatch and the equivalent bilinear Patch,
 *    same points, same frame, compared pixel by pixel.
 *  - Texture coordinates no-op: an order-2x2 NuPatch with and without
 *    RiTextureCoordinates renders bit-for-bit the same, pinning the
 *    out-of-scope decision that getRSNuPatch always passes
 *    kIdentityCorners.
 *  - Malformed requests and exception safety: tests/paramclamp_test.cpp's
 *    own Fixture{file, expectedWarning} shape, plus a dedicated
 *    illegal-block case proving parseNuPatch's knot arrays survive a
 *    throw.
 */

#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "check.h"
#include "checkertexture.h"
#include "goldenimage.h"
#include "gmanparameterlist.h"
#include "gmanprimitives.h"

namespace {

// ---- geometry helpers ----

double hullExtent(std::vector<GMANPoint> const &points) {
  double minX = 1e300, maxX = -1e300;
  double minY = 1e300, maxY = -1e300;
  double minZ = 1e300, maxZ = -1e300;
  for (GMANPoint const &p : points) {
    minX = std::min(minX, (double) p.getX());
    maxX = std::max(maxX, (double) p.getX());
    minY = std::min(minY, (double) p.getY());
    maxY = std::max(maxY, (double) p.getY());
    minZ = std::min(minZ, (double) p.getZ());
    maxZ = std::max(maxZ, (double) p.getZ());
  }
  return std::max({maxX - minX, maxY - minY, maxZ - minZ});
}

bool pointsNear(GMANPoint const &a, GMANPoint const &b, double tol) {
  return std::fabs(a.getX() - b.getX()) <= tol &&
         std::fabs(a.getY() - b.getY()) <= tol &&
         std::fabs(a.getZ() - b.getZ()) <= tol;
}

// |n-hat . m-hat| >= 1 - tol; sameSign asserts the dot product itself
// (not its absolute value) meets that bound, for cases that also derive
// the expected sign.
bool normalsParallel(GMANVector const &a, GMANVector const &b, double tol,
                      bool sameSign) {
  GMANVector am = a, bm = b;
  double amag = am.magnitude();
  double bmag = bm.magnitude();
  if (amag == 0.0 || bmag == 0.0) {
    return false;
  }
  double dot = (a.getX() * b.getX() + a.getY() * b.getY() +
                a.getZ() * b.getZ()) /
               (amag * bmag);
  return sameSign ? dot >= 1.0 - tol : std::fabs(dot) >= 1.0 - tol;
}

// Scales every control point's cartesian coordinates by scale, in place.
// pntSize is 3 ("P") or 4 ("Pw" -- the fourth float is the weight, left
// untouched so the represented cartesian point still scales correctly:
// Pw's xyz is cartesian*weight, so scaling xyz alone scales the point).
std::vector<RtFloat> scaled(std::vector<RtFloat> const &base, double scale,
                             int pntSize) {
  std::vector<RtFloat> out(base);
  for (std::size_t i = 0; i + 2 < out.size(); i += pntSize) {
    out[i + 0] = (RtFloat) (base[i + 0] * scale);
    out[i + 1] = (RtFloat) (base[i + 1] * scale);
    out[i + 2] = (RtFloat) (base[i + 2] * scale);
  }
  return out;
}

const double kScales[] = {1e-3, 1.0, 1e3};

// ---- Evaluator (commit 2) ----

// A non-planar 4x4 control net, u-fastest/v-major (GMANPatch::bicubic's own
// layout): a shallow saddle, curved enough that dP/du x dP/dv never
// degenerates across the sample grid below.
std::vector<RtFloat> bezierGrid() {
  std::vector<RtFloat> p(48);
  for (int j = 0; j < 4; j++) {
    for (int i = 0; i < 4; i++) {
      const double x = (i - 1.5) * 0.3;
      const double y = (j - 1.5) * 0.3;
      const double z = 0.05 * ((i - 1.5) * (i - 1.5) - (j - 1.5) * (j - 1.5));
      const int idx = i + 4 * j;
      p[3 * idx + 0] = (RtFloat) x;
      p[3 * idx + 1] = (RtFloat) y;
      p[3 * idx + 2] = (RtFloat) z;
    }
  }
  return p;
}

std::vector<GMANPoint> asPoints3(std::vector<RtFloat> const &p) {
  std::vector<GMANPoint> pts;
  for (std::size_t i = 0; i + 2 < p.size(); i += 3) {
    pts.emplace_back(p[i], p[i + 1], p[i + 2]);
  }
  return pts;
}

// Order 4 both directions (degree 3, clamped Bezier knots), matched
// against a bicubic GMANPatch over the same points and a default
// (Bezier) GMANBasis -- both consume the same u-fastest/v-major array, so
// this is a direct evaluator-vs-evaluator comparison, not a hand-derived
// closed form.
void testBezierEquivalence() {
  RtFloat uknot[8] = {0, 0, 0, 0, 1, 1, 1, 1};
  RtFloat vknot[8] = {0, 0, 0, 0, 1, 1, 1, 1};
  const std::vector<RtFloat> base = bezierGrid();

  for (double scale : kScales) {
    std::vector<RtFloat> p = scaled(base, scale, 3);
    const double tol = 1e-5 * hullExtent(asPoints3(p));

    GMANParameterList pl;
    GMANNuPatch nupatch(4, 4, uknot, 0, 1, 4, 4, vknot, 0, 1, p.data(), false,
                        pl);
    GMANBasis basis;  // default: Bezier.
    GMANPatch patch((RtToken) RI_BICUBIC, p.data(), basis, pl);

    for (int iu = 0; iu <= 4; iu++) {
      for (int iv = 0; iv <= 4; iv++) {
        const double u = iu / 4.0;
        const double v = iv / 4.0;
        GMANPoint a = nupatch.getLocation(u, v);
        GMANPoint b = patch.getLocation(u, v);
        check(pointsNear(a, b, tol),
              "Bezier equivalence: getLocation(" + std::to_string(u) + "," +
                  std::to_string(v) + ") matches GMANPatch at scale " +
                  std::to_string(scale));

        // GMANPatch::getNormal central-differences and loses precision at
        // the other two scales on its own -- compare normals at scale 1
        // only.
        if (scale == 1.0) {
          GMANVector na = nupatch.getNormal(u, v);
          GMANVector nb = patch.getNormal(u, v);
          check(normalsParallel(na, nb, 1e-4, true),
                "Bezier equivalence: getNormal(" + std::to_string(u) + "," +
                    std::to_string(v) + ") matches GMANPatch");
        }
      }
    }
  }
}

// On a surface with clamped knots, getLocation(1,1) is the last control
// point's own cartesian position -- the span at umax/vmax closes on the
// right rather than falling off the knot vector.
void testClosedRightEnd() {
  RtFloat uknot[8] = {0, 0, 0, 0, 1, 1, 1, 1};
  RtFloat vknot[8] = {0, 0, 0, 0, 1, 1, 1, 1};
  const std::vector<RtFloat> base = bezierGrid();

  for (double scale : kScales) {
    std::vector<RtFloat> p = scaled(base, scale, 3);
    const double tol = 1e-5 * hullExtent(asPoints3(p));

    GMANParameterList pl;
    GMANNuPatch nupatch(4, 4, uknot, 0, 1, 4, 4, vknot, 0, 1, p.data(), false,
                        pl);
    GMANPoint last(p[3 * 15], p[3 * 15 + 1], p[3 * 15 + 2]);
    GMANPoint got = nupatch.getLocation(1.0, 1.0);
    check(pointsNear(got, last, tol),
          "Closed right end: getLocation(1,1) is the last control point "
          "at scale " + std::to_string(scale));
  }
}

// A rational NURBS circle of radius r (u: order 3, nine points, weights
// 1, sqrt(2)/2 alternating, starting and ending at 1, knots
// [0 0 0 .25 .25 .5 .5 .75 .75 1 1 1]) swept along v (order 2, z=0 to
// z=h) into an exact cylinder. Standard construction (Piegl and Tiller,
// The NURBS Book, Ex. 4.2): every homogeneous control point (i,j) is
// (r*cos(theta_i), r*sin(theta_i), h*j*w_i, w_i), theta_i = i*45deg,
// because for both the weight-1 corners and the weight-sqrt(2)/2
// midpoints, cartesian.xy * weight collapses to (r*cos theta_i,
// r*sin theta_i) -- the corner sits on the circle at weight 1; the
// midpoint sits at r*sqrt(2) from the axis at weight sqrt(2)/2, and
// sqrt(2)*sqrt(2)/2 = 1 cancels the radius back to r.
//
// A rational quadratic circle is exact for every u, not only at the
// knots: distance from the z axis is r and the normal is radial
// everywhere, so the grid below checks both at every sampled u, not only
// the u in {0.25, 0.5, 1} RiSpec's own knot values land on.
struct Cylinder {
  std::vector<RtFloat> pw;  // 9*2 control points, 4 floats each.
  double r;
  double h;
};

Cylinder buildCylinder(double r, double h) {
  Cylinder c;
  c.r = r;
  c.h = h;
  c.pw.resize(9 * 2 * 4);
  const double sqrt2over2 = std::sqrt(2.0) / 2.0;
  for (int j = 0; j < 2; j++) {
    for (int i = 0; i < 9; i++) {
      const double theta = i * 45.0 * M_PI / 180.0;
      const double w = (i % 2 == 0) ? 1.0 : sqrt2over2;
      const int idx = i + 9 * j;
      c.pw[4 * idx + 0] = (RtFloat) (r * std::cos(theta));
      c.pw[4 * idx + 1] = (RtFloat) (r * std::sin(theta));
      c.pw[4 * idx + 2] = (RtFloat) (h * j * w);
      c.pw[4 * idx + 3] = (RtFloat) w;
    }
  }
  return c;
}

std::vector<GMANPoint> cylinderHull(Cylinder const &c) {
  std::vector<GMANPoint> pts;
  for (std::size_t i = 0; i + 3 < c.pw.size(); i += 4) {
    const RtFloat w = c.pw[i + 3];
    pts.emplace_back(c.pw[i] / w, c.pw[i + 1] / w, c.pw[i + 2] / w);
  }
  return pts;
}

void testRationalCylinder() {
  RtFloat uknot[12] = {0, 0, 0, 0.25f, 0.25f, 0.5f, 0.5f,
                       0.75f, 0.75f, 1, 1, 1};
  RtFloat vknot[4] = {0, 0, 1, 1};
  const double baseR = 2.0, baseH = 3.0;

  for (double scale : kScales) {
    Cylinder c = buildCylinder(baseR * scale, baseH * scale);
    const double tol = 1e-5 * hullExtent(cylinderHull(c));

    GMANParameterList pl;
    GMANNuPatch cyl(9, 3, uknot, 0, 1, 2, 2, vknot, 0, 1, c.pw.data(), true,
                    pl);

    const double us[] = {0.0, 0.125, 0.25, 0.375, 0.5,
                         0.625, 0.75, 0.875, 1.0};
    const double vs[] = {0.0, 0.5, 1.0};
    for (double u : us) {
      for (double v : vs) {
        GMANPoint loc = cyl.getLocation(u, v);
        const double dist =
            std::sqrt((double) loc.getX() * loc.getX() +
                     (double) loc.getY() * loc.getY());
        check(std::fabs(dist - c.r) <= tol,
              "Rational cylinder: distance from z axis is r at u=" +
                  std::to_string(u) + " v=" + std::to_string(v) +
                  " scale=" + std::to_string(scale));
        check(std::fabs((double) loc.getZ() - c.h * v) <= tol,
              "Rational cylinder: z = h*v at u=" + std::to_string(u) +
                  " v=" + std::to_string(v) + " scale=" +
                  std::to_string(scale));

        GMANVector n = cyl.getNormal(u, v);
        GMANVector expected((RtFloat) loc.getX(), (RtFloat) loc.getY(), 0.0f);
        check(normalsParallel(n, expected, 1e-4, true),
              "Rational cylinder: normal is radial, outward, at u=" +
                  std::to_string(u) + " v=" + std::to_string(v) +
                  " scale=" + std::to_string(scale));
      }
    }
  }
}

// The same cylinder restricted to umin=0.25, umax=0.75: getLocation(0,v)
// on the sub-range surface equals the full-range surface at u=0.25, and
// getLocation(1,v) equals it at u=0.75.
void testSubRange() {
  RtFloat uknot[12] = {0, 0, 0, 0.25f, 0.25f, 0.5f, 0.5f,
                       0.75f, 0.75f, 1, 1, 1};
  RtFloat vknot[4] = {0, 0, 1, 1};
  const double baseR = 2.0, baseH = 3.0;

  for (double scale : kScales) {
    Cylinder c = buildCylinder(baseR * scale, baseH * scale);
    const double tol = 1e-5 * hullExtent(cylinderHull(c));

    GMANParameterList pl;
    GMANNuPatch full(9, 3, uknot, 0, 1, 2, 2, vknot, 0, 1, c.pw.data(), true,
                     pl);
    GMANNuPatch sub(9, 3, uknot, 0.25, 0.75, 2, 2, vknot, 0, 1, c.pw.data(),
                    true, pl);

    const double vs[] = {0.0, 0.5, 1.0};
    for (double v : vs) {
      check(pointsNear(sub.getLocation(0.0, v), full.getLocation(0.25, v),
                       tol),
            "Sub-range: getLocation(0,v) matches full range at u=0.25, "
            "v=" + std::to_string(v) + " scale=" + std::to_string(scale));
      check(pointsNear(sub.getLocation(1.0, v), full.getLocation(0.75, v),
                       tol),
            "Sub-range: getLocation(1,v) matches full range at u=0.75, "
            "v=" + std::to_string(v) + " scale=" + std::to_string(scale));
    }
  }
}

// ---- Rendering and texture-coordinates helpers ----

int runGman(std::string const &gman, std::string const &rib) {
  const std::string command =
      "\"" + gman + "\" \"" + rib + "\" >/dev/null 2>&1";
  int status = std::system(command.c_str());
  return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

void testRenderTwin(std::string const &gman, std::string const &ribDir) {
  check(runGman(gman, ribDir + "/nupatch_bilinear.rib") == 0,
        "nupatch_bilinear.rib renders");
  check(runGman(gman, ribDir + "/patch_bilinear_twin.rib") == 0,
        "patch_bilinear_twin.rib renders");
  checkGoldenImage("nupatch_bilinear.tif", "patch_bilinear_twin.tif",
                   GOLDEN_CHANNEL_TOL, GOLDEN_MAX_FRACTION,
                   "nupatch_bilinear_diff.tif");
}

bool pixelIdentical(GmanImage const &a, GmanImage const &b) {
  if (!a.ok || !b.ok || a.width != b.width || a.height != b.height) {
    return false;
  }
  for (uint32_t y = 0; y < a.height; ++y) {
    for (uint32_t x = 0; x < a.width; ++x) {
      if (a.at(x, y) != b.at(x, y)) {
        return false;
      }
    }
  }
  return true;
}

void testTextureCoordinatesNoOp(std::string const &gman,
                                std::string const &ribDir) {
  check(writeCheckerTexture("checker_texture.tif"),
        "checker_texture.tif writes into the render's working directory");
  check(runGman(gman, ribDir + "/nupatch_texturecoordinates.rib") == 0,
        "nupatch_texturecoordinates.rib renders");
  check(runGman(gman, ribDir + "/nupatch_texturecoordinates_default.rib") ==
            0,
        "nupatch_texturecoordinates_default.rib renders");

  GmanImage withCoords = readGmanTIFF("nupatch_texturecoordinates.tif");
  GmanImage defaultCoords =
      readGmanTIFF("nupatch_texturecoordinates_default.tif");
  check(withCoords.ok && defaultCoords.ok, "both renders read back");
  check(pixelIdentical(withCoords, defaultCoords),
        "TextureCoordinates never changes a NuPatch's render: "
        "nupatch_texturecoordinates.rib and "
        "nupatch_texturecoordinates_default.rib are pixel-identical");

  // The pair cannot agree by both rendering blank: two grid corners land
  // in different checker quadrants (identity corners, so s=u, t=v),
  // texture.rib's own frame projecting (u,v)=(0.25,0.25) to raster
  // (90,110) [red] and (0.75,0.25) to (110,110) [green].
  if (defaultCoords.ok && defaultCoords.width > 110 &&
      defaultCoords.height > 110) {
    uint32_t px1 = defaultCoords.at(90, 110);
    uint32_t px2 = defaultCoords.at(110, 110);
    int dr = std::abs((int) TIFFGetR(px1) - (int) TIFFGetR(px2));
    int dg = std::abs((int) TIFFGetG(px1) - (int) TIFFGetG(px2));
    int db = std::abs((int) TIFFGetB(px1) - (int) TIFFGetB(px2));
    check(dr > GOLDEN_CHANNEL_TOL || dg > GOLDEN_CHANNEL_TOL ||
              db > GOLDEN_CHANNEL_TOL,
          "nupatch_texturecoordinates_default.rib is not uniform: two "
          "pixels in different checker quadrants differ");
  } else {
    check(false, "nupatch_texturecoordinates_default.tif large enough to "
                 "sample both quadrant pixels");
  }
}

// ---- Malformed requests (commit 1) ----

struct RunResult {
  bool timedOut = false;
  bool crashed = false;
  int exitStatus = -1;
  std::string output;
};

// tests/paramclamp_test.cpp's own shape: fork/exec, waitpid,
// WIFEXITED/WEXITSTATUS, WIFSIGNALED crash detection.
RunResult runCapturingOutput(std::string const &gman, std::string const &rib,
                             int timeoutSeconds) {
  RunResult result;

  int pipeFds[2];
  if (pipe(pipeFds) != 0) {
    return result;
  }

  pid_t pid = fork();
  if (pid < 0) {
    close(pipeFds[0]);
    close(pipeFds[1]);
    return result;
  }
  if (pid == 0) {
    close(pipeFds[0]);
    dup2(pipeFds[1], STDOUT_FILENO);
    dup2(pipeFds[1], STDERR_FILENO);
    close(pipeFds[1]);
    execl(gman.c_str(), gman.c_str(), rib.c_str(), (char *) nullptr);
    _exit(127);
  }
  close(pipeFds[1]);

  char buf[4096];
  ssize_t n;
  while ((n = read(pipeFds[0], buf, sizeof(buf))) > 0) {
    result.output.append(buf, (std::size_t) n);
  }
  close(pipeFds[0]);

  const int pollIntervalUs = 50 * 1000;
  const int maxPolls = (timeoutSeconds * 1000000) / pollIntervalUs;
  int status = 0;
  for (int i = 0; i < maxPolls; ++i) {
    pid_t r = waitpid(pid, &status, WNOHANG);
    if (r == pid) {
      if (WIFEXITED(status)) {
        result.exitStatus = WEXITSTATUS(status);
      } else if (WIFSIGNALED(status)) {
        result.crashed = true;
      }
      return result;
    }
    usleep(pollIntervalUs);
  }

  result.timedOut = true;
  kill(pid, SIGKILL);
  waitpid(pid, &status, 0);
  return result;
}

void testMalformedFixtures(std::string const &gman,
                           std::string const &malformedDir) {
  struct Fixture {
    char const *file;
    char const *expectedWarning;
  };
  const Fixture fixtures[] = {
      {"nupatch_short_uknot.rib",
       "NuPatch: uknot length 3 does not match nu + uorder = 4; ignoring."},
      {"nupatch_decreasing_knots.rib",
       "NuPatch: uknot[2]=0.25 is less than uknot[1]=0.5, not "
       "non-decreasing; ignoring."},
      {"nupatch_order_exceeds_n.rib",
       "NuPatch: nu=2 uorder=3 violates nu >= uorder >= 1; ignoring."},
      {"nupatch_empty_range.rib",
       "NuPatch: umin=0.5 umax=0.5 violates umin < umax; ignoring."},
      {"nupatch_umin_below_knot.rib",
       "NuPatch: umin=-0.1 is less than uknot[uorder-1]=0; ignoring."},
      {"nupatch_short_pw.rib",
       "Parameter \"Pw\": declared length 16, supplied length 14; "
       "clamping and zero-filling the remainder."},
      {"nupatch_zero_weight.rib",
       "NuPatch: control point 2 has non-positive \"Pw\" weight 0; "
       "ignoring."},
  };

  for (Fixture const &fixture : fixtures) {
    const std::string rib = malformedDir + "/" + fixture.file;
    RunResult r = runCapturingOutput(gman, rib, 10);
    check(!r.timedOut,
          std::string(fixture.file) + ": does not hang (10s bound)");
    check(!r.crashed, std::string(fixture.file) + ": does not crash");
    check(r.exitStatus == 0,
          std::string(fixture.file) + ": exits 0 (degrade, don't abort)");
    check(r.output.find(fixture.expectedWarning) != std::string::npos,
          std::string(fixture.file) + ": warns naming the rule and its "
                                      "values");
  }
}

// ---- Exception safety (commit 1) ----

void testIllegalBlockLeaksNothing(std::string const &gman,
                                  std::string const &malformedDir) {
  const std::string rib = malformedDir + "/nupatch_illegal_block.rib";
  RunResult r = runCapturingOutput(gman, rib, 10);
  check(!r.timedOut, "nupatch_illegal_block.rib: does not hang (10s bound)");
  check(!r.crashed, "nupatch_illegal_block.rib: does not crash");
  check(r.exitStatus == 1,
        "nupatch_illegal_block.rib: exits 1 (GMANHandleError, "
        "RIE_ILLSTATE) -- on Linux, a leak overrides this with LSan's own "
        "23, so this assertion doubles as the leak-freedom proof under "
        "ctest --test-dir build-debug");
}

}  // namespace

int main(int argc, char *argv[]) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s <gman-binary> <tests/rib dir>\n",
                argv[0]);
    return 2;
  }
  const std::string gman = argv[1];
  const std::string ribDir = argv[2];
  const std::string malformedDir = ribDir + "/malformed";

  testBezierEquivalence();
  testClosedRightEnd();
  testRationalCylinder();
  testSubRange();

  testRenderTwin(gman, ribDir);
  testTextureCoordinatesNoOp(gman, ribDir);

  testMalformedFixtures(gman, malformedDir);
  testIllegalBlockLeaksNothing(gman, malformedDir);

  return checkSummary("nupatch holds");
}
