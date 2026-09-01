/* SPDX-License-Identifier: LGPL-2.1-or-later */

/* This is part of the GNU GMAN Library, a FREE implementation of the
 * RenderMan Interface Specification.
 *
 * Copyright (c) 2001, 2000, 1999  John Cairns 
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


/* System headers */
#include <stdlib.h>

#include <cstdint>

/* Local Headers */
#include "ri.h"      /* RenderMan Interface */
#include "gmanlog.h"
#include "gmanthread.h" /* Declaration Header */


/*
 * GMANThread
 *
 */

// start routine for C thread functions

static void *GMANThreadStart(void *arg);

// default constructor
GMANThread::GMANThread() { };


// default destructor 
GMANThread::~GMANThread() { };

void GMANThread::start(void) {
  pthread_create(&thread, NULL, GMANThreadStart, (void*)this);
};



// stop the thread
// return: exit code for the thread returned by
// the 'run' method
int GMANThread::stop(void) {
  // This is not very nice :)
  return pthread_cancel(thread);
}

  
// suspend the running thread
void GMANThread::suspend(void) {
	
}

// resume the suspended thread
void GMANThread::resume(void) {

}


// set the priority to one of the predefined priorities
void GMANThread::setPriority(Priority pri) {
	int    policy;
	struct sched_param param;

	/* safe to get existing scheduling param */
	[[maybe_unused]] int ret = pthread_getschedparam (thread, &policy, &param);


	/* set the priority; others are unchanged */
	switch(pri) {
	case PRIORITY_MAX:
		param.sched_priority = 20;
		break;
	case PRIORITY_NORM:
		param.sched_priority = 10;
		break;
	case PRIORITY_MIN:
		param.sched_priority = 0;
		break;
	}

	/* setting the new scheduling param */
	ret = pthread_setschedparam (thread, policy, &param);

}

// return the priority of the currently running thread
GMANThread::Priority GMANThread::getPriority(void) {
	int    policy;
	struct sched_param param;

	/* safe to get existing scheduling param */
	// FIXME: pthread_getschedparam can return an error
	//int ret = pthread_getschedparam (thread, &policy, &param);
	pthread_getschedparam (thread, &policy, &param);
	if(param.sched_priority > 10) {
		return PRIORITY_MAX;
	} else if(param.sched_priority < 10) {
		return PRIORITY_MIN;
	}
	return PRIORITY_NORM;
}

// wait for the thread to exit,
// returns the exit code for the running thread
int GMANThread::waitForExit(void) {
	int *exitCd;

	pthread_join(thread, (void**)&exitCd);

	return static_cast<int>(reinterpret_cast<std::uintptr_t>(exitCd));

}

void *GMANThreadStart(void *arg) {

    // in our instance, 
    // arg is a pointer to an object of type GMAN Thread

    GMANThread *threadInst = (GMANThread*)arg;

    return reinterpret_cast<void *>(static_cast<std::uintptr_t>(threadInst->run()));

}

