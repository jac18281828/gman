/* This is part of the GNU GMAN Library, a FREE implementation of the
 * RenderMan Interface Specification.
 *
 * Copyright (c) 2001, 2000, 1999  John Cairns 
 *
 * Author: John Cairns <john@2ad.com>
 */

/* Added GMANHandleError and default Error managers.
 * Lionel Joseph Lacour -- 2000/07/19 
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
#include <iostream>
#if HAVE_STD_NAMESPACE
using std::cout;
using std::endl;
#endif

#include "gmanerror.h"

// Global variable
RtErrorHandler GMANErrorHandler=(*RiErrorPrint);
RtInt RiLastError;

static RtVoid print (RtInt code, RtInt severity, const char *msg)
{
  string cd;
  string sev;

  switch(code) {
  case RIE_NOERROR: cd= "RIE_NOERROR ";
    break;
  case RIE_NOMEM: cd= "RIE_NOMEM ";
    break;
  case RIE_SYSTEM: cd= "RIE_SYSTEM ";    
    break;  
  case RIE_NOFILE: cd= "RIE_NOFILE ";     
    break; 
  case RIE_BADFILE: cd= "RIE_BADFILE ";
    break;
  case RIE_VERSION: cd= "RIE_VERSION ";  
    break;
  case RIE_DISKFULL: cd= "RIE_DISKFULL ";  
    break;
  case RIE_INCAPABLE: cd= "RIE_INCAPABLE ";
    break;
  case RIE_UNIMPLEMENT: cd= "RIE_UNIMPLEMENT ";
    break;
  case RIE_LIMIT: cd= "RIE_LIMIT ";
    break;
  case RIE_BUG: cd= "RIE_BUG ";
    break;
  case RIE_NOTSTARTED: cd= "RIE_NOTSTARTED ";
    break;
  case RIE_NESTING: cd= "RIE_NESTING ";
    break;
  case RIE_NOTOPTIONS: cd= "RIE_NOTOPTIONS ";
    break;
  case RIE_NOTATTRIBS: cd= "RIE_NOTATTRIBS ";
    break;
  case RIE_NOTPRIMS: cd= "RIE_NOTPRIMS ";
    break;
  case RIE_ILLSTATE: cd= "RIE_ILLSTATE ";
    break;
  case RIE_BADMOTION: cd= "RIE_BADMOTION ";
    break;
  case RIE_BADSOLID: cd= "RIE_BADSOLID ";
    break;
  case RIE_BADTOKEN: cd= "RIE_BADTOKEN ";
    break;
  case RIE_RANGE: cd= "RIE_RANGE ";
    break;
  case RIE_CONSISTENCY: cd= "RIE_CONSISTENCY ";
    break;
  case RIE_BADHANDLE: cd= "RIE_BADHANDLE ";
    break;
  case RIE_NOSHADER: cd= "RIE_NOSHADER ";
    break;
  case RIE_MISSINGDATA: cd= "RIE_MISSINGDATA ";
    break;
  case RIE_SYNTAX: cd= "RIE_SYNTAX ";
    break;
  case RIE_MATH: cd= "RIE_MATH ";
    break;
  }
  switch (severity) {
  case RIE_INFO: sev="INFO: ";
    break;
  case RIE_WARNING: sev="WARNING: ";
    break;
  case RIE_ERROR: sev="ERROR: ";
    break;
  case RIE_SEVERE: sev="SEVERE: ";
    break;
  }
  cout << sev << cd << "-- " << msg  << endl;
}

#ifdef __cplusplus
extern "C" {
#endif
  // Standard Error Handler
  RtVoid RiErrorIgnore( RtInt cd, RtInt sev, const char *msg)
  {
    RiLastError=cd;
    return;
  }
  RtVoid RiErrorPrint( RtInt cd, RtInt sev, const char *msg)
  {
    RiLastError=cd;
    print (cd,sev,msg);
    return;
  }
  RtVoid RiErrorAbort( RtInt cd, RtInt sev, const char *msg)
  {
    RiLastError=cd;
    print (cd,sev,msg);
    abort();
  }
#ifdef __cplusplus
}
#endif


GMANError::GMANError()
{ 
  code = RIE_NOERROR;
  severity = RIE_INFO;
}

GMANError::GMANError (RtInt cd, RtInt sev, const char *msg)
{
  code=cd;
  severity=sev;
  message=string(msg);
}

RtVoid GMANError::set (RtInt cd, RtInt sev, const char *msg)
{
  code=cd;
  severity=sev;
  message=string(msg);
}

RtVoid GMANHandleError (GMANError &r)
{
  (*GMANErrorHandler) (r.getCode(), r.getSeverity(), r.getMessage());
}

//  //==============================================================
//  int main()
//  {
//    GMANError er(RIE_NOMEM,RIE_SEVERE,"Virtual memory exhausted");
//
//    GMANErrorHandler=(*RiErrorPrint);
//    try {
//      throw er;
//    } catch (GMANError &r) {
//      GMANHandleError (r);
//    }
//    return 0;
//  }












