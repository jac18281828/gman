/* SPDX-License-Identifier: LGPL-2.1-or-later */

/*---------------------------------------------------------
  Copyright (C) Lionel Joseph Lacour 2000, 2001
  December 2000  First release
  ---------------------------------------------------------
  Primitives datas storage.
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

#include <cstring>

#include <math.h>

#include "gmanmath.h"
#include "gmanprimitives.h"
#include "gmanvector.h"

namespace {

// Every quadric here differentiates its own getLocation and returns
// normalize(dP/du x dP/dv) -- the convention that makes the outward
// direction agree across primitives (verified sphere, cylinder and torus
// against their textbook closed forms; see phase-3-REPORT.md). A few
// primitives hit a coordinate singularity at a pole (dP/du -> 0), where the
// raw cross product degenerates to zero; guardTangent nudges the parameter
// away from the singularity before differentiating so the limit, not a
// division by zero, is what gets normalized.
inline double guardTangent(double v, double eps = 1e-6) { return (v < eps) ? eps : ((v > 1.0 - eps) ? 1.0 - eps : v); }

} // namespace

///////////////////////////////////////////////////////////////////////////////////////////////
////  GMAN_BLOBBY.CPP
///////////////////////////////////////////////////////////////////////////////////////////////

RtVoid GMANBlobby::copy(GMANBlobby const& b) {
  counter = b.counter;
  *counter += 1;
  nleaf = b.nleaf;
  code = b.code;
  nfloat = b.nfloat;
  floats = b.floats;
  nstrings = b.nstrings;
  strings = b.strings;
}
RtVoid GMANBlobby::destroy() {
  if ((*counter -= 1) == 0) {
    delete counter;
    delete[] code;
    delete[] floats;
    delete[] strings;
  }
}
GMANBlobby::GMANBlobby(RtInt nlf, RtInt ncd, RtInt cd[], RtInt nf, RtFloat f[], RtInt ns, RtString s[],
                       GMANParameterList p)
    : GMANPrimDatStorage(p) {
  int i;
  counter = new int;
  *counter = 1;

  nleaf = nlf;
  ncode = ncd;
  code = new RtInt[ncd];
  for (i = 0; i < ncd; i++)
    code[i] = cd[i];
  nfloat = nf;
  floats = new RtFloat[nf];
  for (i = 0; i < nf; i++)
    floats[i] = f[i];
  nstrings = ns;
  strings = new std::string[ns];
  for (i = 0; i < ns; i++)
    strings[i] = std::string(s[i]);
}
GMANBlobby::GMANBlobby(GMANBlobby const& b) : GMANPrimDatStorage(b) { copy(b); }
GMANBlobby::~GMANBlobby() { destroy(); }
GMANBlobby const& GMANBlobby::operator=(GMANBlobby const& b) {
  if (this != &b) {
    destroy();
    GMANPrimDatStorage* a1 = this;
    GMANPrimDatStorage const* a2 = &b;
    *a1 = *a2;
    copy(b);
  }
  return (*this);
}

///////////////////////////////////////////////////////////////////////////////
////  GMAN_CONE.CPP
///////////////////////////////////////////////////////////////////////////////
GMANPoint GMANCone::getLocation(double u, double v) {
  float theta = u * (thetamax / 360.0) * 2.0 * PI;

  float r = (1.0 - v) * radius;
  float x = r * cos(theta);
  float y = r * sin(theta);
  float z = v * height;

  return GMANPoint(x, y, z);
}

GMANVector GMANCone::getNormal(double u, double /*v*/) {
  // Closed form from the cone's implicit surface x^2+y^2 = g(z)^2, with
  // g(z) = radius*(1 - z/height): gradient (2x, 2y, 2*g(z)*radius/height)
  // simplifies, after dividing out g(z), to (cos theta, sin theta,
  // radius/height) -- independent of the radial coordinate, so unlike the
  // parametric cross product dP/du x dP/dv it stays well-defined at the
  // apex (r=0), where du no longer moves the surface.
  float theta = u * (thetamax / 360.0) * 2.0 * PI;

  GMANVector n(cos(theta), sin(theta), radius / height);
  n.normalize();
  return n;
}

///////////////////////////////////////////////////////////////////////////////////////////////
////  GMAN_CURVES.CPP
///////////////////////////////////////////////////////////////////////////////////////////////
RtVoid GMANCurves::copy(GMANCurves const& c) {
  type = c.type;
  ncurves = c.ncurves;
  nvertices = new RtInt[ncurves];
  for (int i = 0; i < ncurves; i++)
    nvertices[i] = c.nvertices[i];
  wrap = c.wrap;
}

GMANCurves::GMANCurves(RtToken ty, RtInt nc, RtInt* nv, RtToken wr, GMANParameterList p) : GMANPrimDatStorage(p) {
  type = ty;
  ncurves = nc;
  nvertices = new RtInt[ncurves];
  for (int i = 0; i < nc; i++)
    nvertices[i] = nv[i];
  wrap = wr;
}

GMANCurves::GMANCurves(GMANCurves const& c) : GMANPrimDatStorage(c) { copy(c); }

