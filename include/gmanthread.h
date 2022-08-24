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
 

#ifndef __GMAN_GMANTHREAD_H
#define __GMAN_GMANTHREAD_H 1


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
// the universal super class declaration
#include "universalsuperclass.h"

/*
 * RenderMan API GMANThread
 *
 */

class GMANDLL  GMANThread : public UniversalSuperClass {
private:
#ifdef WIN32
  HANDLE		thread;
  DWORD			threadId;
#else
  pthread_t		thread;
#endif
public:

  // public types
  typedef enum { PRIORITY_NORM, PRIORITY_MIN, PRIORITY_MAX } Priority;
  

  // create a thread context
  GMANThread(); // default constructor

   // this destructor does not join the
   // thread the caller must do this
  virtual ~GMANThread(); // default destructor


  // start the thread (run it)
  void start(void);

  // stop the thread
  // return: exit code for the thread returned by
  // the 'run' method
  int stop(void);

  
  // this method must be implemented in the 
  // child class.
  //
  // after the thread is created, this method is 
  // called in a new execution context.
  //
  // the return code is returned by the stop 
  // method
  virtual int run(void) = 0;


  // suspend the running thread
  void suspend(void);

  // resume the suspended thread
  void resume(void);

  // set the priority to one of the predefined priorities
  void setPriority(Priority pri);

  // return the priority of the currently running thread
  Priority getPriority(void);

  // wait for the thread to exit,
  // returns the exit code for the running thread
  int waitForExit(void);
};


#endif

