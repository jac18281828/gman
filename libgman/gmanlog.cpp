/* SPDX-License-Identifier: LGPL-2.1-or-later */

/* This is part of GMAN, a RenderMan-compatible renderer.
 *
 * Copyright (c) 2001, 2000, 1999  John Cairns
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

/* System Headers */
#include <cstdio>

/* Local Headers */
#include "ri.h"      /* RenderMan Interface */
#include "gmanlog.h" /* Declaration Header */
#include "gmanmutex.h"
#include "gmanguard.h"

// FIXME FIXME FIXME
// set this with autoconf macros
static const char *softwareVersion = "1.0.0 Alpha";

/* log state */
static GMANLogLevel logLevel = LOGLVL_INFO;
static FILE *logFile = NULL;
static bool  screenOutput = true;
static GMANMutex logMutex;

bool logEnabled(GMANLogLevel lvl) {
  GMANGuard guard(logMutex);
  return lvl >= logLevel;
}

void logWrite(GMANLogLevel lvl, std::string_view message) {
  GMANGuard guard(logMutex);

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
  bool hasEol = !message.empty() && message.back() == '\n';
  if(logFile) {
    std::fprintf(logFile, "%s", dispMsg);
    std::fwrite(message.data(), 1, message.size(), logFile);
    if(!hasEol) std::fputc('\n', logFile);
  }
  if(screenOutput) {
    std::fwrite(message.data(), 1, message.size(), stdout);
    if(!hasEol) std::fputc('\n', stdout);
  }
}

// set an output file for logging
void setLogFile(const char *path) {
  GMANGuard guard(logMutex);
  info("Setting log: {}", path);

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
    info("GMAN {}\n\n",
	 "This library is free software; you can redistribute it and/or\n"
	 "modify it under the terms of the GNU Lesser General Public\n"
	 "License as published by the Free Software Foundation; either\n"
	 "version 2.1 of the License, or (at your option) any later version.\n"
	 "Copyright (c) 2002, 2001, 2000, 1999  John Cairns <john@2ad.com>.\n\n",
	 "The RenderMan interface is copyright Pixar (c) 1987, 1988, 1989, 1995.\n\n",
	 softwareVersion);
}
