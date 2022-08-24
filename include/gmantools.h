/*---------------------------------------------------------
  Copyright (C) Lionel Joseph Lacour 2001, 2002
  February 2001  First release
  ---------------------------------------------------------
  Some tools.
  Bicubic tools are in gmanbasis.
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

#ifndef _GMANTOOLS_H
#define _GMANTOOLS_H

#include "ri.h"
#include "universalsuperclass.h"
#include "gmanpoint.h"
#include "gmanhpoint.h"

class GMANDLL  GMANTools : public UniversalSuperClass
{
 public:
  /*----------------------------------------------------------
   * Bilinear Patch tools
   */
  static GMANPoint bilinear  (RtFloat u, RtFloat v, RtFloat *pnts);
  static GMANPoint bilinearZ (RtFloat u, RtFloat v, RtFloat *pnts);
  static GMANPoint bilinearW (RtFloat u, RtFloat v, RtFloat *pnts);
  
  /*----------------------------------------------------------
   * Bilinear PatchMesh tools
   */
  static GMANPoint bilinearMesh  (RtFloat u, RtFloat v,
				  RtInt nu, bool uwrap,
				  RtInt nv, bool vwrap,
				  RtFloat *pts);
  static GMANPoint bilinearMeshZ (RtFloat u, RtFloat v,
				  RtInt nu, RtInt nv,
				  RtFloat *pts);
  static GMANPoint bilinearMeshW (RtFloat u, RtFloat v,
				  RtInt nu, bool uwrap,
				  RtInt nv, bool vwrap,
				  RtFloat *pts);

 /*----------------------------------------------------------
  * NuPatch tools
  */
  static RtFloat nurbsBlendFactor (RtInt i, RtInt degree, RtFloat u, RtFloat *knot);
  static GMANPoint nurbs          (RtFloat u, RtFloat v,
				   RtInt nu, RtInt uorder, RtFloat *uknot,
				   RtInt nv, RtInt vorder, RtFloat *vknot,
				   RtFloat *points);
  static GMANPoint nurbsW         (RtFloat u, RtFloat v,
				   RtInt nu, RtInt uorder, RtFloat *uknot,
				   RtInt nv, RtInt vorder, RtFloat *vknot,
				   RtFloat *points);

  /*----------------------------------------------------------
   * Quadrics tools
   */
  static GMANPoint sphere      (RtFloat u, RtFloat v,
				RtFloat radius, RtFloat zmin, RtFloat zmax,
				RtFloat thetamax);
  static GMANPoint cone        (RtFloat u, RtFloat v,
				RtFloat height, RtFloat radius, 
				RtFloat thetamax);
  static GMANPoint cylinder    (RtFloat u, RtFloat v,
				RtFloat radius, RtFloat zmin, RtFloat zmax,
				RtFloat thetamax);
  static GMANPoint hyperboloid (RtFloat u, RtFloat v,
				GMANPoint const &p1, GMANPoint const &p2,
				RtFloat thetamax);
  static GMANPoint paraboloid  (RtFloat u, RtFloat v,
				RtFloat rmax, RtFloat zmin, RtFloat zmax,
				RtFloat thetamax);
  static GMANPoint disk        (RtFloat u, RtFloat v,
				RtFloat height, RtFloat radius,
				RtFloat thetamax);
  static GMANPoint torus       (RtFloat u, RtFloat v,
				RtFloat majorr, RtFloat minorr,
				RtFloat phimin, RtFloat phimax,
				RtFloat thetamax);
};
#endif
