/*---------------------------------------------------------
  Copyright (C) Lionel Joseph Lacour 2000, 2001
  2000/07/31  datas storage
  ---------------------------------------------------------
  Trim curves data storage
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

#include "ri.h"
#include "gmantrimcurve.h"

RtVoid GMANTrimCurve::copy (GMANTrimCurve const &tc)
{
  counter=tc.counter;
  *counter+=1;
  nloops=tc.nloops;
  ncurves=tc.ncurves;
  order=tc.order;
  knot=tc.knot;
  min=tc.min;
  max=tc.max;
  n=tc.n;
  u=tc.u;
  v=tc.v;
  w=tc.w;
}

RtVoid GMANTrimCurve::destroy ()
{
  if (*counter!=0) {
    *counter-=1;
    return;
  }
  delete [] ncurves;
  delete [] order;
  delete [] knot;
  delete [] min;
  delete [] max;
  delete [] n;
  delete [] u;
  delete [] v;
  delete [] w;
  delete counter;
}

GMANTrimCurve::GMANTrimCurve (GMANTrimCurve const &tc)
{
  copy(tc);
}

GMANTrimCurve const &GMANTrimCurve::operator=(GMANTrimCurve const &tc)
{
  if (this!=&tc) {
    destroy();
    copy(tc);
  }
  return (*this);
}

GMANTrimCurve::~GMANTrimCurve ()
{
  destroy();
}

GMANTrimCurve::GMANTrimCurve ()
{
  counter=new int;
  *counter=0;
  nloops=0;
  ncurves=0;
  order=0;
  knot=0;
  min=0;
  max=0;
  n=0;
  u=0;
  v=0;
  w=0;
}

GMANTrimCurve::GMANTrimCurve(RtInt nl, RtInt *nc, RtInt *ord,
			     RtFloat *kn, RtFloat *mn, RtFloat *mx,
			     RtInt *nn, RtFloat *uu, RtFloat *vv, RtFloat *ww)
{
  int i;
  int nbcrvs;
  int nbcoords;
  int knotsize;
  counter=new int;
  *counter=0;
  ncurves=new RtInt[nl]; // 1

  nloops=nl; // 2
  nbcrvs=0;
  for(i=0;i<nl;i++) {
    nbcrvs+=nc[i];
    ncurves[i]=nc[i];
  }
  order=new RtInt [nbcrvs]; // 3
  min=new RtFloat [nbcrvs]; // 4
  max=new RtFloat [nbcrvs]; // 5
  n=new RtInt [nbcrvs]; // 6

  nbcoords=0;
  knotsize=0;
  for(i=0;i<nbcrvs;i++) {
    order[i]=ord[i];
    min[i]=mn[i];
    max[i]=mx[i];
    n[i]=nn[i];
    knotsize+=order[i]+n[i];
    nbcoords+=nn[i];
  }

  u=new RtFloat [nbcoords]; // 7
  v=new RtFloat [nbcoords]; // 8
  w=new RtFloat [nbcoords]; // 9
  for(i=0;i<nbcoords;i++) {
    u[i]=uu[i];
    v[i]=vv[i];
    w[i]=ww[i];
  }

  knot=new RtFloat [knotsize]; // 10
  for(i=0;i<knotsize;i++) {
    knot[i]=kn[i];
  }
}
