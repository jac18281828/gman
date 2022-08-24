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

  To contact the author of GNU GMAN, write to John Cairns, 607 E STUART ST, 
  FT COLLINS, CO, 80525, USA, or write via E-mail john@2ad.com.
*/
#ifndef __GMANCONTEXT_H
#define __GMANCONTEXT_H 1

#include <list>
#if HAVE_STD_NAMESPACE
using std::list;
#endif

#include "ri.h"
#include "gmanerror.h"
#include "gmanrenderman.h"

class GMANDLL  GMANContext
{
private:
  list<GMANRenderMan *> chl;
  GMANRenderMan *active;
public:
  GMANContext();

  RtVoid          addContext(RtVoid);
  RtContextHandle getContext(RtVoid);
  GMANRenderMan & current(RtVoid);
  RtVoid          switchTo (RtContextHandle);
  RtVoid          removeCurrent(RtVoid);
};

#endif