GMANCurves const& GMANCurves::operator=(GMANCurves const& c) {
  if (this != &c) {
    delete[] nvertices;
    GMANPrimDatStorage* a1 = this;
    GMANPrimDatStorage const* a2 = &c;
    *a1 = *a2;
    copy(c);
  }
  return (*this);
}

GMANCurves::~GMANCurves() { delete[] nvertices; }

///////////////////////////////////////////////////////////////////////////////
////  GMAN_CYLINDER.CPP
///////////////////////////////////////////////////////////////////////////////
GMANPoint GMANCylinder::getLocation(double u, double v) {
  float theta = u * (thetamax / 360.0) * 2.0 * PI;

  float x = radius * cos(theta);
  float y = radius * sin(theta);
  float z = zmin + (zmax - zmin) * v;

  return GMANPoint(x, y, z);
}

GMANVector GMANCylinder::getNormal(double u, double /*v*/) {
  // A cylinder wall's outward normal is radial from the axis and does not
  // depend on z, so it is the same at every v: (cos theta, sin theta, 0).
  float theta = u * (thetamax / 360.0) * 2.0 * PI;

  GMANVector n(cos(theta), sin(theta), 0.0);
  n.normalize();
  return n;
}

///////////////////////////////////////////////////////////////////////////////
////  GMAN_DISK.CPP
///////////////////////////////////////////////////////////////////////////////
GMANPoint GMANDisk::getLocation(double u, double v) {
  // A flat plate at z=height: u sweeps the angle, v sweeps the radius out
  // from the center (v=0) to the rim (v=1), matching the theta convention
  // every other quadric here uses.
  float theta = u * (thetamax / 360.0) * 2.0 * PI;
  float r = v * radius;
  float x = r * cos(theta);
  float y = r * sin(theta);

  return GMANPoint(x, y, height);
}

GMANVector GMANDisk::getNormal(double /*u*/, double /*v*/) {
  // dP/du x dP/dv for this parameterization is (0, 0, -r*radius*dtheta/du);
  // the disk's normal is that direction's sign, constant everywhere since
  // the surface is flat.
  GMANVector n(0.0, 0.0, -1.0);
  return n;
}

///////////////////////////////////////////////////////////////////////////////////////////////
////  GMAN_GENERALPOLYGON.CPP
///////////////////////////////////////////////////////////////////////////////////////////////
RtVoid GMANGeneralPolygon::copy(GMANGeneralPolygon const& gp) {
  nloop = gp.nloop;
  nvert = new RtInt[nloop];
  for (int i = 0; i < nloop; i++)
    nvert[i] = gp.nvert[i];
}

GMANGeneralPolygon::GMANGeneralPolygon(RtInt nl, RtInt nv[], GMANParameterList p) : GMANPrimDatStorage(p) {
  nloop = nl;
  nvert = new RtInt[nl];
  for (int i = 0; i < nl; i++)
    nvert[i] = nv[i];
}

GMANGeneralPolygon::GMANGeneralPolygon(GMANGeneralPolygon const& gp) : GMANPrimDatStorage(gp) { copy(gp); }

GMANGeneralPolygon const& GMANGeneralPolygon::operator=(GMANGeneralPolygon const& gp) {
  if (this != &gp) {
    delete[] nvert;
    GMANPrimDatStorage* a1 = this;
    GMANPrimDatStorage const* a2 = &gp;
    *a1 = *a2;
    copy(gp);
  }
  return (*this);
}

GMANGeneralPolygon::~GMANGeneralPolygon() { delete[] nvert; }

///////////////////////////////////////////////////////////////////////////////
////  GMAN_HYPERBOLOID.CPP
///////////////////////////////////////////////////////////////////////////////
// A hyperboloid is the surface swept by rotating the segment point1..point2
// around the z axis. The segment's own point at parameter v need not lie in
// the x=0 half-plane, so the sweep adds theta to that point's own azimuth
// phi(v), not to a fixed 0: P(u,v) = ( r(v)*cos(phi(v)+theta),
// r(v)*sin(phi(v)+theta), z(v) ), r(v)=dist to axis, phi(v)=atan2(y,x).
GMANPoint GMANHyperboloid::getLocation(double u, double v) {
  float xv = point1.getX() + v * (point2.getX() - point1.getX());
  float yv = point1.getY() + v * (point2.getY() - point1.getY());
  float zv = point1.getZ() + v * (point2.getZ() - point1.getZ());

  float r = sqrt(xv * xv + yv * yv);
  float phi = atan2(yv, xv);
  float theta = u * (thetamax / 360.0) * 2.0 * PI;

  return GMANPoint(r * cos(phi + theta), r * sin(phi + theta), zv);
}

