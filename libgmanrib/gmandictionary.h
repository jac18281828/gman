/*----------------------------------------------------------
  Copyright (C) Lionel Joseph Lacour 2000, 2001
  2000/07/09  First release
  ----------------------------------------------------------
  This Dictionary can handle inline declaration as described in the 
  RenderMan Spec v3.2
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
#ifndef __GMANDICTIONARY_H
#define __GMANDICTIONARY_H 1

#include <vector>
#include <string>
#if HAVE_STD_NAMESPACE
using std::string;
using std::vector;
#endif

#include "ri.h"
#include "gmantypes.h"

typedef GMANUInt GMANTokenId;

class GMANDLL  GMANTokenEntry 
{
public:
 enum TokenClass { CONSTANT, UNIFORM, VARYING, VERTEX };
 enum TokenType { FLOAT, POINT, VECTOR, NORMAL, COLOR, STRING, MATRIX, HPOINT, INTEGER };

private:
  string     name;
  TokenClass tclass:4;
  TokenType  ttype:6;
  bool       in_line:1;
  RtInt      quantity;

public:
  GMANTokenEntry (string n, TokenClass tc, TokenType tt, RtInt qnt=1, bool inln=false);

  const string getName () { return name; }
  TokenClass   getClass () { return tclass; }
  TokenType    getType () { return ttype; }
  RtInt        getQuantity () { return quantity; }
  bool         isInline () { return in_line; } 
#ifdef DEBUG
  RtVoid printClassType ();
#endif
};



class GMANDictionary
{
private:
  vector<GMANTokenEntry> te;
public:
  GMANDictionary();

  GMANTokenId addToken (string n, GMANTokenEntry::TokenClass tc, GMANTokenEntry::TokenType tt, RtInt qnt=1, bool inln=false);
  GMANTokenId getTokenId (string n);
#ifdef PRE
  RtVoid        isValid     (GMANTokenId id);
#endif
  GMANTokenEntry::TokenClass  getClass    (GMANTokenId id);
  GMANTokenEntry::TokenType   getType     (GMANTokenId id);
  RtInt       allocSize   (GMANTokenId id, RtInt vertex, RtInt varying, RtInt uniform);
  RtInt       getTypeSize (GMANTokenEntry::TokenType);
  RtInt       getQuantity (GMANTokenId id);
#ifdef DEBUG
  RtVoid        stats (RtVoid);
#endif
};

#endif










