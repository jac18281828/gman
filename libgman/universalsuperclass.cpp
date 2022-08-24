/* This is part of the GNU GMAN Library, a FREE implementation of the
 * RenderMan Interface Specification.
 *
 * Copyright (c) 2001, 2000, 19992000  John Cairns 
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

#ifdef WIN32
#include <string.h>
#endif

#include <stdio.h>
#include <stdarg.h>

/* Local Headers */
#include "ri.h"      /* RenderMan Interface */
#include "universalsuperclass.h" /* Declaration Header */
#include "gmanmutex.h"
#include "gmanguard.h"


/*
 * RenderMan API UniversalSuperClass
 *
 */

/* gloabal static data */
UniversalSuperClass::LogLevel
UniversalSuperClass::logLevel = UniversalSuperClass::LOGLVL_INFO;
int UniversalSuperClass::logFileRefCount=0;
FILE *UniversalSuperClass::logFile=NULL;
bool UniversalSuperClass::screenOutput=true;
GMANMutex UniversalSuperClass::logMutex;


// default constructor
UniversalSuperClass::UniversalSuperClass() { 

  // track a refrence count so we know when to close the
  // log file
  GMANGuard guard(logMutex);

  logFileRefCount++;

};


// default destructor 
UniversalSuperClass::~UniversalSuperClass() { 
  GMANGuard guard(logMutex);

  if(--logFileRefCount == 0) {
      if(logFile) {
	  // we need to clean up the log file
	  fclose(logFile);
	  logFile=NULL;
      }
  }
};

// log a message of the specified level to the log
void UniversalSuperClass::log(LogLevel lvl, const char *msg, va_list args) {

  GMANGuard guard(logMutex);

  if(lvl >= logLevel) {
    const char *dispMsg;
	
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
    bool hasEol = (msg[strlen(msg)] == '\n');
    if(logFile) {
      fprintf(logFile, dispMsg);
      vfprintf(logFile, msg, args);
      if(!hasEol) fprintf(logFile, "\n");
    }
    if(screenOutput) {
      vprintf(msg, args);
      if(!hasEol) printf("\n");
    }
  }
}

// log a debug message
void UniversalSuperClass::debug(const char *msg, ...) {
  va_list args;
  va_start(args, msg);
  log(LOGLVL_DEBUG, msg, args);
  va_end(args);
}

// log a info message
void UniversalSuperClass::info (const char *msg, ...) {
  va_list args;
  va_start(args, msg);
  log(LOGLVL_INFO, msg, args);
  va_end(args);
}

// log a warning message
void UniversalSuperClass::warning(const char *msg, ...) {
  va_list args;
  va_start(args, msg);
  log(LOGLVL_WARNING, msg, args);
  va_end(args);
};

// log an error message
void UniversalSuperClass::error(const char *msg, ...) {
  va_list args;
  va_start(args, msg);
  log(LOGLVL_ERROR, msg, args);
  va_end(args);
};

// log a complete disaster
void UniversalSuperClass::disaster(const char *msg, ...) {
  va_list args;
  va_start(args, msg);
  log(LOGLVL_DISASTER, msg, args);
  va_end(args);
};

// set an output file for logging
void UniversalSuperClass::setLogFile(const char *path) {
  GMANGuard guard(logMutex);
  info("Setting log: %s", path);

  if(logFile != NULL) {
    fclose(logFile);
  }
    
  logFile = fopen(path, "a");
};

void UniversalSuperClass::setScreenOutput(bool output) {
  GMANGuard guard(logMutex);
  screenOutput=output;
}

void UniversalSuperClass::setLogLevel(LogLevel lvl) {
    GMANGuard guard(logMutex);
    logLevel = lvl;
}

#ifdef WIN32
bool UniversalSuperClass::operator <(const UniversalSuperClass &usc) const {
  return (strncmp((const char*)this, (const char*)&usc, sizeof(UniversalSuperClass)) < 0);
}

bool UniversalSuperClass::operator ==(const UniversalSuperClass &usc) const {
  return (strncmp((const char*)this, (const char*)&usc, sizeof(UniversalSuperClass)) == 0);
}
#endif

