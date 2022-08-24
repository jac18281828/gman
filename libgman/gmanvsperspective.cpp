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

GMANVSPerspective::GMANVSPerspective(RtInt xr, RtInt yr, 
				     const GMANOptions::ScreenWindowStruct &s,
				     RtFloat fov, RtFloat nearDist, RtFloat farDist)
  : GMANViewingSystem(xr,yr,s)
{
    mtrx.prjPersp(fov,nearDist,farDist);
}

GMANPoint GMANVSPerspective::project(GMANPoint const &p)
{
  GMANPoint a(p*mtrx);
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
  GMANPoint a(0,0,0);
  GMANPoint b(x,y,1);
  r.setP1(a);
  r.setP2(b);
  return r;
}


/* 
 * return true if the object is visible from
 * this perspective
 */
bool GMANVSPerspective::visible(const GMANFace *face) {

    // determine if the incident angle from the line of
    // sight (z axis) 
    //
    // The general test for visibility is 
    // N_los dot N_face > 0
    // since the Normal to the line of sight is [0,0,1]
    // we get [0,0,0].[Nfx, Nfy, Nfz] > 0 
    // or
    return (face->getNormal().getZ() > 0);
}

const RtMatrix &GMANVSPerspective::getProjMatrix(RtVoid) const {
  return mtrx.get();
}
