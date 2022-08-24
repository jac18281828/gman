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

#ifndef __GMANISOA_H
#define __GMANISOA_H 1

#include <string>
#include "ri.h"
#include "universalsuperclass.h"

class GMANDLL GMANISO : public UniversalSuperClass
{
  /* searchpath */
  string archive;
  string texture;
  string shader;
  string procedural;
public:
  RtVoid set (RtToken name, int n, RtToken *tk, RtPointer *dt);
  const string &getArchivePath() const { return archive; }
  const string &getTexturePath() const { return texture; }
  const string &getShaderPath() const { return shader; }
  const string &getProceduralPath() const { return procedural; }
};

class GMANISA : public UniversalSuperClass
{
private:
public:
  GMANISA ();
  ~GMANISA ();
};

#endif

