/*----------------------------------------------------------
  Copyright (C) Lionel Joseph Lacour 2000, 2001
  February 2001 First release
  ----------------------------------------------------------
  Sphere ray intersection code
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

#ifndef __GMANRAYSPHERE_H
#define __GMANRAYSPHERE_H 1

#include "gmanrayinterface.h"

class NOTGMANDLL GMANRaySphere : public GMANRayInterface, public GMANSphere
{
public:
  GMANRaySphere(RtFloat radius, RtFloat zmin, RtFloat zmax, RtFloat tmax,
		GMANParameterList pl)
    : GMANSphere(radius,zmin,zmax,tmax,pl) {}

  bool intersect (const GMANRay &ray, RtFloat& t) const;
};

#endif
