/*--------------------------------------------------------------------
  Copyright (C) Lionel Joseph Lacour 2000, 2001
  2000/07/21  First release
  --------------------------------------------------------------------
  A class to parse RiDeclare and inline parameter list definitions.
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
#include "gmaninlineparse.h"

RtVoid GMANInlineParse::check_syntax ()
{
  // number_of_words =0 ---> ERROR
  // number_of_words =1 ---> not an inline def
  // number_of_words =2 ---> type id
  // number_of_words =3 ---> class type id
  // number_of_words =4 ---> ERROR
  // number_of_words =5 ---> type [ size ] id
  // number_of_words =6 ---> class type [ size ] id
  // number_of_words =7 ---> ERROR
  GMANError error( RIE_SYNTAX, RIE_ERROR, "GMANInlineParse: BAD_SYNTAX" );

  switch (number_of_words) {
  case 0:
    error.setMessage("GMANInlineParse: RTVOID_STRING");
    throw error;
  case 4:
  case 7: 
    throw error;
  case 1: inline_def=false;
    break;
  case 2:
    lc(word[0]);
    if (is_type(word[0])==false) {
      throw error;
    }
    inline_def=true;
    tc=GMANTokenEntry::UNIFORM;
    tt=get_type(word[0]);
    size=1;
    identifier=word[1];
    break;
  case 3:
    lc(word[0]);
    lc(word[1]);
    if ((is_class(word[0])==false) || (is_type(word[1])==false)) {
      throw error;
    }
    inline_def=true;
    tc=get_class(word[0]);
    tt=get_type(word[1]);
    size=1;
    identifier=word[2];
    break;
  case 5:
    lc(word[0]);
    if ((is_type(word[0])==false) || (word[1]!="[") ||
	(is_int(word[2])==false) || (word[3]!="]")) {
      throw error;
    }
    inline_def=true;
    tc=GMANTokenEntry::UNIFORM;
    tt=get_type(word[0]);
    size=get_size(word[2]);
    identifier=word[4];
    break;
  case 6:
    lc(word[0]);
    lc(word[1]);
    if ((is_class(word[0])==false) || (is_type(word[1])==false) ||
	 (word[2]!="[") || (is_int(word[3])==false) ||
	 (word[4]!="]")) {
      throw error;
    }
    inline_def=true;
    tc=get_class(word[0]);
    tt=get_type(word[1]);
    size=get_size(word[3]);
    identifier=word[5];
    break;
  }
}

RtVoid GMANInlineParse::parse (string str)
{
  GMANError error( RIE_SYNTAX, RIE_ERROR, "GMANInlineParse: BAD_SYNTAX" );
  uint i,j;
  size_t sp;
  size_t sz;
  bool start_found;

  sp=0;
  sz=1;
  j=0;
  start_found=false;

  for (i=0;(i<str.length())&&(j<7);i++) {
    switch (str[i]) {
    case ' ':
    case '\t':
    case '\n':
      if (start_found==true) {
	word[j]=str.substr(sp,sz);
	j++;	
	sz=1;	
	}
      start_found=false;
      break;
    case '#':
      error.setMessage("GMANInlineParse: '#' character not allowed in strings");
      throw error;
    case '\"':
      error.setMessage("GMANInlineParse: '\"' character not allowed in strings");
      throw error;
    case '[':
    case ']':
      if (start_found==true) {
	word[j]=str.substr(sp,sz);
	j++;
	start_found=false;
      }
      sp=i;
      sz=1;
      word[j]=str.substr(sp,sz);
      j++;
      break;
    default:
      if (start_found==true) {
	sz+=1;
	break;
      }
      start_found=true;
      sp=i;
      sz=1;
    }
  }
  // if there is no space at the end of the string,
  // the previous loop will not notice the end of the word,
  // and so will 'forget' to store it.
  if (start_found==true) {
    word[j]=str.substr(sp,sz);
    j++;
  }
  number_of_words=j;
  check_syntax ();
}

bool GMANInlineParse::is_class (string str)
{
  if ((str=="constant") ||
      (str=="uniform") ||
      (str=="varying") ||
      (str=="vertex"))
    return true;
  return false;
}

bool GMANInlineParse::is_type (string str)
{
  if ((str=="float") ||
      (str=="point") ||
      (str=="vector") ||
      (str=="normal") ||
      (str=="color") ||
      (str=="string") ||
      (str=="matrix") ||
      (str=="hpoint") ||
      (str=="integer"))
    return true;
  return false;
}

// check if this int is >0 too
bool GMANInlineParse::is_int (string str)
{
  int i,j;
  i=sscanf(str.c_str(),"%d",&j);
  if ((i!=1) || (j<=0)) return false;
  return true;
}

GMANTokenEntry::TokenClass GMANInlineParse::get_class (string str)
{
  if (str=="constant") return GMANTokenEntry::CONSTANT;
  if (str=="uniform") return GMANTokenEntry::UNIFORM;
  if (str=="varying") return GMANTokenEntry::VARYING;
  if (str=="vertex") return GMANTokenEntry::VERTEX;
}

GMANTokenEntry::TokenType GMANInlineParse::get_type (string str)
{
  if (str=="float") return GMANTokenEntry::FLOAT;
  if (str=="point") return GMANTokenEntry::POINT;
  if (str=="vector") return GMANTokenEntry::VECTOR;
  if (str=="normal") return GMANTokenEntry::NORMAL;
  if (str=="color") return GMANTokenEntry::COLOR;
  if (str=="string") return GMANTokenEntry::STRING;
  if (str=="matrix") return GMANTokenEntry::MATRIX;
  if (str=="hpoint") return GMANTokenEntry::HPOINT;
  if (str=="integer") return GMANTokenEntry::INTEGER;
}

int  GMANInlineParse::get_size (string str)
{
  int i;
  sscanf(str.c_str(),"%d",&i);
  return i;
}

RtVoid GMANInlineParse::lc(string &str)
{
  for(uint i=0;i<str.length();i++) {
    str[i]=tolower(str[i]);
  }
}


