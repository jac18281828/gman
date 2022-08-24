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
  FT COLLINS, CO 80525, USA, or write via E-mail john@2ad.com.
*/

/* Local Headers */
#include "ri.h"      /* RenderMan Interface */
#include "gmanrenderer.h" /* Super class */
#include "gmanloadablerenderer.h" /* Declaration Header */
#include "gmanlinearworldmanager.h"

/*
 * RenderMan API GMANLoadableRenderer
 *
 */

const char *		GMANLoadableRenderer::LoadRendererFncName = "GMANLoadRenderer";

// default constructor
GMANLoadableRenderer::GMANLoadableRenderer(const char *path) throw(GMANError) 
  : GMANRenderer(), GMANLoadable(path), renderer(NULL) { 

  LoadRendererFnc loadRenderer = 
    (LoadRendererFnc)loadSymbol(LoadRendererFncName);
  
  if(loadRenderer == NULL) {
    throw(GMANError(RIE_SYSTEM, RIE_SEVERE, "Loadable module missing renderer."));
  }

  renderer = loadRenderer();

  if(renderer == NULL) {
    throw(GMANError(RIE_SYSTEM, RIE_SEVERE, "Loadable module missing renderer."));
  }
};


// default destructor 
GMANLoadableRenderer::~GMANLoadableRenderer() { };

/*
 * Default rendering interface.
 *
 * A renderer applies a lighting and environment model to the 
 * objects in object manager, applies the projection represented
 * by the viewing system, and uses this information to
 * produce a frame buffer with a representation of the environment.
 */
RtVoid GMANLoadableRenderer::render(GMANFrameBuffer *frameBuffer,
				    GMANViewingSystem *viewingSys,
				    const GMANOptions       &options,
				    const GMANAttributes    &attributes) 
  throw(GMANError)
{
  if(renderer) {
    renderer->render(frameBuffer, viewingSys, options, attributes);
  } else {
    throw(GMANError(RIE_SYSTEM, RIE_SEVERE, "No renderer available."));
  }
}

GMANWorldManager* GMANLoadableRenderer::getWorldManager(RtVoid)
{
  return renderer->getWorldManager();
}

GMANObjectManager* GMANLoadableRenderer::getObjectManager(RtVoid)
{
  return renderer->getObjectManager();
}


RtFloat GMANLoadableRenderer::getDepth(RtInt xs, RtInt ys) const {
    return renderer->getDepth(xs, ys);
}
