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

#include "gmancolorsamples.h"


RtVoid GMANColorSamples::copy(RtInt n, RtFloat *ncolor, RtFloat *colorn)
{
  for(int i=0;i<n;i++) {
    nRGB[i]=ncolor[i];
    RGBn[i]=colorn[i];
  }
}

RtVoid GMANColorSamples::copy(GMANColorSamples const &cs)
{
  counter=cs.counter;
  *counter+=1;
  number=cs.number;
  RGBn=cs.RGBn;
  nRGB=cs.nRGB;
}

RtVoid GMANColorSamples::destroy()
{
  if (*counter!=0) {
    *counter-=1;
    return;
  }
  delete counter;
  delete [] nRGB;
  delete [] RGBn;
}

GMANColorSamples::GMANColorSamples ()
{
  counter=new int;
  *counter=0;
  number=DefaultColorSamples;
  nRGB=new RtFloat [DefaultColorSamples*3];
  RGBn=new RtFloat [DefaultColorSamples*3];
  for (int i=0;i<DefaultColorSamples*3;i++) {
    nRGB[i]=DefaultCSMatrixNRGB[i];
    RGBn[i]=DefaultCSMatrixRGBN[i];
  }
}

GMANColorSamples::GMANColorSamples (RtInt n, RtFloat *ncolor, RtFloat *colorn)
{
  counter=new int;
  *counter=0;
  number=n;
  nRGB=new RtFloat[3*n];
  RGBn=new RtFloat[3*n];
  for (int i=0;i<3*n;i++) {
    nRGB[i]=ncolor[i];
    RGBn[i]=colorn[i];
  }
}

GMANColorSamples::GMANColorSamples (GMANColorSamples const &cs)
{
  copy(cs);
}
GMANColorSamples::~GMANColorSamples ()
{
  destroy();
}

GMANColorSamples const &GMANColorSamples::operator=(GMANColorSamples const &cs)
{
  if (this!=&cs) {
    destroy();
    copy(cs);
  }
  return (*this);
}