GMANVector GMANHyperboloid::getNormal(double u, double v) {
  float dx = point2.getX() - point1.getX();
  float dy = point2.getY() - point1.getY();
  float dz = point2.getZ() - point1.getZ();

  float xv = point1.getX() + v * dx;
  float yv = point1.getY() + v * dy;

  float r = sqrt(xv * xv + yv * yv);
  float phi = atan2(yv, xv);
  // Guard against the segment crossing the axis (r=0), where phi and its
  // derivative are undefined; a hyperboloid degenerate enough to do that
  // mid-surface is a malformed scene, not a case worth failing hard on.
  float rSafe = (r < (float)RI_EPSILON) ? (float)RI_EPSILON : r;

  float rPrime = (xv * dx + yv * dy) / rSafe;
  float phiPrime = (xv * dy - yv * dx) / (rSafe * rSafe);

  float kt = thetamax / 360.0 * 2.0 * PI;
  float angle = phi + u * kt;

  GMANVector dPdu(-r * sin(angle) * kt, r * cos(angle) * kt, 0.0);
  GMANVector dPdv(rPrime * cos(angle) - r * sin(angle) * phiPrime, rPrime * sin(angle) + r * cos(angle) * phiPrime, dz);

  GMANVector n = dPdu.cross(dPdv);
  n.normalize();
  return n;
}

namespace {

// Cox-de Boor basis functions and their first derivative, Piegl and
// Tiller, The NURBS Book, algorithms A2.1 (FindSpan) and A2.3
// (DersBasisFuns, here always called for one derivative). Spans are
// half-open, [U[i], U[i+1)), except the last, which closes on the right
// so u == U[n+1] still resolves to span n rather than falling off the
// knot vector.
int findSpan(int n, int p, double u, std::vector<RtFloat> const& U) {
  if (u >= U[n + 1]) {
    return n;
  }
  int low = p, high = n + 1, mid = (low + high) / 2;
  while (u < U[mid] || u >= U[mid + 1]) {
    if (u < U[mid]) {
      high = mid;
    } else {
      low = mid;
    }
    mid = (low + high) / 2;
  }
  return mid;
}

// ders[0][0..p] are the p+1 nonzero basis functions at u over span; ders[1]
// their first derivative. nDeriv is always 1 here; carried as a parameter
// because A2.3 is written for the general case and specializing it by hand
// risks a transcription bug the book's own indices do not have.
void dersBasisFuns(int span, double u, int p, int nDeriv, std::vector<RtFloat> const& U,
                   std::vector<std::vector<double>>& ders) {
  std::vector<std::vector<double>> ndu(p + 1, std::vector<double>(p + 1));
  std::vector<double> left(p + 1), right(p + 1);
  ndu[0][0] = 1.0;
  for (int j = 1; j <= p; j++) {
    left[j] = u - U[span + 1 - j];
    right[j] = U[span + j] - u;
    double saved = 0.0;
    for (int r = 0; r < j; r++) {
      ndu[j][r] = right[r + 1] + left[j - r];
      double temp = ndu[r][j - 1] / ndu[j][r];
      ndu[r][j] = saved + right[r + 1] * temp;
      saved = left[j - r] * temp;
    }
    ndu[j][j] = saved;
  }

  ders.assign(nDeriv + 1, std::vector<double>(p + 1, 0.0));
  for (int j = 0; j <= p; j++) {
    ders[0][j] = ndu[j][p];
  }

  std::vector<std::vector<double>> a(2, std::vector<double>(p + 1));
  for (int r = 0; r <= p; r++) {
    int s1 = 0, s2 = 1;
    a[0][0] = 1.0;
    for (int k = 1; k <= nDeriv; k++) {
      double d = 0.0;
      int rk = r - k, pk = p - k;
      if (r >= k) {
        a[s2][0] = a[s1][0] / ndu[pk + 1][rk];
        d = a[s2][0] * ndu[rk][pk];
      }
      int j1 = (rk >= -1) ? 1 : -rk;
      int j2 = (r - 1 <= pk) ? k - 1 : p - r;
      for (int j = j1; j <= j2; j++) {
        a[s2][j] = (a[s1][j] - a[s1][j - 1]) / ndu[pk + 1][rk + j];
        d += a[s2][j] * ndu[rk + j][pk];
      }
      if (r <= pk) {
        a[s2][k] = -a[s1][k - 1] / ndu[pk + 1][r];
        d += a[s2][k] * ndu[r][pk];
      }
      ders[k][r] = d;
      int tmp = s1;
      s1 = s2;
      s2 = tmp;
    }
  }

  double factor = p;
  for (int k = 1; k <= nDeriv; k++) {
    for (int j = 0; j <= p; j++) {
      ders[k][j] *= factor;
    }
    factor *= (p - k);
  }
}

} // namespace

///////////////////////////////////////////////////////////////////////////////////////////////
////  GMAN_NUPATCH.CPP
///////////////////////////////////////////////////////////////////////////////////////////////
GMANNuPatch::GMANNuPatch(RtInt nu, RtInt uorder, RtFloat uknot[], RtFloat umin, RtFloat umax, RtInt nv, RtInt vorder,
                         RtFloat vknot[], RtFloat vmin, RtFloat vmax, RtFloat* p, bool rational, GMANParameterList pl)
    : GMANPrimDatStorage(pl), nu(nu), uorder(uorder), uknot(uknot, uknot + nu + uorder), umin(umin), umax(umax), nv(nv),
      vorder(vorder), vknot(vknot, vknot + nv + vorder), vmin(vmin), vmax(vmax), rational(rational),
      cpts(p, p + (std::size_t)nu * nv * (rational ? 4 : 3)) {}

