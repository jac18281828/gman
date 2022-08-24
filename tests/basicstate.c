/* This is part of the GNU GMAN Library, a FREE implementation of the
 * RenderMan Interface Specification.
 *
 * Copyright (c) 2001, 2000, 1999, John Cairns 
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

#include "ri.h"

/* main */
int main(int argc, char *argv[]) {

  // basic object
  RtObjectHandle		simpleObject;

  //RtInt	origin[2]		= { 10, 10 };

  RtColor red                   = { 1.0, 0.0, 0.0 };



//  RiBegin("zbuffer");
  RiBegin("libgmanraytracer.so");
  
  // create object
  simpleObject = RiObjectBegin();
  // too simple
  RiSphere(0.5f, -0.5f, 0.5f, 360.0f, RI_NULL);
  RiObjectEnd();


  //RiDisplay("basicstate.pnm", "file", "rgba", "origin", (RtPointer)origin, 
//	    RI_NULL);

  RiDisplay("basicstate.pnm", "file", "rgba", RI_NULL);
  RiFormat(800, 600, 1.0);

  RiFrameAspectRatio(1.0);

  /* would default with above setting */
  RiScreenWindow(-1.0, 1.0, -1.0, 1.0);

  RiFrameBegin(0);

  /* camera */

  RiProjection(RI_PERSPECTIVE, RI_NULL);

  RiWorldBegin();

  RiRotate(17.0, 1.0, 1.0, 1.0);
  RiColor(red);
  RiTranslate(1.0, 1.0, 1.0);

  RiObjectInstance(simpleObject);

  RiWorldEnd();

  RiFrameEnd();

  RiEnd();

  return 0;
}
