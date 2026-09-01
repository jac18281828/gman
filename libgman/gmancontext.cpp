/* SPDX-License-Identifier: LGPL-2.1-or-later */

/*----------------------------------------------------------
  Copyright (C) Lionel Joseph Lacour 2000, 2001
  2000/08/06  First release
  ----------------------------------------------------------
  This class implements the RiContext feature of the
  RiSpec V3.2
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
*/
#include "gmancontext.h"
#include "gmanrendermanimpl.h"

GMANContext::GMANContext()
{
  active=(GMANRenderMan *)RI_NULL;
}

RtVoid  GMANContext::addContext()
{
  active=new GMANRenderManImpl;
  chl.push_back(active);
}

RtContextHandle GMANContext::getContext()
{
  return (RtContextHandle) active;
}

GMANRenderMan & GMANContext::current()
{
  if (active==((GMANRenderMan *) RI_NULL)) {
    GMANError error(RIE_NOTSTARTED, RIE_SEVERE, "GMANContext: No active context");
    throw error;
  }
  return *active;
}
RtVoid GMANContext::switchTo(RtContextHandle ch)
{
  std::list<GMANRenderMan *>::iterator first=chl.begin();
  std::list<GMANRenderMan *>::iterator last=chl.end();
  GMANRenderMan *r=(GMANRenderMan *)ch;
  for (;first!=last;first++) {
    if (*first==r) { 
      active=r;
      return;
    }
  }
  GMANError error(RIE_NESTING,RIE_SEVERE,"GMANContext: invalid Context Handle");
  throw(error);
}
RtVoid GMANContext::removeCurrent(RtVoid)
{
  std::list<GMANRenderMan *>::iterator first=chl.begin();
  std::list<GMANRenderMan *>::iterator last=chl.end();
  for (;first!=last;first++) {
    if (*first==active) {
      delete *first;
      chl.erase(first);
      active = (GMANRenderMan *)RI_NULL;
      return;
    }
  } 
}