void GMANNuPatch::evaluate(double u, double v, GMANPoint& S, GMANVector& Su, GMANVector& Sv) const {
  const double uu = umin + u * (umax - umin);
  const double vv = vmin + v * (vmax - vmin);

  const int spanU = findSpan(nu - 1, uorder - 1, uu, uknot);
  const int spanV = findSpan(nv - 1, vorder - 1, vv, vknot);

  std::vector<std::vector<double>> Nu, Nv;
  dersBasisFuns(spanU, uu, uorder - 1, 1, uknot, Nu);
  dersBasisFuns(spanV, vv, vorder - 1, 1, vknot, Nv);

  const int pntSize = rational ? 4 : 3;
  double A[4] = {0.0, 0.0, 0.0, 0.0};
  double Au[4] = {0.0, 0.0, 0.0, 0.0};
  double Av[4] = {0.0, 0.0, 0.0, 0.0};

  for (int b = 0; b < vorder; b++) {
    const int j = spanV - vorder + 1 + b;
    for (int a = 0; a < uorder; a++) {
      const int i = spanU - uorder + 1 + a;
      RtFloat const* cp = &cpts[(std::size_t)pntSize * (i + nu * j)];
      const double basis = Nu[0][a] * Nv[0][b];
      const double basisU = Nu[1][a] * Nv[0][b];
      const double basisV = Nu[0][a] * Nv[1][b];
      for (int c = 0; c < pntSize; c++) {
        A[c] += basis * cp[c];
        Au[c] += basisU * cp[c];
        Av[c] += basisV * cp[c];
      }
    }
  }

  if (rational) {
    // Homogeneous "Pw": S = A/w; the quotient rule gives S_u = (A_u -
    // w_u*S)/w. RiNuPatchV rejects every non-positive "Pw" weight before
    // this evaluator ever runs, so w > 0 always -- no defensive guard here.
    const double w = A[3];
    S = GMANPoint((RtFloat)(A[0] / w), (RtFloat)(A[1] / w), (RtFloat)(A[2] / w));
    Su = GMANVector((RtFloat)((Au[0] - Au[3] * S.getX()) / w), (RtFloat)((Au[1] - Au[3] * S.getY()) / w),
                    (RtFloat)((Au[2] - Au[3] * S.getZ()) / w));
    Sv = GMANVector((RtFloat)((Av[0] - Av[3] * S.getX()) / w), (RtFloat)((Av[1] - Av[3] * S.getY()) / w),
                    (RtFloat)((Av[2] - Av[3] * S.getZ()) / w));
  } else {
    S = GMANPoint((RtFloat)A[0], (RtFloat)A[1], (RtFloat)A[2]);
    Su = GMANVector((RtFloat)Au[0], (RtFloat)Au[1], (RtFloat)Au[2]);
    Sv = GMANVector((RtFloat)Av[0], (RtFloat)Av[1], (RtFloat)Av[2]);
  }
}

GMANPoint GMANNuPatch::getLocation(double u, double v) {
  GMANPoint S;
  GMANVector Su, Sv;
  evaluate(u, v, S, Su, Sv);
  return S;
}

GMANVector GMANNuPatch::getNormal(double u, double v) {
  GMANPoint S;
  GMANVector Su, Sv;
  evaluate(u, v, S, Su, Sv);

  GMANVector n = Su.cross(Sv);
  // Divide by the cross product's own magnitude, never through
  // GMANVector::normalize()'s absolute RI_EPSILON threshold -- a known
  // scale defect (AGENTS.md's normal handling note; see also
  // GMANPatch::getNormal's own comment). An exactly zero cross product
  // returns the zero vector, as GMANPatch already does.
  RtFloat mag = n.magnitude();
  if (mag != 0.0) {
    n /= mag;
  }
  return n;
}

///////////////////////////////////////////////////////////////////////////////
////  GMAN_PARABOLOID.CPP
///////////////////////////////////////////////////////////////////////////////
GMANPoint GMANParaboloid::getLocation(double u, double v) {
  float theta = u * (thetamax / 360.0) * 2.0 * PI;

  float factor = rmax * sqrt(v / zmax);
  float x = factor * cos(theta);
  float y = factor * sin(theta);
  float z = zmin + (zmax - zmin) * v;

  return GMANPoint(x, y, z);
}

GMANVector GMANParaboloid::getNormal(double u, double v) {
  // dP/du x dP/dv reduces to (cos theta, sin theta, -f'(v)/(zmax-zmin)),
  // where f(v)=rmax*sqrt(v/zmax) is getLocation's radial factor. f'(v)
  // diverges as v -> 0 (the apex), but the *normalized* direction still has
  // a well-defined limit there -- (0,0,-+1), the axis -- so guardTangent
  // keeps the division finite rather than branching on the singularity.
  double vg = guardTangent(v);
  float theta = u * (thetamax / 360.0) * 2.0 * PI;
  float fPrime = (float)(rmax / (2.0 * sqrt(zmax * vg)));

  GMANVector n(cos(theta), sin(theta), -fPrime / (zmax - zmin));
  n.normalize();
  return n;
}

