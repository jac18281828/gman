/* SPDX-License-Identifier: LGPL-2.1-or-later */

/* This is part of GMAN, a RenderMan-compatible renderer.
 *
 * Copyright (c) 2001, 2000, 1999 by John Cairns 
 *
 * Author: John Cairns <john@2ad.com>
 */

/*
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
 */
 

#ifndef __GMAN_GMANTHREAD_H
#define __GMAN_GMANTHREAD_H 1


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
// logging
#include "gmanlog.h"

/*
 * RenderMan API GMANThread
 *
 */

class GMAN_EXPORT  GMANThread {
private:
  pthread_t		thread;
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

