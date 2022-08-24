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
#ifdef DEBUG
#include <iostream.h>
#include <iomanip.h>
#endif
#include "gmandictionary.h"
#include "gmaninlineparse.h"
#include "gmanerror.h"

GMANTokenEntry::GMANTokenEntry(string n, TokenClass tc, TokenType tt, RtInt qnt, bool inln) 
{  
  name = n;
  tclass = tc;
  ttype = tt;
  in_line = inln;
  quantity = qnt;
}

GMANTokenEntry::GMANTokenEntry() {

}


GMANTokenEntry::GMANTokenEntry(const GMANTokenEntry &ent) {
	*this = ent;
}

const GMANTokenEntry &GMANTokenEntry::operator=(const GMANTokenEntry &ent) {

	name = ent.getName();
	tclass = ent.getClass();
	ttype = ent.getType();
	in_line = ent.isInline();
	quantity = ent.getQuantity();

	return *this;
}


#ifdef DEBUG
RtVoid GMANTokenEntry::printClassType ( RtVoid )
{
  cout << setw(9);
  switch (tclass) {
  case GMANTokenEntry::CONSTANT: cout << "CONSTANT";
    break;
  case GMANTokenEntry::UNIFORM: cout << "UNIFORM";
    break;
  case GMANTokenEntry::VARYING: cout << "VARYING";
    break;
  case GMANTokenEntry::VERTEX: cout << "VERTEX";
  }
  cout << setw(8);
  switch (ttype) {
  case GMANTokenEntry::HPOINT: cout << "HPOINT";
    break;
  case GMANTokenEntry::MATRIX: cout << "MATRIX";
    break;
  case GMANTokenEntry::NORMAL: cout << "NORMAL";
    break;
  case GMANTokenEntry::VECTOR: cout << "VECTOR";
    break;
  case GMANTokenEntry::FLOAT: cout << "FLOAT";
    break;
  case GMANTokenEntry::INTEGER: cout << "INTEGER";
    break;
  case GMANTokenEntry::STRING: cout << "STRING";
    break;
  case GMANTokenEntry::POINT: cout << "POINT";
    break;
  case GMANTokenEntry::COLOR: cout << "COLOR";
  }
}
#endif 
/**========CLASS Token_entry END========**/




