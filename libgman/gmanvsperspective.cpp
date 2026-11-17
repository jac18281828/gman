/* SPDX-License-Identifier: LGPL-2.1-or-later */

/*----------------------------------------------------------
  Copyright (C) Lionel Joseph Lacour 2001, 2002
  February 2001 First release
  ----------------------------------------------------------
  Perspective viewing system.
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

#include "gmanvector.h"
#include "gmanvector4.h"
#include "gmanvsperspective.h"

namespace gman {

VSPerspective::VSPerspective(RtInt xr, RtInt yr, GMANOptions::ScreenWindowStruct const& s,
                             const GMANMatrix4& worldToCamera, RtFloat fov, RtFloat nearDist, RtFloat farDist)
    : GMANViewingSystem(xr, yr, s, worldToCamera) {
  mtrx.prjPersp(fov, nearDist, farDist);
}

GMANPoint VSPerspective::project(GMANPoint const& p) {
  GMANVector4 clip;
  clip.projTransform(p, mtrx.get());
  GMANPoint a;
  clip.perspective(a);
  RtFloat x = a.getX();
  RtFloat y = a.getY();
  screenToRaster(x, y);
  a.setX(x);
  a.setY(y);
  return a;
}
GMANRay VSPerspective::cameraRay(RtFloat x, RtFloat y) const {
  rasterToScreen(x, y);

  // project() divides camera-space x/y by z and then by tan(fov/2)
  // (mtrx[0][0] == cot(fov/2), GMANMatrix4::prjPersp); a ray through this
  // screen point has to invert that same factor, or it only lands back on
  // the point project() maps it to when fov happens to be 90 (tan(45) = 1).
  // The eye sits at the camera-space origin; the screen point at z=1 sets
  // the direction. GMANViewingSystem::ray carries this into world space
  // via the camera-to-world transform captured at RiWorldBegin.
  RtFloat const tanHalfFov = (RtFloat)1.0 / mtrx[0][0];
  return GMANRay(GMANPoint(0, 0, 0), GMANVector(x * tanHalfFov, y * tanHalfFov, 1));
}

/*
 * The eye sits at the camera-space origin, so the view vector to position
 * is position itself, unnormalized. It varies across the frame; comparing
 * against a fixed +z axis is the orthographic approximation
 * VSOrthographic::facesCamera keeps deliberately. The two agree on-axis and
 * diverge only near the silhouette.
 */
bool VSPerspective::facesCamera(GMANPoint const& position, GMANVector const& normal) const {
  return normal.dot(GMANVector(position)) > 0;
}

RtMatrix const& VSPerspective::getProjMatrix(RtVoid) const { return mtrx.get(); }

} // namespace gman
