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

  To contact the author of GNU GMAN, write to John Cairns, 607 E STUART ST, 
  FT COLLINS, CO 80525, USA, or write via E-mail john@2ad.com.
*/

#ifdef WIN32
#include <windows.h>
#endif

/* System headers */
#include <stdlib.h>

/* Local Headers */
#include "ri.h"      /* RenderMan Interface */
#include "universalsuperclass.h" /* Super class */
#include "gmanthread.h" /* Declaration Header */


/*
 * GMANThread
 *
 */

// start routine for C thread functions

#ifdef WIN32
static unsigned long  __stdcall GMANThreadStart(void *arg);

// default constructor
GMANThread::GMANThread() : UniversalSuperClass() { 
	// no initialization required
};


// default destructor 
GMANThread::~GMANThread() { 
	// this destructor does not join the
	// thread the caller must do this
};

void GMANThread::start(void) {
  thread=CreateThread(NULL, NULL, GMANThreadStart, (void*)this, 0, &threadId);
};


// stop the thread
// return: exit code for the thread returned by
// the 'run' method
int GMANThread::stop(void) {
	DWORD exitCd=0;
	TerminateThread(thread, exitCd);

	return exitCd;
}

  
// suspend the running thread
void GMANThread::suspend(void) {
	SuspendThread(thread);
}

// resume the suspended thread
void GMANThread::resume(void) {
	ResumeThread(thread);
}


// set the priority to one of the predefined priorities
void GMANThread::setPriority(Priority pri) {
	HANDLE accessHdl;
	OpenThreadToken (
		thread,
		THREAD_SET_INFORMATION,
		false,
		&accessHdl);
	switch(pri) {
	case PRIORITY_NORM:
		SetThreadPriority(accessHdl, THREAD_PRIORITY_NORMAL);
		break;
	case PRIORITY_MIN:
		SetThreadPriority(accessHdl, THREAD_PRIORITY_LOWEST);
		break;
	case PRIORITY_MAX:
		SetThreadPriority(accessHdl, THREAD_PRIORITY_HIGHEST);
		break;
	}
	CloseHandle(accessHdl);
}

// return the priority of the currently running thread
GMANThread::Priority GMANThread::getPriority(void) {

	HANDLE accessHdl;
	OpenThreadToken (
		thread,
		THREAD_QUERY_INFORMATION,
		false,
		&accessHdl);

	Priority rc = PRIORITY_NORM;
	switch(GetThreadPriority(accessHdl)) {
	case THREAD_PRIORITY_TIME_CRITICAL:
	case THREAD_PRIORITY_HIGHEST:
	case THREAD_PRIORITY_ABOVE_NORMAL:
		rc=PRIORITY_MAX;
		break;
	case THREAD_PRIORITY_LOWEST: 
	case THREAD_PRIORITY_BELOW_NORMAL:
	case THREAD_PRIORITY_IDLE:
		rc=PRIORITY_MIN;
		break;
	case THREAD_PRIORITY_NORMAL: 
		rc=PRIORITY_NORM;
		break;
	}

	CloseHandle(accessHdl);

	return rc;
}

// wait for the thread to exit,
// returns the exit code for the running thread
int GMANThread::waitForExit(void) {
	// syncronize with the thread
	HANDLE accessHdl;
	OpenThreadToken (
		thread,
		SYNCHRONIZE,
		false,
		&accessHdl);

	DWORD rc = WaitForSingleObject(accessHdl, 0);
	CloseHandle(accessHdl);

	if(rc == WAIT_OBJECT_0) {
		DWORD exitCd;
		GetExitCodeThread(thread, &exitCd);
		return exitCd;
	} 

    // huh, what happened
	return EXIT_FAILURE;
}

DWORD __stdcall GMANThreadStart(void *arg) {

    // in our instance, 
    // arg is a pointer to an object of type GMAN Thread

    GMANThread *threadInst = (GMANThread*)arg;

    return threadInst->run();

}


#else
static void *GMANThreadStart(void *arg);

// default constructor
GMANThread::GMANThread() : UniversalSuperClass() { };


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
	int ret = pthread_getschedparam (thread, &policy, &param);


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

	return (int)exitCd;

}

void *GMANThreadStart(void *arg) {

    // in our instance, 
    // arg is a pointer to an object of type GMAN Thread

    GMANThread *threadInst = (GMANThread*)arg;

    return (void*)threadInst->run();

}

#endif
