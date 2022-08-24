/* This is part of the GNU GMAN Library, a FREE implementation of the
 * RenderMan Interface Specification.
 *
 * Copyright (c) 2002, 2001, 2000, 1999  John Cairns 
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

/* System Headers */
#ifdef WIN32
#include <windows.h>
#endif

/* Local Headers */
#include "ri.h"      /* RenderMan Interface */
#include "universalsuperclass.h" /* Super class */
#include "gmanlog.h" /* Declaration Header */

// FIXME FIXME FIXME
// set this with autoconf macros
static const char *softwareVersion = "1.0.0 Alpha";

/*
 * RenderMan API GMANLog
 *
 */

// default constructor
GMANLog::GMANLog() : UniversalSuperClass() { };


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

