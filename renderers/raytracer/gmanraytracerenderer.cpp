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
#include "gmanrenderer.h" /* Super class */
#include "gmanraytracerenderer.h" /* Declaration Header */
#include "gmanrayinterface.h"
#include "gmanraysphere.h"
#include "gmanworldmanager.h"

/*
 * RenderMan API GMANRaytraceRenderer
 *
 */

// default constructor
GMANRaytraceRenderer::GMANRaytraceRenderer() : GMANRenderer() { };


// default destructor 
GMANRaytraceRenderer::~GMANRaytraceRenderer() { };



void GMANRaytraceRenderer::render(GMANFrameBuffer *frameBuffer,
				  GMANViewingSystem *viewingSys,
				  const GMANOptions       &options,
				  const GMANAttributes    &attributes)
  throw (GMANError) {

  RtInt width, height;
  GMANRay ray;
  width = frameBuffer->getWidth();
  height = frameBuffer->getHeight();

  for (int x = 0; x < width; x++) {
    for (int y = 0; y < height; y++) {
      GMANColor color;
#if 0
      GMANRayInterface *prim = dynamic_cast<GMANRayInterface*>(worldManager.getFirst());
      const GMANRayInterface *hitPrimitive = NULL;
      RtFloat t,tempT;

      while(prim) {
	if ( prim->intersect(ray, tempT) ){
	  //We have a hit 
	  if (hitPrimitive == NULL || tempT<t);
	  t = tempT;
	  hitPrimitive = prim;
	}
	
	// FIXME FIXME FIXME
	// this should use a 'findNearest' directive in the BSP world
	// manager

	prim = (GMANRayInterface*)(worldManager.getNext());
      }
      GMANColor black( 1.f, 1.f, 1.f ), white( 0.f, 0.f, 0.f );
      if ( hitPrimitive != NULL ) {
	frameBuffer->setPixel(x, y, white); 
      }
      else {
	frameBuffer->setPixel(x, y, black); 
      }
#endif
    }
  }
}

GMANWorldManager *GMANRaytraceRenderer::getWorldManager(void) {
  return &worldManager;
}

// return its object manager
GMANObjectManager *GMANRaytraceRenderer::getObjectManager(void) {
  return &objectManager;
}

