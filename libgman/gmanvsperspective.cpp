/* SPDX-License-Identifier: LGPL-2.1-or-later */

/*----------------------------------------------------------
  Copyright (C) Lionel Joseph Lacour 2001, 2002
  February 2001 First release
  ----------------------------------------------------------
  Perspective viewing system.
*/
/*
  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Library General Public
  License as published by the Free Software Foundation; either
  version 2 of the License, or (at your option) any later version.
  

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Library General Public License for more details.
  
  You should have received a copy of the GNU Library General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.

  To contact the author of GNU GMAN, write to John Cairns, 607 E STUART ST, 
  FT COLLINS, CO, 80525, USA, or write via E-mail john@2ad.com.
*/

#include "gmanvsperspective.h"
#include "gmanvector4.h"

GMANVSPerspective::GMANVSPerspective(RtInt xr, RtInt yr,
				     const GMANOptions::ScreenWindowStruct &s,
				     const GMANMatrix4 &worldToCamera,
				     RtFloat fov, RtFloat nearDist, RtFloat farDist)
  : GMANViewingSystem(xr,yr,s,worldToCamera)
{
    mtrx.prjPersp(fov,nearDist,farDist);
}

GMANPoint GMANVSPerspective::project(GMANPoint const &p)
{
  GMANVector4 clip;
  clip.projTransform(p, mtrx.get());
  GMANPoint a;
  clip.perspective(a);
  RtFloat x=a.getX();
  RtFloat y=a.getY();
  screenToRaster(x,y);
  a.setX(x);
  a.setY(y);
  return a;
}
GMANRay   GMANVSPerspective::ray(RtFloat x, RtFloat y)
{
  GMANRay r;
  rasterToScreen(x,y);

  // camera-space origin and a point along the ray direction, carried into
  // world space via the camera-to-world transform captured at RiWorldBegin.
  RtFloat srcOrigin[] = {0,0,0};
  RtFloat srcThrough[] = {x,y,1};
  RtFloat dstOrigin[3], dstThrough[3];
  GMANMatrix4 c2w = getCameraToWorld();
  c2w.p3m(1, srcOrigin, dstOrigin);
  c2w.p3m(1, srcThrough, dstThrough);

  r.setP1(GMANPoint(dstOrigin[0], dstOrigin[1], dstOrigin[2]));
  r.setP2(GMANPoint(dstThrough[0], dstThrough[1], dstThrough[2]));
  return r;
}


/*
 * return true if the face is visible from this perspective.
 *
 * RiSides 2 (the RenderMan default) means both sides are visible: no
 * culling. Only a single-sided face is culled, and then only by whether
 * its normal -- already in camera space, since it was computed from
 * already-transformed vertex positions at tessellation time -- faces the
 * camera. RiOrientation "inside" inverts which winding counts as facing
 * out.
 */
bool GMANVSPerspective::visible(const GMANFace *face) {
    if (face->getSides() != 1) {
      return true;
    }

    bool facingCamera = (face->getNormal().getZ() > 0);
    if (face->getOrientation() == RI_INSIDE) {
      facingCamera = !facingCamera;
    }
    return facingCamera;
}

const RtMatrix &GMANVSPerspective::getProjMatrix(RtVoid) const {
  return mtrx.get();
}
