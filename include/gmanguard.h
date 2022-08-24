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
 

#ifndef __GMAN_GMANGUARD_H
#define __GMAN_GMANGUARD_H 1


/* Headers */

// STL
#include <list>
#include <map>
#include <stack>
#include <string>

// the renderman interface
#include "ri.h"
// the mutex object
#include "gmanmutex.h"

/*
 * RenderMan API GMANGuard
 *
 */

// GMANGuard and GMANMutex don't inherit from UniversalSuperClass
// since they are used in the logging code

class GMANDLL  GMANGuard {
private:

    // the mutex to guard
    GMANMutex &mutex;

public:

  // create a new mutex instance
  GMANGuard(GMANMutex &m); // default constructor

  ~GMANGuard(); // default destructor

};


#endif

