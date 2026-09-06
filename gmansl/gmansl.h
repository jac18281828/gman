/* SPDX-License-Identifier: LGPL-2.1-or-later */

/* This is part of GMAN, a RenderMan-compatible renderer.
 *
 * Copyright (c) 1999, John Cairns 
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