///////////////////////////////////////////////////////////////////////////////////////////////
////  GMAN_PATCH.CPP
///////////////////////////////////////////////////////////////////////////////////////////////
GMANPatch::GMANPatch(RtToken pat, RtFloat* p, GMANParameterList pl) : GMANPrimDatStorage(pl), pt(pat), bicubic(false) {
  for (int i = 0; i < 4; i++) {
    corner[i] = GMANPoint(p[3 * i], p[3 * i + 1], p[3 * i + 2]);
  }
}

GMANPatch::GMANPatch(RtToken pat, RtFloat* p, GMANBasis const& b, GMANParameterList pl)
    : GMANPrimDatStorage(pl), pt(pat), bicubic(true), basis(b) {
  for (int i = 0; i < 48; i++) {
    cpts[i] = p[i];
  }
  cpts[48] = 0.0;
}

GMANPoint GMANPatch::getLocation(double u, double v) {
  if (bicubic) {
    return basis.bicubic((RtFloat)u, (RtFloat)v, cpts);
  }

  RtFloat uu = (RtFloat)u;
  RtFloat vv = (RtFloat)v;
  RtFloat w00 = (1 - uu) * (1 - vv);
  RtFloat w10 = uu * (1 - vv);
  RtFloat w01 = (1 - uu) * vv;
  RtFloat w11 = uu * vv;

  return GMANPoint(w00 * corner[0].getX() + w10 * corner[1].getX() + w01 * corner[2].getX() + w11 * corner[3].getX(),
                   w00 * corner[0].getY() + w10 * corner[1].getY() + w01 * corner[2].getY() + w11 * corner[3].getY(),
                   w00 * corner[0].getZ() + w10 * corner[1].getZ() + w01 * corner[2].getZ() + w11 * corner[3].getZ());
}

GMANVector GMANPatch::getNormal(double u, double v) {
  if (bicubic) {
    // GMANBasis::bicubic has no derivative of its own; central-difference
    // it instead. The evaluator is a cubic polynomial in u and v, defined
    // for any real argument, so sampling h past 0 or 1 needs no clamp.
    const RtFloat h = (RtFloat)1.0e-4;
    GMANPoint pu0 = basis.bicubic((RtFloat)(u - h), (RtFloat)v, cpts);
    GMANPoint pu1 = basis.bicubic((RtFloat)(u + h), (RtFloat)v, cpts);
    GMANPoint pv0 = basis.bicubic((RtFloat)u, (RtFloat)(v - h), cpts);
    GMANPoint pv1 = basis.bicubic((RtFloat)u, (RtFloat)(v + h), cpts);
    GMANVector dU(pu0, pu1);
    GMANVector dV(pv0, pv1);

    // dU and dV carry a (2h) factor from the finite difference; crossing
    // them raw scales the result by (2h)^2 = 4e-8, well below RI_EPSILON
    // for any surface whose true |dP/du x dP/dv| is under about 2.5e-3 --
    // normalize() then returns that tiny vector unchanged rather than a
    // unit normal, for geometry that is not actually degenerate.
    // Normalizing the tangents first cancels the step-size factor before
    // the cross product. It does not fix the fixed step itself: at large
    // coordinate magnitudes, 2h times the true derivative can be smaller
    // than one ulp of the coordinate, so both finite-difference samples
    // come back bit-identical and a tangent underflows to exactly zero
    // regardless of normalization -- most of what survives this fix is
    // that (a step-size limit, not fixed here), not genuine surface
    // degeneracy. Either way, GMANVector::normalize on a zero-magnitude
    // vector returns it unchanged rather than dividing by zero, so this
    // never produces a NaN normal.
    dU.normalize();
    dV.normalize();
    GMANVector n = dU.cross(dV);
    n.normalize();
    return n;
  }

  RtFloat uu = (RtFloat)u;
  RtFloat vv = (RtFloat)v;

  GMANVector eu0(corner[0], corner[1]); // P(1,0) - P(0,0)
  GMANVector eu1(corner[2], corner[3]); // P(1,1) - P(0,1)
  GMANVector dU = eu0 * (1 - vv) + eu1 * vv;

  GMANVector ev0(corner[0], corner[2]); // P(0,1) - P(0,0)
  GMANVector ev1(corner[1], corner[3]); // P(1,1) - P(1,0)
  GMANVector dV = ev0 * (1 - uu) + ev1 * uu;

  GMANVector n = dU.cross(dV);
  n.normalize();
  return n;
}

///////////////////////////////////////////////////////////////////////////////////////////////
////  GMAN_PATCHMESH.CPP
///////////////////////////////////////////////////////////////////////////////////////////////
GMANPatchMesh::GMANPatchMesh(RtToken pat, RtFloat* p, RtInt u, RtToken uw, RtInt v, RtToken vw, GMANParameterList pl)
    : GMANPrimDatStorage(pl), pt(pat), nu(u), uPeriodic(strcmp(uw, RI_PERIODIC) == 0), nv(v),
      vPeriodic(strcmp(vw, RI_PERIODIC) == 0), bicubic(false), cpts(p, p + (std::size_t)u * v * 3) {}

