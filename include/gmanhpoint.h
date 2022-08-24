/*---------------------------------------------------------
  Copyright (C) Lionel Joseph Lacour 2000, 2001
  February 2001  First release
  ---------------------------------------------------------
  Homogeneous coordinate point.
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

#ifndef __GMANHPOINT_H
#define __GMANHPOINT_H 1

#include "ri.h"
#include "universalsuperclass.h"
#include "gmanpoint.h"


class GMANDLL  GMANHPoint : public GMANPoint
{
protected:
  RtFloat w;
public:
  GMANHPoint() : GMANPoint(0,0,0), w(1.0) {}

  GMANHPoint(RtFloat x, RtFloat y, RtFloat z, RtFloat wv)
    : GMANPoint(x,y,z), w(wv) {}

  GMANHPoint(RtFloat *f)
    : GMANPoint(f[0], f[1], f[2]), w(f[3]) {}

  RtFloat getW() const { return w; };
  RtVoid  setW(RtFloat wv) { w=wv; };

  RtVoid wToOne();

  GMANHPoint operator+(const GMANHPoint &p) const;
  GMANHPoint operator*(RtFloat f) const;
  GMANHPoint operator*(const GMANMatrix4 &m) const;

  GMANHPoint &operator+=(const GMANHPoint &p);
  GMANHPoint &operator*=(RtFloat f);
  GMANHPoint &operator*=(const GMANMatrix4 &m);

};

#endif
