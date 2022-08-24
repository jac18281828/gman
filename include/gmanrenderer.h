/* This is part of the GNU GMAN Library, a FREE implementation of the
 * RenderMan Interface Specification.
 *
 * Copyright (c) 2001, 2000, 1999 John Cairns 
 *
 * Author: John Cairns <john@2ad.com>
 */

/* Added RSP. LJL */
/* LJL - March 2001 - added lightBeams and solarBeams */
/*                  - added illuminance, illuminate and solar */

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
 

#ifndef __GMAN_GMANRENDERER_H
#define __GMAN_GMANRENDERER_H 1

/* Headers */

// STL
#include <list>
#include <map>
#include <stack>
#include <string>


#include "ri.h"
#include "universalsuperclass.h"

#include "gmanframebuffer.h"
#include "gmanworldmanager.h"
#include "gmanviewingsystem.h"

#include "gmanprimitives.h"
#include "gmanoptions.h"
#include "gmanattributes.h"
#include "gmantransform.h"

#include "gmanpoint.h"
#include "gmanvector.h"

#include "gmanworldmanager.h"
#include "gmanlinearworldmanager.h"
#include "gmanobjectmanager.h"
#include "gmanpatchpolyobjectmanager.h"

/*
 * RenderMan API GMANRenderer
 *
 * The default rendering object, produces a frame buffer from
 * an object manager and set of light sources and options.
 *
 */

class GMANDLL  GMANRenderer : public UniversalSuperClass
{
 protected:
  bool lightBeams (GMANPoint pos1, GMANVector axis1, RtFloat angle1,
		   GMANPoint pos2, GMANVector axis2, RtFloat angle2);
  bool solarBeams (GMANVector axis1, RtFloat angle1,
		   GMANVector axis2, RtFloat angle2);

 public:
  GMANRenderer(); // default constructor
  virtual ~GMANRenderer(); // default destructor

  virtual RtVoid illuminance(RtInt i,
			     GMANPoint const &p,
			     GMANVector const &axis,
			     RtFloat angle) throw(GMANError) = 0;

  virtual RtVoid illuminate(RtInt i,
			    GMANPoint const &p,
			    GMANVector const &axis,
			    RtFloat angle) throw(GMANError) = 0;

  virtual RtVoid solar(RtInt i, GMANVector const &axis, RtFloat angle)
    throw(GMANError) = 0;


    /**
     * To support output of depth all renderers must
     * compute and store depth information.
     *
     * After a call to the 'render' method, this method
     * should return the appropriate depth at xs, ys.
     */
    virtual RtFloat	getDepth(int xs, int ys) const = 0;

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
    throw(GMANError) = 0;


  /*
   * Each renderer provides its own world manager.
   */
  virtual GMANWorldManager *getWorldManager(RtVoid) = 0;

  /*
   * Each renderer provides its own object manager.
   */
  virtual GMANObjectManager *getObjectManager(RtVoid) = 0;

};


#endif