GMANPatchMesh::GMANPatchMesh(RtToken pat, RtFloat* p, RtInt u, RtToken uw, RtInt v, RtToken vw, GMANBasis const& b,
                             GMANParameterList pl)
    : GMANPrimDatStorage(pl), pt(pat), nu(u), uPeriodic(strcmp(uw, RI_PERIODIC) == 0), nv(v),
      vPeriodic(strcmp(vw, RI_PERIODIC) == 0), bicubic(true), basis(b), cpts(p, p + (std::size_t)u * v * 3) {}

GMANPoint GMANPatchMesh::point(RtInt i, RtInt j) const {
  const RtFloat* p = &cpts[3 * (i + nu * j)];
  return GMANPoint(p[0], p[1], p[2]);
}

// Same sub-patch selection as GMANBasis::bicubicMesh (see its own comment):
// clamp the raw sub-patch index into the valid range and let newU/newV
// carry the overshoot, so u==1.0 lands on the trailing edge of the last
// sub-patch rather than the leading edge of one that does not exist.
void GMANPatchMesh::bilinearPatch(double u, double v, GMANPoint& p00, GMANPoint& p10, GMANPoint& p01, GMANPoint& p11,
                                  RtFloat& newU, RtFloat& newV) const {
  RtInt nbupatch = uPeriodic ? nu : nu - 1;
  RtInt nbvpatch = vPeriodic ? nv : nv - 1;

  RtInt patchUStart = (RtInt)floor(u * nbupatch);
  if (patchUStart < 0) {
    patchUStart = 0;
  } else if (patchUStart >= nbupatch) {
    patchUStart = nbupatch - 1;
  }
  RtInt patchVStart = (RtInt)floor(v * nbvpatch);
  if (patchVStart < 0) {
    patchVStart = 0;
  } else if (patchVStart >= nbvpatch) {
    patchVStart = nbvpatch - 1;
  }

  newU = (RtFloat)(u * nbupatch - patchUStart);
  newV = (RtFloat)(v * nbvpatch - patchVStart);

  RtInt u1 = (patchUStart + 1 >= nu) ? patchUStart + 1 - nu : patchUStart + 1;
  RtInt v1 = (patchVStart + 1 >= nv) ? patchVStart + 1 - nv : patchVStart + 1;

  p00 = point(patchUStart, patchVStart);
  p10 = point(u1, patchVStart);
  p01 = point(patchUStart, v1);
  p11 = point(u1, v1);
}

GMANPoint GMANPatchMesh::getLocation(double u, double v) {
  if (bicubic) {
    return basis.bicubicMesh((RtFloat)u, (RtFloat)v, nu, uPeriodic, nv, vPeriodic, cpts.data());
  }

  GMANPoint p00, p10, p01, p11;
  RtFloat newU, newV;
  bilinearPatch(u, v, p00, p10, p01, p11, newU, newV);

  RtFloat w00 = (1 - newU) * (1 - newV);
  RtFloat w10 = newU * (1 - newV);
  RtFloat w01 = (1 - newU) * newV;
  RtFloat w11 = newU * newV;

  return GMANPoint(w00 * p00.getX() + w10 * p10.getX() + w01 * p01.getX() + w11 * p11.getX(),
                   w00 * p00.getY() + w10 * p10.getY() + w01 * p01.getY() + w11 * p11.getY(),
                   w00 * p00.getZ() + w10 * p10.getZ() + w01 * p01.getZ() + w11 * p11.getZ());
}

GMANVector GMANPatchMesh::getNormal(double u, double v) {
  if (bicubic) {
    // Mirrors GMANPatch::getNormal's bicubic branch: normalize each
    // central-differenced tangent before crossing, cancelling the (2h)
    // step-size factor. Its own comment records why the fixed step still
    // underflows at large coordinate magnitudes -- unfixed here for the
    // same reason.
    const RtFloat h = (RtFloat)1.0e-4;
    RtFloat* pts = cpts.data();
    GMANPoint pu0 = basis.bicubicMesh((RtFloat)(u - h), (RtFloat)v, nu, uPeriodic, nv, vPeriodic, pts);
    GMANPoint pu1 = basis.bicubicMesh((RtFloat)(u + h), (RtFloat)v, nu, uPeriodic, nv, vPeriodic, pts);
    GMANPoint pv0 = basis.bicubicMesh((RtFloat)u, (RtFloat)(v - h), nu, uPeriodic, nv, vPeriodic, pts);
    GMANPoint pv1 = basis.bicubicMesh((RtFloat)u, (RtFloat)(v + h), nu, uPeriodic, nv, vPeriodic, pts);
    GMANVector dU(pu0, pu1);
    GMANVector dV(pv0, pv1);
    dU.normalize();
    dV.normalize();
    GMANVector n = dU.cross(dV);
    n.normalize();
    return n;
  }

  GMANPoint p00, p10, p01, p11;
  RtFloat newU, newV;
  bilinearPatch(u, v, p00, p10, p01, p11, newU, newV);

  GMANVector eu0(p00, p10);
  GMANVector eu1(p01, p11);
  GMANVector dU = eu0 * (1 - newV) + eu1 * newV;

  GMANVector ev0(p00, p01);
  GMANVector ev1(p10, p11);
  GMANVector dV = ev0 * (1 - newU) + ev1 * newU;

  GMANVector n = dU.cross(dV);
  n.normalize();
  return n;
}

