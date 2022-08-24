/* LJL February 2001 */
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

#include "gmanpoint.h"
#include "gmanhpoint.h"

GMANPoint::GMANPoint(GMANHPoint &p)
{
  p.wToOne();
  c[X]=p.getX();
  c[Y]=p.getY();
  c[Z]=p.getZ();
}

/* operators */
//--------------------------------------------------
// == !=
bool GMANPoint::operator==(const GMANPoint &v) const
{
    for(RtInt i=0; i<NCOORDS; i++) {
	if(c[i] != v.c[i]) {
	    return false;
	}
    }
    return true;
}
bool GMANPoint::operator!=(const GMANPoint &v) const
{
    return !(*this == v);
}

bool GMANPoint::operator<(const GMANPoint &p) const {
    return ((c[X]<p.c[X]) && (c[Y]<p.c[Y]) && (c[Z]<p.c[Z]));
}

//-------------------------------------------------------------
GMANPoint GMANPoint::operator*(const GMANMatrix4 &m) const
{
    GMANPoint res(*this);

    res *= m;

    return res;
}

GMANPoint &GMANPoint::operator *=(const GMANMatrix4 &m) {

    // transform the point 
    RtPoint res;
    
    for(RtInt i=0; i<NCOORDS; i++) {
	res[i] = m[i][0]*c[X] +
	    m[i][1]*c[Y] +
	    m[i][2]*c[Z] +
	    m[i][3];
    }

    *this = res;

    return *this;
}
