/* This is part of the GNU GMAN Library, a FREE implementation of the
 * RenderMan Interface Specification.
 *
 * Copyright (c) 2001, 2000, 1999 John Cairns 
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
  FT COLLINS, CO, 80525, USA, or write via E-mail john@2ad.com.
*/
 

#ifndef __GMAN_UNIVERSALSUPERCLASS_H
#define __GMAN_UNIVERSALSUPERCLASS_H 1

/* system headers */
#include <stdio.h>
#include <stdarg.h>

#include "ri.h"
#include "gmanmutex.h"

/*
 * RenderMan API UniversalSuperClass
 *
 */

class GMANDLL  UniversalSuperClass {

  public:
    // public types
    

    // supported logging levels
    typedef enum { LOGLVL_DEBUG=0,  // log every damn thing
		   LOGLVL_INFO=1,   // log more than the user needs
		   LOGLVL_WARNING=2, // log possible problems
		   LOGLVL_ERROR=3,   // log errors that have occured
		   LOGLVL_DISASTER=4 // only log critical failures
    } LogLevel;
    
  private:
  // support logging through our universal superclass
    static LogLevel logLevel;
    static int   logFileRefCount;
    static FILE *logFile;
    static bool  screenOutput;
    static GMANMutex logMutex;

  public:
    UniversalSuperClass(); // default constructor
    
    ~UniversalSuperClass(); // default destructor


    // log a debug message
    void debug(const char *msg, ...);

    // log a info message
    void info (const char *msg, ...);

    // log a warning message
    void warning(const char *msg, ...);

    // log an error message
    void error(const char *msg, ...);

    // log a complete disaster
    void disaster(const char *msg, ...);

    // set an output file for logging
    void setLogFile(const char *path);

    // enable or disable screen output for the log

    // set to true to have log message go to the terminal
    // set to false to disable terminal output
    void setScreenOutput(bool output);

    // set the current loging level to lvl
    void setLogLevel(LogLevel lvl);

#ifdef WIN32
	// just about all msvc templates require objects
	// to provide operator less, even if it is not required,
	// i.e. list, etc.
	//
	// provided here to silence the compiler.
	//
	// Bill Gates stop the madness!!!!
  bool operator <(const UniversalSuperClass &usc) const;

  // same here
  bool operator ==(const UniversalSuperClass &usc) const;
#endif

  private:
    // log a message to the logfile or terminal
    void log(LogLevel lvl,
	     const char *msg,
	     va_list    args);
};


#endif

