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
 

#ifndef __GMAN_GMANLOADABLE_H
#define __GMAN_GMANLOADABLE_H 1


/* Headers */
#ifdef WIN32
#include <windows.h>
#endif

// STL
#include <list>
#include <map>
#include <stack>
#include <string>

// the renderman interface
#include "ri.h"
// the universal super class declaration
#include "universalsuperclass.h"
// gman error object
#include "gmanerror.h"

/* Global loadable object types */

#ifdef WIN32
typedef HINSTANCE GMANLoadableObjectHandle;
#else
typedef RtVoid *GMANLoadableObjectHandle;
#endif


typedef struct {
  const char *name;
  const char *author;
  const char *copyright;
  const char *description;
} GMANLoadableObjectInfo;



/*
 * RenderMan API GMANLoadable
 *
 * This class provides the base support for loadable shared
 * objects within GMAN.
 *
 */

class GMANDLL  GMANLoadable : public UniversalSuperClass {
public:
  // a required function for every loadable object.
  // The function returning the loadable object info struct
  typedef	const GMANLoadableObjectInfo*	(*LoadInfoFnc)(RtVoid);

  static        const char *		getInfoFncName;

protected:

  GMANLoadableObjectHandle		object;

  const GMANLoadableObjectInfo		 *objInfo;

  /*
   * Protected methods
   */

  /*
   * Load a symbol by name form of the current object
   */
  RtVoid *loadSymbol(const char *symName);
  
public:
  // construct a loadable object from the
  // specified path
  GMANLoadable(const char *path) throw(GMANError); // default constructor

  ~GMANLoadable(); // default destructor

  // get name of DSO
  const char *getName(RtVoid) const;

  // get author of DSO
  const char *getAuthor(RtVoid) const;

  // get description of DSO
  const char *getDescription(RtVoid) const;

  // get copyright of DSO
  const char *getCopyright(RtVoid) const;

};


#endif

