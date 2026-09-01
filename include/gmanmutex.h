/* SPDX-License-Identifier: LGPL-2.1-or-later */

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
*/
 

#ifndef __GMAN_GMANMUTEX_H
#define __GMAN_GMANMUTEX_H 1


/* Headers */

// this requires posix threads
#include <pthread.h>

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
class GMAN_EXPORT  GMANMutex {
private:
  pthread_mutex_t		mutex;
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

