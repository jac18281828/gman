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
 

#ifndef __GMAN_GMANVERTEX4_H
#define __GMAN_GMANVERTEX4_H 1


/* Headers */

// STL
#include <list>
#include <map>
#include <stack>
#include <string>

// the renderman interface
#include "ri.h"
// the universal super class declaration
#include "universalsuperclass.h"
#include "gmanvector4.h"
#include "gmansurface.h"
#include "gmancolor.h"

/*
 * RenderMan API GMANVertex4
 *
 */

class GMANDLL  GMANVertex4 : public UniversalSuperClass {
private:
  GMANColor	color;
  GMANAlpha	alpha;
  GMANVector4   coord; // 4-d homogeneous coordinate

public:
  GMANVertex4(); // default constructor

  ~GMANVertex4(); // default destructor

  const GMANColor &getColor(RtVoid) const { return color; }

  const GMANAlpha &getAlpha(RtVoid) const { return alpha; }

  const GMANVector4  &getCoord(RtVoid) const { return coord; }

  RtVoid set(const GMANPoint &p, const GMANColor &c, const GMANAlpha &a, const RtMatrix &ptm) {
    coord.projTransform(p, ptm);

    color = c;
    alpha = a;
  }

  RtVoid set(const GMANVector4 &v, const GMANColor &c, const GMANAlpha &a) {
    coord = v;
    color = c;
    alpha = a;
  }

};


#endif