GMANDictionary::GMANDictionary()
{
  //=== Standard Geometric Primitive Variables ===
  addToken (RI_P, GMANTokenEntry::VERTEX, GMANTokenEntry::POINT);
  addToken (RI_PZ, GMANTokenEntry::VERTEX, GMANTokenEntry::FLOAT);
  addToken (RI_PW, GMANTokenEntry::VERTEX, GMANTokenEntry::HPOINT);
  addToken (RI_N, GMANTokenEntry::VARYING, GMANTokenEntry::NORMAL);
  addToken (RI_NP, GMANTokenEntry::UNIFORM, GMANTokenEntry::NORMAL);
  addToken (RI_CS, GMANTokenEntry::VARYING, GMANTokenEntry::COLOR);
  addToken (RI_OS, GMANTokenEntry::VARYING, GMANTokenEntry::COLOR);
  addToken (RI_S, GMANTokenEntry::VARYING, GMANTokenEntry::FLOAT);
  addToken (RI_T, GMANTokenEntry::VARYING, GMANTokenEntry::FLOAT);
  addToken (RI_ST, GMANTokenEntry::VARYING, GMANTokenEntry::FLOAT, 2);

  //=== Standard Light Source Shader Parameters ===
  addToken (RI_INTENSITY, GMANTokenEntry::CONSTANT, GMANTokenEntry::FLOAT);
  addToken (RI_LIGHTCOLOR, GMANTokenEntry::CONSTANT, GMANTokenEntry::COLOR);
  addToken (RI_FROM, GMANTokenEntry::CONSTANT, GMANTokenEntry::POINT);
  addToken (RI_TO, GMANTokenEntry::CONSTANT, GMANTokenEntry::POINT);
  addToken (RI_CONEANGLE, GMANTokenEntry::CONSTANT, GMANTokenEntry::FLOAT);
  addToken (RI_CONEDELTAANGLE, GMANTokenEntry::CONSTANT, GMANTokenEntry::FLOAT);
  addToken (RI_BEAMDISTRIBUTION, GMANTokenEntry::CONSTANT, GMANTokenEntry::FLOAT);

  //=== Standard Surface Shader Parameters ===
  addToken (RI_KA, GMANTokenEntry::CONSTANT, GMANTokenEntry::FLOAT);
  addToken (RI_KD, GMANTokenEntry::CONSTANT, GMANTokenEntry::FLOAT);
  addToken (RI_KS, GMANTokenEntry::CONSTANT, GMANTokenEntry::FLOAT);
  addToken (RI_KR, GMANTokenEntry::CONSTANT, GMANTokenEntry::FLOAT);
  addToken (RI_ROUGHNESS, GMANTokenEntry::CONSTANT, GMANTokenEntry::FLOAT);
  addToken (RI_SPECULARCOLOR, GMANTokenEntry::CONSTANT, GMANTokenEntry::COLOR);
  addToken (RI_TEXTURENAME, GMANTokenEntry::CONSTANT, GMANTokenEntry::STRING);

  //=== Standard Volume Shader Parameters ===
  addToken (RI_MINDISTANCE, GMANTokenEntry::CONSTANT, GMANTokenEntry::FLOAT);
  addToken (RI_MAXDISTANCE, GMANTokenEntry::CONSTANT, GMANTokenEntry::FLOAT);
  addToken (RI_BACKGROUND, GMANTokenEntry::CONSTANT, GMANTokenEntry::COLOR);
  addToken (RI_DISTANCE, GMANTokenEntry::CONSTANT, GMANTokenEntry::FLOAT);

  //=== Standard Displacement Shader Parameter ===
  addToken (RI_AMPLITUDE, GMANTokenEntry::CONSTANT, GMANTokenEntry::FLOAT);

  addToken (RI_FOV, GMANTokenEntry::CONSTANT, GMANTokenEntry::FLOAT);
  addToken (RI_ORIGIN, GMANTokenEntry::CONSTANT, GMANTokenEntry::INTEGER, 2);
  addToken (RI_WIDTH, GMANTokenEntry::VARYING, GMANTokenEntry::FLOAT);
  addToken (RI_CONSTANTWIDTH, GMANTokenEntry::CONSTANT, GMANTokenEntry::FLOAT);
}


// If the token already exists, addToken return the corresponding id 
GMANTokenId GMANDictionary::addToken (string n, GMANTokenEntry::TokenClass tc, GMANTokenEntry::TokenType tt, RtInt qnt, bool inln)
{
  vector<GMANTokenEntry>::iterator first=te.begin();
  vector<GMANTokenEntry>::iterator last=te.end();
  GMANTokenId i;

  GMANTokenEntry tmp(n,tc,tt,qnt,inln);

  for(i=1;first!=last;first++,i++)
    {
      if ( tmp == *first ) {
	if (inln==false) (*first).inlineOff();
		return i;
      }
    }

  
  te.push_back(tmp);
  return i;
}

