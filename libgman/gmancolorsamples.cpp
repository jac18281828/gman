/* SPDX-License-Identifier: LGPL-2.1-or-later */

/*----------------------------------------------------------
  Copyright (C) Lionel Joseph Lacour 2000, 2001
  July 2000  First release
  ----------------------------------------------------------
  This class provide color conversion between
  spectral colors to or from RGB
*/
/*
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
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



