/* This is part of the GNU GMAN Library, a FREE implementation of the
 * RenderMan Interface Specification.
 *
 * Copyright (c) 2001, 2000, 1999 John Cairns 
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
 

#ifndef __GMAN_GMANPOLYGON_H
#define __GMAN_GMANPOLYGON_H 1


/* Headers */

// STL
#include <vector>
#if HAVE_STD_NAMESPACE
using std::vector;
#endif

// the renderman interface
#include "ri.h"
// the universal super class declaration
#include "universalsuperclass.h"
// point type
#include "gmanpoint.h"
// face interface
#include "gmanface.h"
// segment or ray
#include "gmansegment.h"

// types
typedef vector<GMANPoint> PointVector;

/*
 * RenderMan API GMANPolygon
 *
 * A polygon defined by a series of points in space.
 *
 */

class GMANDLL  GMANPolygon {
  PointVector	points;
public:
  GMANPolygon(); // default constructor

  // create a polygon with n points
  GMANPolygon(int n);

  ~GMANPolygon(); // default destructor

  // add a point to the polygon.
  RtVoid addPoint(const GMANPoint &point);

  // set a point a position n
  RtVoid setPoint(int n, const GMANPoint &point);

  // reveal a point
  GMANPoint& operator[](int n) {
    return points[n];
  };

  // get number of points
  int getNPoints(RtVoid) { return points.size(); };
};


#endif

