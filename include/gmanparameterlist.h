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

#ifndef __GMANPARAMETERLIST_H
#define __GMANPARAMETERLIST_H 1

#include <string>
#include "ri.h"
#include "universalsuperclass.h"
#include "gmandictionary.h"
#include "gmanerror.h"


class GMANDLL  GMANParameterList : public UniversalSuperClass
{
private:
  RtInt *counter;
  
  RtInt  number;
  GMANTokenId *id;
  RtPointer *datas;
  GMANDictionary *dic;

  RtVoid copy_float(RtInt number, RtFloat *source, RtFloat *dest);
  RtVoid copy_integer(RtInt number, RtInt *source, RtInt *dest);
  RtVoid copy_string(RtInt number, char **source, string *dest);
  RtVoid copy (GMANParameterList const &pl);
  RtVoid destroy ();
public:
  GMANParameterList ();
  GMANParameterList (GMANDictionary &di,
		     RtInt n, RtToken *tk, RtPointer *dt,
		     RtInt vertex=1, RtInt varying=1, RtInt uniform=1);
  GMANParameterList (GMANParameterList const &pl);
  GMANParameterList const &operator=(GMANParameterList const &pl);
  ~GMANParameterList ();

  const RtPointer getPointer(GMANTokenId tid) const;
};

#endif








