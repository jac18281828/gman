/* SPDX-License-Identifier: LGPL-2.1-or-later */

/*---------------------------------------------------------
  Copyright (C) Lionel Joseph Lacour 2000, 2001
  2000/07/26  First release
  ---------------------------------------------------------
  A class to store parameter lists.
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

#include "gmanparameterlist.h"

RtVoid GMANParameterList::copy (GMANParameterList const &pl)
{
  counter=pl.counter;
  *counter+=1;
  number=pl.number;
  id=pl.id;
  datas=pl.datas;
  dic=pl.dic;
}

RtVoid GMANParameterList::destroy ()
{
  if (*counter!=0) {
    *counter -=1;
    return;
  }
  for(int i=0;i<number;i++) {
    switch (dic->getType(id[i])) {
    case GMANTokenEntry::STRING:
      delete [] (std::string *)datas[i];
      break;
    case GMANTokenEntry::INTEGER:
      delete [] (RtInt *)datas[i];
      break;
    default:
      delete [] (RtFloat *)datas[i];
      break;
    }
  }
  delete [] datas;
  delete [] id;
  delete counter;
}

GMANParameterList::GMANParameterList ()
{
  counter=new int;
  *counter=0;
  number=0;
  id=0;
  datas=0;
  dic=0;
}

GMANParameterList::GMANParameterList (GMANDictionary &di,
				      RtInt n, RtToken *tk, RtPointer *dt,
				      RtInt vertex, RtInt varying, RtInt uniform,
				      RtInt facevarying)
{
  int i,index;
  int size;
  counter=new int;
  *counter=0;

  dic=&di;
  id=new GMANTokenId[n];
  datas= new RtPointer[n];

  index=0;
  for (i=0; i<n; i++) { // convert token to id
    try {
      id[index]= di.getTokenId (std::string(tk[i]));
    } catch (GMANError &r) {
      GMANHandleError (r);
      continue;
    }

    size =di.allocSize(id[index], vertex, varying, uniform, facevarying);
    switch (di.getType(id[index])) {
    case GMANTokenEntry::STRING:
      datas[index]=(RtPointer)new std::string[size];
      copy_string(size,(char **)dt[i],(std::string *)datas[index]);
      break;
    case GMANTokenEntry::INTEGER:
      datas[index]=(RtPointer)new RtInt[size];
      copy_integer(size,(RtInt *)dt[i],(RtInt *)datas[index]);
      break;
    default:
      datas[index]=(RtPointer) new RtFloat[size];
      copy_float(size,(RtFloat *)dt[i],(RtFloat *)datas[index]);
      break;
    }
    index++;
  }
  number=index;
}

GMANParameterList::GMANParameterList (GMANParameterList const &pl)
{
 copy(pl);
}

GMANParameterList const &GMANParameterList::operator=(GMANParameterList const &pl)
{
  if (this!=&pl) {
    destroy();
    copy(pl);
  }
  return (*this);
}

GMANParameterList::~GMANParameterList()
{
  destroy();
}

// Absence of a token is the routine case: every caller asks "did the user
// pass this optional parameter," not "is this parameter list well-formed."
// Every call site in the tree (RiWorldBegin's fov lookup, and the shader/
// light "tryGet" wrappers) already treats a missing token as NULL and either
// falls back to a spec default or wraps this call in try/catch to force
// that meaning by hand. Returning NULL directly makes that the only meaning
// there is to get.
RtPointer GMANParameterList::getPointer(GMANTokenId tid) const
{
  for(int i=0;i<number;i++) {
    if (tid==id[i]) return datas[i];
  }
  return NULL;
}


RtVoid GMANParameterList::copy_float(RtInt n, RtFloat *source, RtFloat *dest)
{
  for(int i=0;i<n;i++) {
    *dest=*source;
    dest++;
    source++;
  }
}
RtVoid GMANParameterList::copy_integer(RtInt n, RtInt *source, RtInt *dest)
{
  for(int i=0;i<n;i++) {
    *dest=*source;
    dest++;
    source++;
  }
}
RtVoid GMANParameterList::copy_string(RtInt n, char **source, std::string *dest)
{
  for(int i=0;i<n;i++) {
    *dest=std::string(*source);
    dest++;
    source++;
  }
}





















