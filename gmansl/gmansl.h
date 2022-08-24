/* This is part of the GNU GMAN Library, a FREE implementation of the
 * RenderMan Interface Specification.
 *
 * Copyright (c) 1999, John Cairns 
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
 

#ifndef __GMAN_SL_H
#define __GMAN_SL_H 1

/* global headers */
#include <stdio.h>

/* gman headers */
#include "ri.h"

/* global variables */
extern int   lineNumber;
extern FILE  *outHeaderFile, *outSourceFile, *logFile;

/* function prototypes */

RtVoid usage(char *myname);

RtBoolean compile(char *shader);

// create a new header
FILE *initHeader(char *headerName);
// create a new source
FILE *initSource(char *headerName, char *srcName);

// build a shading language source
// into a dynamic loadable module
RtVoid build(char *srcName);

void yyerror(char *s);

#endif
