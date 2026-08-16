/* SPDX-License-Identifier: LGPL-2.1-or-later */

/*----------------------------------------------------------
  Copyright (C) Lionel Joseph Lacour 2001, 2002
  February 2001 First release
  ----------------------------------------------------------
  Orthographic viewing system.
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

#include "gmanvsorthographic.h"
#include "gmanvector4.h"

GMANVSOrthographic::GMANVSOrthographic(RtInt xr, RtInt yr,
				       const GMANOptions::ScreenWindowStruct &s,
				       const GMANMatrix4 &worldToCamera,
				       RtFloat nearDist, RtFloat farDist)
  : GMANViewingSystem(xr,yr,s,worldToCamera)
{
    mtrx.prjOrtho(nearDist,farDist);
}
GMANPoint GMANVSOrthographic::project(GMANPoint const &p)
{
  GMANVector4 clip;
  clip.projTransform(p, mtrx.get());
  GMANPoint a;
  clip.perspective(a); // w==1 under ortho; a no-op divide
  RtFloat x=a.getX();
  RtFloat y=a.getY();
  screenToRaster(x,y);
  a.setX(x);
  a.setY(y);
  return a;
}
GMANRay   GMANVSOrthographic::ray(RtFloat x, RtFloat y)
{
  GMANRay r;
  rasterToScreen(x,y);

  RtFloat srcOrigin[] = {x,y,0};
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
 * return true if the face is visible from this perspective. See
 * GMANVSPerspective::visible for the RiSides/RiOrientation rationale.
 */
bool GMANVSOrthographic::visible(const GMANFace *face) {
    if (face->getSides() != 1) {
      return true;
    }

    bool facingCamera = (face->getNormal().getZ() > 0);
    if (face->getOrientation() == RI_INSIDE) {
      facingCamera = !facingCamera;
    }
    return facingCamera;
}


const RtMatrix &GMANVSOrthographic::getProjMatrix(RtVoid) const {
    return mtrx.get();
}
