/* SPDX-License-Identifier: LGPL-2.1-or-later */

/*----------------------------------------------------------
  Copyright (C) Lionel Joseph Lacour 2001, 2002
  February 2001 First release
  ----------------------------------------------------------
  Orthographic viewing system.
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
#include "gmanvsorthographic.h"

namespace gman {

VSOrthographic::VSOrthographic(RtInt xr, RtInt yr, GMANOptions::ScreenWindowStruct const& s,
                               const GMANMatrix4& worldToCamera, RtFloat nearDist, RtFloat farDist)
    : GMANViewingSystem(xr, yr, s, worldToCamera) {
  mtrx.prjOrtho(nearDist, farDist);
}
GMANPoint VSOrthographic::project(GMANPoint const& p) {
  GMANVector4 clip;
  clip.projTransform(p, mtrx.get());
  GMANPoint a;
  clip.perspective(a); // w==1 under ortho; a no-op divide
  RtFloat x = a.getX();
  RtFloat y = a.getY();
  screenToRaster(x, y);
  a.setX(x);
  a.setY(y);
  return a;
}
GMANRay VSOrthographic::cameraRay(RtFloat x, RtFloat y) const {
  rasterToScreen(x, y);

  // Every ray points down the view axis; the screen point sets the
  // origin instead. GMANViewingSystem::ray carries this into world space
  // via the camera-to-world transform captured at RiWorldBegin.
  return GMANRay(GMANPoint(x, y, 0), GMANVector(0, 0, 1));
}

/*
 * Every ray points down +z, so the view direction is the same at every
 * position and only the normal's z component decides. See
 * VSPerspective::facesCamera for the perspective measure.
 */
bool VSOrthographic::facesCamera(GMANPoint const&, GMANVector const& normal) const { return normal.getZ() > 0; }

RtMatrix const& VSOrthographic::getProjMatrix(RtVoid) const { return mtrx.get(); }

} // namespace gman
