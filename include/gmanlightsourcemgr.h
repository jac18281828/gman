/* This is part of the GNU GMAN Library, a FREE implementation of the
 * RenderMan Interface Specification.
 *
 * Copyright (c) 2001, 2000, 1999 John Cairns 
 *
 * Author: John Cairns <john@2ad.com>
 */

// Added Light List LJL 2000/08/08

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
 

#ifndef __GMAN_GMANLIGHTSOURCEMGR_H
#define __GMAN_GMANLIGHTSOURCEMGR_H 1


/* Headers */

// STL
#include <list>
#include <map>
#include <stack>
#include <string>
#if HAVE_STD_NAMESPACE
using std::list;
#endif

// the renderman interface
#include "ri.h"
// the universal super class declaration
#include "universalsuperclass.h"

/*
 * RenderMan API GMANLightSourceMgr
 *
 * A Manager for multiple light source objects
 *
 */

class GMANDLL  GMANLightSourceMgr : public UniversalSuperClass {
public:
  GMANLightSourceMgr(); // default constructor

  ~GMANLightSourceMgr(); // default destructor
};


/* 
 * A class for light lists storage
 */

class GMANDLL GMANLightList
{
private:
  list<RtLightHandle> ll;
public:
  RtVoid  on  (RtLightHandle h);
  RtVoid  off (RtLightHandle h);
};

#endif