///////////////////////////////////////////////////////////////////////////////////////////////
////  GMAN_POINTSGENERALPOLYGONS.CPP
///////////////////////////////////////////////////////////////////////////////////////////////
RtVoid GMANPointsGeneralPolygons::copy(GMANPointsGeneralPolygons const& pgp) {
  int i;
  int j = 0;
  npolys = pgp.npolys;
  nloops = new RtInt[npolys];
  for (i = 0; i < npolys; i++) {
    nloops[i] = pgp.nloops[i];
    j += nloops[i];
  }
  int k = 0;
  nvertices = new RtInt[j];
  for (i = 0; i < j; i++) {
    nvertices[i] = pgp.nvertices[i];
    k += nvertices[i];
  }
  vertices = new RtInt[k];
  for (i = 0; i < k; i++) {
    vertices[i] = pgp.vertices[i];
  }
}

RtVoid GMANPointsGeneralPolygons::destroy() {
  delete[] nloops;
  delete[] nvertices;
  delete[] vertices;
}

GMANPointsGeneralPolygons::GMANPointsGeneralPolygons(RtInt np, RtInt nl[], RtInt nv[], RtInt v[], GMANParameterList p)
    : GMANPrimDatStorage(p) {
  int i;
  int j = 0;
  npolys = np;
  nloops = new RtInt[npolys];
  for (i = 0; i < npolys; i++) {
    nloops[i] = nl[i];
    j += nloops[i];
  }
  int k = 0;
  nvertices = new RtInt[j];
  for (i = 0; i < j; i++) {
    nvertices[i] = nv[i];
    k += nvertices[i];
  }
  vertices = new RtInt[k];
  for (i = 0; i < k; i++) {
    vertices[i] = v[i];
  }
}

GMANPointsGeneralPolygons::GMANPointsGeneralPolygons(GMANPointsGeneralPolygons const& pgp) : GMANPrimDatStorage(pgp) {
  int i;
  int j = 0;
  npolys = pgp.npolys;
  nloops = new RtInt[npolys];
  for (i = 0; i < npolys; i++) {
    nloops[i] = pgp.nloops[i];
    j += nloops[i];
  }
  int k = 0;
  nvertices = new RtInt[j];
  for (i = 0; i < j; i++) {
    nvertices[i] = pgp.nvertices[i];
    k += nvertices[i];
  }
  vertices = new RtInt[k];
  for (i = 0; i < k; i++) {
    vertices[i] = pgp.vertices[i];
  }
}

GMANPointsGeneralPolygons const& GMANPointsGeneralPolygons::operator=(GMANPointsGeneralPolygons const& pgp) {
  if (this != &pgp) {
    destroy();
    GMANPrimDatStorage* a1 = this;
    GMANPrimDatStorage const* a2 = &pgp;
    *a1 = *a2;
    copy(pgp);
  }
  return (*this);
}

GMANPointsGeneralPolygons::~GMANPointsGeneralPolygons() { destroy(); }

///////////////////////////////////////////////////////////////////////////////////////////////
////  GMAN_POINTSPOLYGONS.CPP
///////////////////////////////////////////////////////////////////////////////////////////////
RtVoid GMANPointsPolygons::copy(GMANPointsPolygons const& pp) {
  int j = 0, i;
  npolys = pp.npolys;
  nvertices = new RtInt[npolys];
  for (i = 0; i < npolys; i++) {
    nvertices[i] = pp.nvertices[i];
    j += nvertices[i];
  }
  vertices = new RtInt[j];
  for (i = 0; i < j; i++)
    vertices[i] = pp.vertices[i];
}
RtVoid GMANPointsPolygons::destroy() {
  delete[] nvertices;
  delete[] vertices;
}

GMANPointsPolygons::GMANPointsPolygons(RtInt np, RtInt nv[], RtInt v[], GMANParameterList p) : GMANPrimDatStorage(p) {
  int i, j = 0;
  npolys = np;
  nvertices = new RtInt[npolys];
  for (i = 0; i < npolys; i++) {
    nvertices[i] = nv[i];
    j += nvertices[i];
  }
  vertices = new RtInt[j];
  for (i = 0; i < j; i++)
    vertices[i] = v[i];
}

GMANPointsPolygons::GMANPointsPolygons(GMANPointsPolygons const& pp) : GMANPrimDatStorage(pp) { copy(pp); }

GMANPointsPolygons const& GMANPointsPolygons::operator=(GMANPointsPolygons const& pp) {
  if (this != &pp) {
    destroy();
    GMANPrimDatStorage* a1 = this;
    GMANPrimDatStorage const* a2 = &pp;
    *a1 = *a2;
    copy(pp);
  }
  return (*this);
}

GMANPointsPolygons::~GMANPointsPolygons() { destroy(); }

