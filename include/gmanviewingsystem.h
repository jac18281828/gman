/* This is part of the GNU GMAN Library, a FREE implementation of the
 * RenderMan Interface Specification.
 *
 * Copyright (c) 2001, 2000, 1999 by John Cairns 
 *
 * Author: John Cairns <john@2ad.com>
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
 

#ifndef __GMAN_GMANVIEWINGSYSTEM_H
#define __GMAN_GMANVIEWINGSYSTEM_H 1

#include "ri.h"
#include "universalsuperclass.h"
#include "gmanpoint.h"
#include "gmanface.h"
#include "gmansegment.h"
#include "gmanoptions.h"

class GMANDLL  GMANViewingSystem : public UniversalSuperClass
{
  protected:
    RtInt xres;
    RtInt yres;
    GMANOptions::ScreenWindowStruct sw;
  public:
    GMANViewingSystem(RtInt xr, RtInt yr, 
		      const GMANOptions::ScreenWindowStruct &s)
	: xres(xr), yres(yr), sw(s) {}
    virtual ~GMANViewingSystem() {}
    
    /* LJL February 2001 + project & ray */
    RtVoid screenToRaster(RtFloat &x, RtFloat &y);
    RtVoid rasterToScreen(RtFloat &x, RtFloat &y);
    
    /* Project a point on screen (raster space coords) */
    virtual GMANPoint project(GMANPoint const &p)=0;
    
    /* Given x y coordinate in raster space, return a ray. */
    virtual GMANRay ray(RtFloat x, RtFloat y)=0;
    
    /* return true if the face is visible from the 
     * current perspective.
     */
    virtual bool visible(const GMANFace *face)=0;

    /*
     * return the current projective transformation
     * matrix
     */
    virtual const RtMatrix &getProjMatrix(RtVoid) const = 0;
};

#endif
