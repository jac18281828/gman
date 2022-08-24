/*---------------------------------------------------------
  Copyright (C) Lionel Joseph Lacour 2000, 2001
  2000/07/30  First release
  ---------------------------------------------------------
  GMAN Implementation Specific Options & Attributes
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

#include <string>
#if HAVE_STD_NAMESPACE
using std::string;
#endif

#include "gmanisoa.h"

RtVoid GMANISO::set (RtToken name, int n, RtToken *tk, RtPointer *dt)
{
  string a=name;
  if (name=="searchpath") {
    for (int i=0;i<n;i++) {
      a=tk[i];
      if (a=="archive") {
	archive=(char *)dt[i];
	continue;
      }
      if (a=="texture") {
	texture=(char *)dt[i];
	continue;
      }
      if (a=="shader") {
	shader=(char *)dt[i];
	continue;
      }
      if (a=="procedural") {
	procedural=(char *)dt[i];
      }
    }
  }
}

GMANISA::GMANISA()
{}
GMANISA::~GMANISA()
{}
