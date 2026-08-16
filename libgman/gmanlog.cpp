/* SPDX-License-Identifier: LGPL-2.1-or-later */

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

/* System Headers */
#include <cstdarg>
#include <cstdio>
#include <cstring>

/* Local Headers */
#include "ri.h"      /* RenderMan Interface */
#include "gmanlog.h" /* Declaration Header */
#include "gmanmutex.h"
#include "gmanguard.h"

// FIXME FIXME FIXME
// set this with autoconf macros
static const char *softwareVersion = "1.0.0 Alpha";

/* log state, formerly the static members of UniversalSuperClass */
static GMANLogLevel logLevel = LOGLVL_INFO;
static FILE *logFile = NULL;
static bool  screenOutput = true;
static GMANMutex logMutex;

// log a message of the specified level to the log
static void logMessage(GMANLogLevel lvl, const char *msg, va_list args) {

  GMANGuard guard(logMutex);

  if(lvl >= logLevel) {
    const char *dispMsg = "";

    switch(lvl) {
    case LOGLVL_DEBUG:
      dispMsg = "GMAN DEBUG: ";
      break;
    case LOGLVL_INFO:
      dispMsg = "GMAN INFO: ";
      break;
    case LOGLVL_WARNING:
      dispMsg = "GMAN WARNING: ";
      break;
    case LOGLVL_ERROR:
      dispMsg = "GMAN ERROR: ";
      break;
    case LOGLVL_DISASTER:
      dispMsg = "GMAN DISASTER: ";
      break;
    }
    bool hasEol = (msg[std::strlen(msg)] == '\n');
    if(logFile) {
      std::fprintf(logFile, "%s", dispMsg);
      std::vfprintf(logFile, msg, args);
      if(!hasEol) std::fprintf(logFile, "\n");
    }
    if(screenOutput) {
      std::vprintf(msg, args);
      if(!hasEol) std::printf("\n");
    }
  }
}

// log a debug message
void debug(const char *msg, ...) {
  va_list args;
  va_start(args, msg);
  logMessage(LOGLVL_DEBUG, msg, args);
  va_end(args);
}

// log a info message
void info(const char *msg, ...) {
  va_list args;
  va_start(args, msg);
  logMessage(LOGLVL_INFO, msg, args);
  va_end(args);
}

// log a warning message
void warning(const char *msg, ...) {
  va_list args;
  va_start(args, msg);
  logMessage(LOGLVL_WARNING, msg, args);
  va_end(args);
}

// log an error message
void error(const char *msg, ...) {
  va_list args;
  va_start(args, msg);
  logMessage(LOGLVL_ERROR, msg, args);
  va_end(args);
}

// log a complete disaster
void disaster(const char *msg, ...) {
  va_list args;
  va_start(args, msg);
  logMessage(LOGLVL_DISASTER, msg, args);
  va_end(args);
}

// set an output file for logging
void setLogFile(const char *path) {
  GMANGuard guard(logMutex);
  info("Setting log: %s", path);

  if(logFile != NULL) {
    std::fclose(logFile);
  }

  logFile = std::fopen(path, "a");
}

void setScreenOutput(bool output) {
  GMANGuard guard(logMutex);
  screenOutput = output;
}

void setLogLevel(GMANLogLevel lvl) {
  GMANGuard guard(logMutex);
  logLevel = lvl;
}


/*
 * RenderMan API GMANLog
 *
 */

// default constructor
GMANLog::GMANLog() { };


// default destructor
GMANLog::~GMANLog() { };

RtVoid GMANLog::copyright(RtVoid) {
    info("GMAN %s\n\n",
	 "This library is free software; you can redistribute it and/or\n"
	 "modify it under the terms of the GNU Library General Public\n"
	 "License as published by the Free Software Foundation; either\n"
	 "version 2 of the License, or (at your option) any later version.\n"
	 "Copyright (c) 2002, 2001, 2000, 1999  John Cairns <john@2ad.com>.\n\n",
	 "The RenderMan interface is copyright Pixar (c) 1987, 1988, 1989, 1995.\n\n",
	 softwareVersion);
}
