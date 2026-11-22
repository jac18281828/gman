/* SPDX-License-Identifier: LGPL-2.1-or-later */

/* This is part of GMAN, a RenderMan-compatible renderer.
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

#include <cmath>
#include <numbers>

#include "gmaninvertiblematrix.h"
#include "gmanmath.h"
#include "gmanmatrix4.h"
#include "gmanparameterlist.h"
#include "gmanpoint.h"
#include "gmanprimitives.h"
#include "gmanray.h"
#include "gmanraybbox.h"
#include "gmanrayfinitehit.h"
#include "gmanrayparaboloid.h"
#include "gmanrayquadratic.h"
#include "gmantransform.h"
#include "gmanvector.h"
#include "ri.h"

namespace {

void paraboloidTangents(RtFloat zSpan, RtFloat thetamaxRad, RtFloat theta, double px, double py, double radial,
                        RtFloat v, GMANVector& dPdu, GMANVector& dPdv) {
  double const cosTheta = std::cos((double)theta), sinTheta = std::sin((double)theta);
  double const alongU = (double)thetamaxRad;
  double const radialRate = radial / (2.0 * (double)v);
  dPdu = GMANVector((RtFloat)(-alongU * py), (RtFloat)(alongU * px), 0.0);
  dPdv = GMANVector((RtFloat)(radialRate * cosTheta), (RtFloat)(radialRate * sinTheta), zSpan);
}

} // namespace

GMANRayParaboloid::GMANRayParaboloid(RtFloat rmax, RtFloat zmin, RtFloat zmax, RtFloat thetamax, GMANParameterList pl,
                                     GMANTransform const& transform)
    : GMANParaboloid(rmax, zmin, zmax, thetamax, pl),
      objectToCamera(gman::InvertibleMatrix::of(transform.interpolate(0.0))) {
  if (!objectToCamera)
    return;

  // x^2 + y^2 == k*(z - zmin), k == rmax^2 / (zmax*(zmax - zmin))
  // (GMANRayParaboloid::intersect's own substitution): at z == zmax this
  // gives radius rmax^2/zmax, independent of zmin's sign or ordering
  // against zmax, so |rmax| / sqrt(zmax) bounds x and y for the full
  // revolution. sqrt(zmax) is undefined for zmax <= 0 (intersect()'s own
  // guard already rejects zmax == 0; a negative zmax is the sphere's own
  // open question): leave the default box rather than compute one.
  if (zmax <= 0.0)
    return;

  RtFloat const r = (RtFloat)(std::fabs(rmax) / std::sqrt((double)zmax));
  bbox = gman::revolutionBBox(objectToCamera->forward(), r, zmin, zmax);
}

bool GMANRayParaboloid::intersect(GMANRay const& ray, GMANHit& hit) const {
  if (!objectToCamera)
    return false;
  gman::InvertibleMatrix const& placement = *objectToCamera;

  // A null wedge, a null z-band, a zero rmax or a zero zmax leaves no
  // surface to hit, the sphere's own guard against a NaN-filled hit: zmax
  // divides into k below. Its open question about negative thetamax and
  // negative radius stands unchanged here.
  if (thetamax == 0.0 || rmax == 0.0 || zmax == 0.0 || zmin == zmax)
    return false;

  GMANPoint const objOrigin = gman::transformPoint(placement.backward(), ray.getOrigin());
  GMANVector const objDirection = gman::transformDirection(placement.backward(), ray.getDirection());

  // Every surface point satisfies x^2+y^2+z^2 <= r^2 + max(zmin^2,
  // zmax^2), r == |rmax|/sqrt(|zmax|) (gmanraybbox.h's revolutionBBox's
  // own r for zmax > 0; the constructor's own default-box guard leaves
  // zmax <= 0 unbounded there, but intersect() below still needs a finite
  // radius for any zmax, hence the absolute value).
  double const rParaboloid = std::fabs((double)rmax) / std::sqrt(std::fabs((double)zmax));
  double const boundingRadius =
      std::sqrt(rParaboloid * rParaboloid + GMANMax((double)zmin * zmin, (double)zmax * zmax));

  gman::ShiftedRay shifted;
  if (!gman::shiftIntoBoundingSphere(objOrigin, objDirection, boundingRadius, shifted))
    return false;

  // GMANParaboloid::getLocation's radial factor is rmax*sqrt(v/zmax);
  // substituting v == (z - zmin) / (zmax - zmin) gives x^2 + y^2 ==
  // k*(z - zmin), k == rmax^2 / (zmax*(zmax - zmin)) -- quadratic in the
  // ray parameter t.
  double const k = (double)rmax * rmax / ((double)zmax * ((double)zmax - zmin));

  double const a = shifted.dx * shifted.dx + shifted.dy * shifted.dy;
  double const b = 2.0 * (shifted.dx * shifted.ox + shifted.dy * shifted.oy) - k * shifted.dz;
  double const c = shifted.ox * shifted.ox + shifted.oy * shifted.oy - k * (shifted.oz - (double)zmin);

  // a vanishes for a ray parallel to the axis (dx == dy == 0), the
  // apex-seeking case. gman::solveRayQuadratic's own comment covers the
  // linear case and why a near-degenerate a never reaches it.
  double t0 = 0.0, t1 = 0.0;
  int const numRoots = gman::solveRayQuadratic(a, b, c, t0, t1);
  if (numRoots == 0)
    return false;

  double const roots[2] = {t0, t1};
  RtFloat const thetamaxRad = GMANRadians(thetamax);

  for (int i = 0; i < numRoots; ++i) {
    double const tLocal = roots[i];
    gman::ShiftedRootPoint const root = gman::shiftedRootPoint(shifted, tLocal, placement.forward());
    if (!(ray.getTMin() <= root.t && root.t <= ray.getTMax()))
      continue;

    RtFloat const z = (RtFloat)root.pz;
    if (!(zmin <= z && z <= zmax))
      continue;

    RtFloat const theta =
        GMANMod(GMANAtan((RtFloat)root.py, (RtFloat)root.px), RtFloat(2.0) * std::numbers::pi_v<RtFloat>);
    if (!(theta <= thetamaxRad))
      continue;

    // The implicit surface's gradient (2x, 2y, -k) points along
    // GMANParaboloid::getNormal's own (cos theta, sin theta,
    // -fPrime/(zmax-zmin)) direction: substituting fPrime ==
    // rmax/(2*sqrt(zmax*v)) and r == rmax*sqrt(v/zmax) gives
    // -fPrime/(zmax-zmin) == -k/(2*r), the same ratio to (x, y) == r*(cos
    // theta, sin theta) that (2x, 2y, -k) carries -- so no division by r
    // is needed, and the apex (r == 0) is not a special case here.
    GMANVector objNormal((RtFloat)(2.0 * root.px), (RtFloat)(2.0 * root.py), (RtFloat)(-k));
    GMANVector normal = gman::transformNormal(placement, objNormal);
    normal.normalize();

    RtFloat const v = (z - zmin) / (zmax - zmin);
    GMANVector objDPdu;
    GMANVector objDPdv;
    if (((RtFloat)root.px != 0.0 || (RtFloat)root.py != 0.0) && v > 0.0)
      paraboloidTangents(zmax - zmin, thetamaxRad, theta, root.px, root.py,
                         std::sqrt(root.px * root.px + root.py * root.py), v, objDPdu, objDPdv);

    GMANHit candidate;
    candidate.t = root.t;
    candidate.point = root.point;
    candidate.normal = normal;
    candidate.u = theta / thetamaxRad;
    candidate.v = v;
    candidate.dPdu = gman::transformDirection(placement, objDPdu);
    candidate.dPdv = gman::transformDirection(placement, objDPdv);
    candidate.primitive = this;
    if (gman::acceptFiniteHit(candidate, hit))
      return true;
  }

  return false;
}