///////////////////////////////////////////////////////////////////////////////
////  GMAN_SPHERE.CPP
///////////////////////////////////////////////////////////////////////////////
GMANPoint GMANSphere::getLocation(double u, double v) {
  // zmin/zmax bound the sphere's latitude range: v=0 is the zmin cap,
  // v=1 the zmax cap. A full sphere (zmin=-radius, zmax=radius) recovers
  // the two poles; RiSphere 1 -0.5 1 360 (a partial sphere) is why this
  // can't just be "phi = v*2*PI" -- that always swept a full latitude
  // circle regardless of zmin/zmax.
  float phimin = (float)asin(GMANClamp<double>(zmin / radius, -1.0, 1.0));
  float phimax = (float)asin(GMANClamp<double>(zmax / radius, -1.0, 1.0));

  float theta = u * (thetamax / 360.0) * 2.0 * PI;
  float phi = phimin + v * (phimax - phimin);

  float cosPhi = cos(phi);
  float x = radius * cosPhi * cos(theta);
  float y = radius * cosPhi * sin(theta);
  float z = radius * sin(phi);

  return GMANPoint(x, y, z);
}

GMANVector GMANSphere::getNormal(double u, double v) {
  // A sphere is centered at its local origin, so the outward normal is
  // simply the radial direction: normalize(P).
  GMANPoint p = getLocation(u, v);
  GMANVector n(p.getX(), p.getY(), p.getZ());
  n.normalize();
  return n;
}

///////////////////////////////////////////////////////////////////////////////////////////////
////  GMAN_SUBDIVISIONMESH.CPP
///////////////////////////////////////////////////////////////////////////////////////////////
GMANSubdivisionTag::GMANSubdivisionTag(RtToken t, RtInt isize, RtInt fsize, RtInt* iargs, RtFloat* fargs)
    : tag(t), intargs(iargs, iargs + isize), floatargs(fargs, fargs + fsize) {}

GMANSubdivisionMesh::GMANSubdivisionMesh(RtToken schm, RtInt nf, RtInt nverts[], RtInt verts[], RtInt ntgs,
                                         RtToken tgs[], RtInt numargs[], RtInt iargs[], RtFloat fargs[],
                                         GMANParameterList p)
    : GMANPrimDatStorage(p) {
  counter = new RtInt;
  *counter = 1;

  int i, j, k;
  scheme = schm;
  nfaces = nf;
  nvertices = new RtInt[nfaces];
  for (i = 0, j = 0; i < nfaces; i++) {
    nvertices[i] = nverts[i];
    j += nverts[i];
  }
  vertices = new RtInt[j];
  for (i = 0; i < j; i++)
    vertices[i] = verts[i];

  ntags = ntgs;
  tags = new GMANSubdivisionTag*[ntags];
  for (i = j = k = 0; i < ntags; i++) {
    tags[i] = new GMANSubdivisionTag(tgs[i], numargs[i * 2], numargs[i * 2 + 1], &iargs[j], &fargs[k]);
    j += numargs[i * 2];
    k += numargs[i * 2 + 1];
  }
}

GMANSubdivisionMesh::GMANSubdivisionMesh(GMANSubdivisionMesh const& sbd) : GMANPrimDatStorage(sbd) { copy(sbd); }

GMANSubdivisionMesh const& GMANSubdivisionMesh::operator=(GMANSubdivisionMesh const& sbd) {
  if (this != &sbd) {
    destroy();
    GMANPrimDatStorage* a1 = this;
    GMANPrimDatStorage const* a2 = &sbd;
    *a1 = *a2;
    copy(sbd);
  }
  return (*this);
}

GMANSubdivisionMesh::~GMANSubdivisionMesh() { destroy(); }

RtVoid GMANSubdivisionMesh::copy(GMANSubdivisionMesh const& sbd) {
  counter = sbd.counter;
  *counter += 1;
  scheme = sbd.scheme;
  nfaces = sbd.nfaces;
  nvertices = sbd.nvertices;
  vertices = sbd.vertices;
  ntags = sbd.ntags;
  tags = sbd.tags;
}

RtVoid GMANSubdivisionMesh::destroy() {
  if (!(*counter -= 1)) {
    delete counter;
    delete[] nvertices;
    delete[] vertices;
    for (int i = 0; i < ntags; i++)
      delete tags[i];
    delete[] tags;
  }
}

///////////////////////////////////////////////////////////////////////////////
////  GMAN_TORUS.CPP
///////////////////////////////////////////////////////////////////////////////
GMANPoint GMANTorus::getLocation(double u, double v) {
  float theta = u * (thetamax / 360.0) * 2.0 * PI;
  float phi = (phimin + (phimax - phimin) * v) / 360.0 * 2.0 * PI;

  float factor = majorradius + minorradius * cos(phi);
  float x = factor * cos(theta);
  float y = factor * sin(theta);
  float z = minorradius * sin(phi);

  return GMANPoint(x, y, z);
}

GMANVector GMANTorus::getNormal(double u, double v) {
  // The tube cross-section is itself a circle, so the outward normal only
  // depends on phi (position around the tube) and theta (position around
  // the major radius) -- not on majorradius/minorradius's magnitudes.
  float theta = u * (thetamax / 360.0) * 2.0 * PI;
  float phi = (phimin + (phimax - phimin) * v) / 360.0 * 2.0 * PI;
  float cosPhi = cos(phi);

  GMANVector n(cosPhi * cos(theta), cosPhi * sin(theta), sin(phi));
  n.normalize();
  return n;
}
