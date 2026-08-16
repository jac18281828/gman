/* This is part of GMAN, a RenderMan-compatible renderer.
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

  To contact the author of GMAN, write to John Cairns, 607 E STUART ST,
  FT COLLINS, CO, 80525, USA, or write via E-mail john@2ad.com.
*/


#ifndef __GMAN_GMANLOG_H
#define __GMAN_GMANLOG_H 1


/* Headers */

// the renderman interface
#include "ri.h"

/*
 * GMAN logging
 *
 * These were members of UniversalSuperClass, which every class in the tree
 * inherited under doc/codingguide.txt rule 2, "to support the (future)
 * addition of memory management". The memory management never arrived, so the
 * base class is retired and the logging it actually carried lives here as
 * free functions. Call sites are unchanged: an unqualified debug("...") that
 * used to find the inherited member now finds the function.
 */

// supported logging levels
typedef enum { LOGLVL_DEBUG=0,   // log every damn thing
	       LOGLVL_INFO=1,    // log more than the user needs
	       LOGLVL_WARNING=2, // log possible problems
	       LOGLVL_ERROR=3,   // log errors that have occured
	       LOGLVL_DISASTER=4 // only log critical failures
} GMANLogLevel;

// log a debug message
GMAN_EXPORT void debug(const char *msg, ...);

// log a info message
GMAN_EXPORT void info(const char *msg, ...);

// log a warning message
GMAN_EXPORT void warning(const char *msg, ...);

// log an error message
GMAN_EXPORT void error(const char *msg, ...);

// log a complete disaster
GMAN_EXPORT void disaster(const char *msg, ...);

// set an output file for logging
GMAN_EXPORT void setLogFile(const char *path);

// set to true to have log messages go to the terminal
GMAN_EXPORT void setScreenOutput(bool output);

// set the current logging level to lvl
GMAN_EXPORT void setLogLevel(GMANLogLevel lvl);


/*
 * RenderMan API GMANLog
 *
 */

class GMAN_EXPORT  GMANLog {
public:
  GMANLog(); // default constructor

  ~GMANLog(); // default destructor

  // invoke a copyright message laying claim for this
  // software to it's lawful heirs :)
  RtVoid copyright(RtVoid);

  RtVoid setLogLevel(GMANLogLevel lvl) { ::setLogLevel(lvl); }

  RtVoid setLogFile(const char *path) { ::setLogFile(path); }

  RtVoid setScreenOutput(bool output) { ::setScreenOutput(output); }
};


#endif
