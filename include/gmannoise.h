/*----------------------------------------------------------
  Copyright (C) Lionel Joseph Lacour 2001, 2002
  February 2001 First release
  ----------------------------------------------------------
  This code based on Ken Perlin gradient noise function.
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

#ifndef __GMANNOISE_H
#define __GMANNOISE_H 1

#include "ri.h"
#include "gmanpoint.h"

/* Noise and pnoise */
#define N    256
#define MASK 0xff

/* CellNoise */
#define CN    2048
#define CMASK 0x7ff

class GMANDLL  GMANNoise
{
private:
  RtInt  *prn;
  RtInt   prn1[N];  /* 0 to N in random order*/
  RtInt   prn2[N];
  RtInt   prn3[N];
  RtFloat vect[4*N]; /* N 4D random unit vectors */

  RtInt  *cprn;
  RtInt   cprn1[CN];
  RtInt   cprn2[CN];
  RtInt   cprn3[CN];
  RtFloat cvect[CN];

  RtFloat rn();
public:
  GMANNoise();

  RtFloat noise (RtFloat v);
  RtFloat noise (RtFloat u, RtFloat v);
  RtFloat noise (GMANPoint const &p);
  RtFloat noise (GMANPoint const &p, RtFloat t);

  RtVoid  noise (RtFloat v, RtFloat &a, RtFloat &b, RtFloat &c);
  RtVoid  noise (RtFloat u, RtFloat v, RtFloat &a, RtFloat &b, RtFloat &c);
  RtVoid  noise (GMANPoint const &p, RtFloat &a, RtFloat &b, RtFloat &c);
  RtVoid  noise (GMANPoint const &p, RtFloat t, RtFloat &a, RtFloat &b, RtFloat &c);

  RtFloat periodic (RtFloat v, RtFloat pv);
  RtFloat periodic (RtFloat u, RtFloat v, RtFloat pu, RtFloat pv);
  RtFloat periodic (GMANPoint const &p, GMANPoint const &pp);
  RtFloat periodic (GMANPoint const &p, RtFloat t, GMANPoint const &pp, RtFloat pt);

  RtVoid  periodic (RtFloat v, RtFloat pv,
		    RtFloat &a, RtFloat &b, RtFloat &c);
  RtVoid  periodic (RtFloat u, RtFloat v, RtFloat pu, RtFloat pv,
		    RtFloat &a, RtFloat &b, RtFloat &c);
  RtVoid  periodic (GMANPoint const &p, GMANPoint const &pp,
		    RtFloat &a, RtFloat &b, RtFloat &c);
  RtVoid  periodic (GMANPoint const &p, RtFloat t, GMANPoint const &pp,
		    RtFloat pt, RtFloat &a, RtFloat &b, RtFloat &c);

  RtFloat cellnoise (RtFloat v);
  RtFloat cellnoise (RtFloat u, RtFloat v);
  RtFloat cellnoise (GMANPoint const &p);
  RtFloat cellnoise (GMANPoint const &p, RtFloat t);

  RtVoid  cellnoise (RtFloat v, RtFloat &a, RtFloat &b, RtFloat &c);
  RtVoid  cellnoise (RtFloat u, RtFloat v, RtFloat &a, RtFloat &b, RtFloat &c);
  RtVoid  cellnoise (GMANPoint const &p, RtFloat &a, RtFloat &b, RtFloat &c);
  RtVoid  cellnoise (GMANPoint const &p, RtFloat t, RtFloat &a, RtFloat &b, RtFloat &c);
};

#endif
