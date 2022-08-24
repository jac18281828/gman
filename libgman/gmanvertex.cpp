/* This is part of the GNU GMAN Library, a FREE implementation of the
 * RenderMan Interface Specification.
 *
 * Copyright (c) 2001, 2000, 1999  John Cairns 
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

/* Local Headers */
#include "ri.h"      /* RenderMan Interface */
#include "universalsuperclass.h" /* Super class */
#include "gmanvertex.h" /* Declaration Header */
#include "gmanface.h"
#include "gmandefaults.h"


/*
 * RenderMan API GMANVertex
 *
 */

// default constructor
GMANVertex::GMANVertex() : UniversalSuperClass(),
			   location(0.0, 0.0, 0.0),
			   normal(0.0, 0.0, 0.0),
			   color(DefaultBGColor),
			   next(NULL) { };


// default destructor 
GMANVertex::~GMANVertex() { };



// calculate vertex normal
RtVoid GMANVertex::calcNormal() {
  // compute average normal for all faces
  // FIXME FIXME FIXME
#if 0
  for(FaceList::iterator face = flist->begin();
      face != flist->end();
      face++) {
    normal += (*face)->GetNormal();
  }
  // normalize vector
  normal.Normalize();
#endif

};
