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
  FT COLLINS, CO 80525, USA, or write via E-mail john@2ad.com.
*/
 

#ifndef __GMAN_GMANLOADABLERENDERER_H
#define __GMAN_GMANLOADABLERENDERER_H 1


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
// Our parent class
#include "gmanrenderer.h"
// also the DSO loader
#include "gmanloadable.h"

/*
 * RenderMan API GMANLoadableRenderer
 *
 */

class GMANDLL  GMANLoadableRenderer : public GMANRenderer, GMANLoadable {
  
  public:
    // public types
    typedef GMANRenderer *	(*LoadRendererFnc)(RtVoid);

    static const char *		LoadRendererFncName;
  
  private:
    GMANRenderer		*renderer;
  
  public:
// default constructor
    GMANLoadableRenderer(const char *path) throw(GMANError); 

    ~GMANLoadableRenderer(); // default destructor

    RtVoid illuminance(RtInt i,
		       GMANPoint const &p,
		       GMANVector const &axis,
		       RtFloat angle) throw (GMANError) {}

    RtVoid illuminate(RtInt i,
		      GMANPoint const &p,
		      GMANVector const &axis,
		      RtFloat angle) throw (GMANError) {}

    RtVoid solar(RtInt i, GMANVector const &axis,
		 RtFloat angle) throw (GMANError) {}


    /*
     * Default rendering interface.
     *
     * A renderer applies a lighting and environment model to the 
     * objects in object manager, applies the projection represented
     * by the viewing system, and uses this information to
     * produce a frame buffer with a representation of the environment.
     */
    virtual RtVoid render(GMANFrameBuffer *frameBuffer,
			  GMANViewingSystem *viewingSys,
			  const GMANOptions       &options,
			  const GMANAttributes    &attributes)
	throw(GMANError);

    /*
     * Return the depth from the renderer.
     */
    virtual RtFloat getDepth(RtInt xs, RtInt ys) const;

    /*
     * Return a world manager appropriate to this renderer
     */

    virtual GMANWorldManager* getWorldManager(RtVoid);

    /*
     * Each renderer provides its own object manager.
     */
    virtual GMANObjectManager *getObjectManager(RtVoid);


};


#endif

