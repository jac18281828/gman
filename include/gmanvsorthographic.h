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

#ifndef __GMANVSORTHOGRAPHIC_H
#define __GMANVSORTHOGRAPHIC_H 1

#include "gmanviewingsystem.h"
#include "gmanmatrix4.h"

class GMANDLL  GMANVSOrthographic : public GMANViewingSystem
{
  private:
    GMANMatrix4 mtrx;
  public:
    GMANVSOrthographic(RtInt xres, RtInt yres, 
		       const GMANOptions::ScreenWindowStruct &s,
		       RtFloat nearDist, RtFloat farDist);
    ~GMANVSOrthographic() {}
    
    virtual GMANPoint project(GMANPoint const &p);
    virtual GMANRay   ray(RtFloat x, RtFloat y);
    
    virtual bool visible(const GMANFace *face);

    virtual const RtMatrix &getProjMatrix(RtVoid) const;
};

#endif