// If getTokenId find that the token given as input is in fact an inline definition,
// it will add it into the dictionary.
// Be aware that once registered inline def can only be accessed with the returned id.
// Example:
//   getTokenId ("uniform float[4] item")-> id 24
//   getClass (24)-> UNIFORM
//   getTokenId ("item")-> an error is thrown 
//   (except if item was previously defined with RiDeclare)
GMANTokenId GMANDictionary::getTokenId (string n)
{
  GMANInlineParse ip;
  GMANError error(RIE_BADTOKEN, RIE_ERROR,"GMANDictionary: TOKEN_NOT_FOUND");
  GMANTokenId i,j=0;
 
  ip.parse(n);
  if (ip.isInline()==true) {
    j=addToken (ip.getIdentifier(), ip.getClass(), ip.getType(), ip.getQuantity(), true);
  } else {
    vector<GMANTokenEntry>::iterator first=te.begin();
    vector<GMANTokenEntry>::iterator last=te.end();

    for(i=1;first!=last;first++,i++)
      {
	if ((n==(first->getName())) && ((first->isInline())==false))
	  j=i;
      }
    if (j==0) throw error;
  }
  return j;
}
#ifdef PRE
RtVoid GMANDictionary::isValid (GMANTokenId id)
{
  GMANError error(RIE_MISSINGDATA, RIE_ERROR, "GMANDictionary: ID_OUT_OF_RANGE");
  if (id>te.size()) throw error;
  if (id==0) {
    error.setMessage("GMANDictionary: ID=0");
    throw error;
  }
}
#endif
GMANTokenEntry::TokenClass GMANDictionary::getClass (GMANTokenId id)
{
#ifdef PRE
  is_valid (id);
#endif
  vector<GMANTokenEntry>::iterator first=te.begin();
  return ((first+id-1)->getClass());
}
GMANTokenEntry::TokenType GMANDictionary::getType (GMANTokenId id)
{
#ifdef PRE
  is_valid (id);
#endif
  vector<GMANTokenEntry>::iterator first=te.begin();
  return ((first+id-1)->getType());
}
int GMANDictionary::allocSize (GMANTokenId id, RtInt vertex, RtInt varying, RtInt uniform)
{
  int size;
  vector<GMANTokenEntry>::iterator first=te.begin();
  first+=id-1;
  size= getTypeSize(first->getType());
  switch (first->getClass()) {
  case GMANTokenEntry::VERTEX: size*= vertex;
    break;
  case GMANTokenEntry::VARYING: size*= varying;
    break;
  case GMANTokenEntry::UNIFORM: size*= uniform;
    break;
  case GMANTokenEntry::CONSTANT:
    break;
  }
  size *= (first->getQuantity());
  return size;
}
int GMANDictionary::getTypeSize (GMANTokenEntry::TokenType t)
{
  switch (t) {
  case GMANTokenEntry::FLOAT: return 1;
  case GMANTokenEntry::POINT: return 3;
  case GMANTokenEntry::VECTOR: return 3;
  case GMANTokenEntry::NORMAL: return 3;
  case GMANTokenEntry::COLOR: return NCOMPS;
  case GMANTokenEntry::STRING: return 1;
  case GMANTokenEntry::MATRIX: return 16;
  case GMANTokenEntry::HPOINT: return 4;
  case GMANTokenEntry::INTEGER: return 1;
  default : 
    GMANError error(RIE_WARNING, RIE_BUG, "Unknown token type"); 
    throw(error);
  }
  // appease mswin
  return 1;
}
int GMANDictionary::getQuantity (GMANTokenId id)
{
#ifdef PRE
  is_valid (id);
#endif
  vector<GMANTokenEntry>::iterator first=te.begin();
  return ((first+id-1)->getQuantity());
}
#ifdef DEBUG
RtVoid GMANDictionary::stats (RtVoid)
{
  vector<GMANTokenEntry>::iterator first=te.begin();
  vector<GMANTokenEntry>::iterator last=te.end();
  GMANTokenId i;

  cout << endl;
  cout << "GMANDictionary   Number of entries: " << te.size() << endl;
  cout << "------------------------------------------------------" << endl;
  cout << "NAME                  CLASS    TYPE   [SIZE] IS_INLINE" << endl;                 
  cout << "------------------------------------------------------" << endl;

  for(i=1;first!=last;first++,i++)
    {
      cout << setw(20) << setfill (' ');
      cout << setiosflags(ios::left) << (first->getName()).c_str() << "  ";
      first->printClassType (); 
      cout << "[" << get_quantity(i) << "]  ";
      if ((first->isInline())==true) { 
	cout << " inline";
      }
      cout << endl;
    }
  cout << "------------------------------------------------------" << endl;
  cout << endl;
}
#endif
/**--------CLASS GMANDictionary END--------**/












