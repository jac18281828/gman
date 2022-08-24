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

#include "gmanhpoint.h"
#include "gmanvector4.h"

RtVoid GMANHPoint::wToOne()
{
  w=1.0/w;
  c[X]*=w;
  c[Y]*=w;
  c[Z]*=w;
  w=1.0;
}

GMANHPoint &GMANHPoint::operator+=(const GMANHPoint &p)
{
	GMANPoint(*this) += GMANPoint(p);
	w+=p.getW();

  return *this;
}

GMANHPoint &GMANHPoint::operator*=(RtFloat f)
{
	GMANPoint(*this) *= f;
	w*=f;
  return *this;
}

GMANHPoint &GMANHPoint::operator*=(const GMANMatrix4 &m) 
{

  GMANVector4 vec(*this);

  vec *= m;
  GMANHPoint res(vec.getX(), vec.getY(), vec.getZ(), vec.getW());
  *this = res;
  return *this;
}

GMANHPoint GMANHPoint::operator+(const GMANHPoint &p) const
{
  GMANHPoint res(*this);
  res += p;
  return res;
}

GMANHPoint GMANHPoint::operator*(RtFloat f) const
{
  GMANHPoint res(*this);
  res *= f;
  return res;
}

GMANHPoint GMANHPoint::operator*(const GMANMatrix4 &m) const
{
	GMANHPoint res(*this);

	res *= m;

	return res;
}
