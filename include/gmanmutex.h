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
 

#ifndef __GMAN_GMANMUTEX_H
#define __GMAN_GMANMUTEX_H 1


/* Headers */

#ifdef WIN32
#include <windows.h>
#else
// this requires posix threads
#include <pthread.h>
#endif

// STL
#include <list>
#include <map>
#include <stack>
#include <string>

// the renderman interface
#include "ri.h"

/*
 * RenderMan API GMANMutex
 *
 */


// GMANGuard and GMANMutex don't inherit from UniversalSuperClass
// since they are used in the logging code
class GMANDLL  GMANMutex {
private:
#ifdef WIN32
  CRITICAL_SECTION		mutex;
#else
  pthread_mutex_t		mutex;
#endif
public:

  // create a new mutex instance
  GMANMutex(); // default constructor

  ~GMANMutex(); // default destructor


  // lock the mutex
  void lock(void);

  // attempt to get the lock, if the lock is busy
  // return false, if it is not, obtain the lock and return true. 
  bool tryLock(void);

  // unlock the mutex
  void unlock(void);
};


#endif

