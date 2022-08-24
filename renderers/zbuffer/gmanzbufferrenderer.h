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
 

#ifndef __GMAN_GMANZBUFFERRENDERER_H
#define __GMAN_GMANZBUFFERRENDERER_H 1


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
// linear world manager
#include "gmanlinearworldmanager.h"
// patch poly object manager
#include "gmanobjectmanager.h"
#include "gmanpatchpolyobjectmanager.h"



/*
 * RenderMan API GMANZBufferRenderer
 *
 * A ZBuffer renderer.
 *
 */

class NOTGMANDLL GMANZBufferRenderer : public GMANRenderer {
private:

  // private types

  struct Point		  // integer screen position
  {
    int x, y;
  };

  struct VertexInfo	  // vertex information
  {
    
    Point	screen;   // screen coord
    GMANPoint   posn;     // scaled position
    GMANColor   color;    // vertex color

  };
  
  struct ScanInfo	  // a scan line intersection
  {
    RtFloat	x;	  //  x coord
    RtFloat   z;        //  pseudo-depth
    GMANColor   color;    //  color
  };

  struct EdgeInfo
  {
    bool first;		  // first intersection
    
    ScanInfo  isect[2];   // scan line intersection array
  };

  // private data
  RtFloat	*zbuffer; // the depth buffer.

  // private methods
  
  int ymin;   // minimum y-axis coordinate
  int ymax;   // maximum y-axis coordinate

  int width;  // display width
  int height; // display height

  int num_vert; // number of verticies
  
  EdgeInfo	*edge_list;

  VertexInfo	v_info[8];

  // a polygon clipper
  GMANPolygonClipper	clipper;


  GMANPatchPolyObjectManager		objectManager;
  
  GMANLinearWorldManager		worldManager;


  // private methods

  inline void setDepth(int x, int y, RtFloat f) {
    zbuffer[y*width + x] = f;
  }

  bool initZBuffer(void);

  RtVoid setZBuffer(RtInt x, RtInt y, RtFloat val);
  
  RtFloat getZBuffer(RtInt x, RtInt y) const;

  // populate vertex info array
  void getVertexInfo( GMANOutputPolygon & out );
  // edge scanning alg
  void scanEdges(void);
  // draw each of the scanned edges
  void drawEdgeList(GMANFrameBuffer *frameBuffer);

  // render each outpolygon
  void render(GMANOutputPolygon &out,
	      GMANFrameBuffer *frameBuffer) throw (GMANError);
  
public:
  GMANZBufferRenderer(int w, 
		      int h);

  GMANZBufferRenderer(); // default constructor

  ~GMANZBufferRenderer(); // default destructor

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



    inline RtFloat getDepth(int x, int y) const {
	return zbuffer[y*width + x];
    }




  /*
   * Apply a zbuffer environment to the objects in object manager
   * to generate a frameBuffer output.
   */
  virtual RtVoid render(GMANFrameBuffer *frameBuffer,
			GMANViewingSystem *viewingSys,
			const GMANOptions       &options,
			const GMANAttributes    &attributes) throw (GMANError);


  void setHeight(int h) {
    height = h;
  };

  int getHeight(void) {
    return height;
  };

  void setWidth(int w) {
    width = w;
  };

  int getWidth(void) {
    return width;
  };

  // return its world manager
  virtual GMANWorldManager *getWorldManager(void);

  // return its object manager
  virtual GMANObjectManager *getObjectManager(void);
};

#endif

