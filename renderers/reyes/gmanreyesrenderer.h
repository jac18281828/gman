/* This is part of the GNU GMAN Library, a FREE implementation of the
 * RenderMan Interface Specification.
 *
 * Copyright (c) 2002, 2001, 2000, 1999 John Cairns 
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

#ifndef __GMAN_GMANREYESRENDERER_H
#define __GMAN_GMANREYESRENDERER_H 1


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
// gamma correction
#include "gmangamma.h"
// polygon clipper
#include "gmanpolygonclipper.h"
// octree world manager
#include "gmanoctreeworldmanager.h"
// patch poly object manager
#include "gmanobjectmanager.h"
#include "gmanpatchpolyobjectmanager.h"



/*
 * RenderMan API GMANReyesRenderer
 *
 * This is an implementation of the rendering scheme 
 * known as 'REYES.'   REYES is a local illumination model
 * similar to a zbuffer, with the major distinction that
 * polygons in the environment are 'diced' into tiny grids
 * called micropolygons.   The micropolygons are then 
 * stoicastically sampled to yield a color sample value 
 * that approximates reality.
 *
 * The REYES method has the advantage of being quite fast in
 * practical applications, however it has the disadvantage of being a 
 * local illumination model, which means that it can not account 
 * in a practical fashion for object aspects such as specularity
 * and transparency.  For this reason, this instance of REYES also 
 * applies a global illumination model, namely ray-tracing, when the 
 * object encountered has a specularity or transparency value that would
 * do better for raytracing.  This addition improves quality, while
 * taking away performance.   Cest La Vie!
 *
 * A nice future addition would be to implement a radiosity solution
 * along with the raytracer to provide a compromise hibridized
 * local-ray-radiosity model.  Of course the performance for such
 * a hibrid would be very bad.
 *
 * Your mileage may vary.
 * John
 */
class NOTGMANDLL GMANReyesRenderer : public GMANRenderer {
  private:

  RtInt height;

  RtInt width;

    // This is an array of faces which have been hit
    // during the REYES phase of the render
    // It is essentially a depth/zbuffer of objects
    // which will first intersect the rays projected from
    // the raytracer
    GMANFace		*firstHit;

    // this is the depth list computed for the objects
    // above
    RtFloat		*zBuffer;

    // the patch poly manager
    GMANPatchPolyObjectManager		objectManager;
    
    // the world manager (octree for use by ray tracing)
    GMANOctreeWorldManager		worldManager;

    // private methods

    // scan the polygon with reyes :)
    void reyes(GMANOutputPolygon &out,
	       GMANFrameBuffer    *frameBuffer) throw (GMANError);

    
    // trace a ray from screen x and screen y
    void traceRay(RtInt xs, 
		  RtInt ys,
		  GMANFrameBuffer    *frameBuffer,
		  GMANViewingSystem  *viewingSystem) throw (GMANError);

  // return true if the specified point is specular
  bool checkSpec(RtInt xs, RtInt ys);
  // return true if the specified point is transparent
  bool checkTrans(RtInt xs, RtInt ys);
  public:
    GMANReyesRenderer(); // default constructor
    
    ~GMANReyesRenderer(); // default destructor
    
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
     * Apply the reyes micropolygon model using viewingSystem
     * to generate a frameBuffer output.
     */
    virtual void render(GMANFrameBuffer *frameBuffer,
			GMANViewingSystem *viewingSys,
			const GMANOptions       &options,
			const GMANAttributes    &attributes) throw (GMANError);
    
    
    int getHeight(void) {
	return height;
    };

    int getWidth(void) {
	return width;
    };

    inline RtFloat getDepth(int x, int y) const {
	return zBuffer[y*width + x];
    }
    
    // return its world manager
    virtual GMANWorldManager *getWorldManager(void);
    
    // return its object manager
    virtual GMANObjectManager *getObjectManager(void);
};
#endif
