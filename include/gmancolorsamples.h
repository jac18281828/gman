/*----------------------------------------------------------
  Copyright (C) Lionel Joseph Lacour 2000, 2001
  July 2000  First release
  ----------------------------------------------------------
  This class provide color conversion between
  spectral colors to or from RGB
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

#ifndef __GMANCOLORSAMPLES_H
#define __GMANCOLORSAMPLES_H

#include "ri.h"
#include "universalsuperclass.h"
#include "gmandefaults.h"

class GMANDLL GMANColorSamples : public UniversalSuperClass
{
private:
  int *counter;

  RtInt number;
  RtFloat *nRGB;
  RtFloat *RGBn;

  RtVoid copy(RtInt n, RtFloat *ncolor, RtFloat *colorn);
  RtVoid copy(GMANColorSamples const &);
  RtVoid destroy();
public:
  GMANColorSamples ();
  GMANColorSamples (RtInt n, RtFloat *ncolor, RtFloat *colorn);
  GMANColorSamples (GMANColorSamples const &);
  ~GMANColorSamples ();
  GMANColorSamples const &operator=(GMANColorSamples const &);

  int getNumber () { return number; }
};

#endif
