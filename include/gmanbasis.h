/*----------------------------------------------------------
  Copyright (C) Lionel Joseph Lacour 2000, 2001
  2000/08/02  Fisrt release
  2001/02     Added bicubic Patch and PatchMesh tools
  ----------------------------------------------------------
  Handle basis storage.
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

#ifndef __GMANBASIS_H
#define __GMANBASIS_H 1

#include "ri.h"
#include "universalsuperclass.h"
#include "gmanmatrix4.h"
#include "gmanpoint.h"
#include "gmanhpoint.h"

class GMANDLL GMANBasis : public UniversalSuperClass
{
private:
  GMANMatrix4 uBasis;
  RtInt uStep;
  GMANMatrix4 vBasis;
  RtInt vStep;

  RtInt offset (RtInt astart, RtInt bstart, RtInt a, RtInt b,
		RtInt nu, RtInt nv, RtInt pntSize);
public:
  GMANBasis ();

  // copy ctor
  GMANBasis(const GMANBasis &basis);

  ~GMANBasis();
  
  GMANBasis (const GMANMatrix4 &ubss, 
	     RtInt ustp, 
	     const GMANMatrix4 &vbss, 
	     RtInt vstp)
    : uBasis(ubss), uStep(ustp), vBasis(vbss), vStep(vstp) {
  }

  GMANPoint bicubic  (RtFloat u, RtFloat v, RtFloat *pts);
  GMANPoint bicubicZ (RtFloat u, RtFloat v, RtFloat *pts);
  GMANPoint bicubicW (RtFloat u, RtFloat v, RtFloat *pts);

  GMANPoint bicubicMesh  (RtFloat u, RtFloat v,
			  RtInt nu, bool uwrap,
			  RtInt nv, bool vwrap,
			  RtFloat *pts);
  GMANPoint bicubicMeshZ (RtFloat u, RtFloat v,
			  RtInt nu, RtInt nv,
			  RtFloat *pts);
  GMANPoint bicubicMeshW (RtFloat u, RtFloat v,
			  RtInt nu, bool uwrap,
			  RtInt nv, bool vwrap,
			  RtFloat *pts);
};

#endif



