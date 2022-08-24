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
  FT COLLINS, CO, 80525, USA, or write via E-mail john@2ad.com.
*/
#ifndef __GMANERROR_H
#define __GMANERROR_H 1

#include <iostream>
#include <string>
#if HAVE_STD_NAMESPACE
using std::string;
#endif

#include "ri.h"


class GMANDLL  GMANError
{
private:
  RtInt code;
  RtInt severity;
  std::string message;
public:
  GMANError ();
  GMANError (RtInt cd, RtInt sev, const char *msg);

  RtVoid set (RtInt cd, RtInt sev, const char *msg);
  RtVoid setCode (RtInt cd) { code = cd; }
  RtVoid setSeverity (RtInt sev) { severity = sev; }
  RtVoid setMessage (const char *msg) { message = string(msg); }

  RtInt  getCode (RtVoid) const { return code; }
  RtInt  getSeverity (RtVoid) const { return severity; }
  const char *getMessage (RtVoid) const{ return message.c_str(); }
};

extern RtVoid GMANDLL  GMANHandleError (GMANError &);

extern RtErrorHandler GMANDLL  GMANErrorHandler;

#endif















