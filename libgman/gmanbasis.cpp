/*----------------------------------------------------------
  Copyright (C) Lionel Joseph Lacour 2000, 2001
  2000/08/02  First release
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

#include "gmanbasis.h"
#include "gmanmath.h"
#include "gmanvector4.h"

RtBasis RiBezierBasis= { {-1,  3, -3,  1},
			 { 3, -6,  3,  0}, 
			 {-3,  3,  0,  0},
			 { 1,  0,  0,  0} };

RtBasis RiBSplineBasis = { {-1.0/6,  3.0/6, -3.0/6,  1.0/6},
			   { 3.0/6, -6.0/6,  3.0/6,  0.0/6}, 
			   {-3.0/6,  0.0/6,  3.0/6,  0.0/6},
			   { 1.0/6,  4.0/6,  1.0/6,  0.0/6} };

RtBasis RiCatmullRomBasis = { {-1.0/2,  3.0/2, -3.0/2,  1.0/2},
			      { 2.0/2, -5.0/2,  4.0/2, -1.0/2}, 
			      {-1.0/2,  0.0/2,  1.0/2,  0.0/2},
			      { 0.0/2,  2.0/2,  0.0/2,  0.0/2} };

RtBasis RiHermiteBasis = { { 2,  1, -2,  1},
			   {-3, -2,  3, -1}, 
			   { 0,  1,  0,  0},
			   { 1,  0,  0,  0} };

RtBasis RiPowerBasis = { {1, 0, 0, 0},
			 {0, 1, 0, 0}, 
			 {0, 0, 1, 0},
			 {0, 0, 0, 1} };


GMANBasis::GMANBasis ()
{
  uBasis.setBasis(RiBezierBasis);
  vBasis.setBasis(RiBezierBasis);
  uStep=vStep=RI_BEZIERSTEP;
}

GMANBasis::GMANBasis(const GMANBasis &basis) {
  uBasis = basis.uBasis;
  vBasis = basis.vBasis;
  uStep = basis.uStep;
  vStep = basis.vStep;
};

// dtor
GMANBasis::~GMANBasis() {
  // nothing to do
}

/*----------------------------------------------------------
 * Bicubic Patch tools
 */
GMANPoint  GMANBasis::bicubic  (RtFloat u, RtFloat v, RtFloat *pts)
{
  GMANVector4 q[4];
  GMANVector4 pa(u*u*u, u*u, u, 1.0);
  GMANVector4 pb(v*v*v, v*v, v, 1.0);
  pa*=uBasis;
  pb*=vBasis;

  for (RtInt i=0; i<4; i++) {
    GMANVector4 p1(&pts[0+i*12]), p2(&pts[3+i*12]), p3(&pts[6+i*12]), p4(&pts[9+i*12]);
    q[i]=p1*pa.getX()+p2*pa.getY()+p3*pa.getZ()+p4*pa.getW();
  }
  GMANVector4 res(q[0]*pb.getX()+q[1]*pb.getY()+q[2]*pb.getZ()+q[3]*pb.getW());
  return (GMANVector)res;
}

GMANPoint  GMANBasis::bicubicZ (RtFloat u, RtFloat v, RtFloat *pts)
{
  GMANVector4 q[4];
  GMANVector4 pa(u*u*u, u*u, u, 1.0);
  GMANVector4 pb(v*v*v, v*v, v, 1.0);
  pa*=uBasis;
  pb*=vBasis;

  for (RtInt i=0; i<4; i++) {
    GMANVector p1(0.000, (1.0/3)*i, pts[0+i*4]);
    GMANVector p2(1.0/3, (1.0/3)*i, pts[1+i*4]);
    GMANVector p3(2.0/3, (1.0/3)*i, pts[2+i*4]);
    GMANVector p4(1.000, (1.0/3)*i, pts[3+i*4]);
    q[i]=p1*pa.getX()+p2*pa.getY()+p3*pa.getZ()+p4*pa.getW();
  }
  GMANVector4 res(q[0]*pb.getX()+q[1]*pb.getY()+q[2]*pb.getZ()+q[3]*pb.getW());
  return res;
}

GMANPoint GMANBasis::bicubicW (RtFloat u, RtFloat v, RtFloat *pts)
{
  GMANVector4 q[4];
  GMANVector4 pa(u*u*u, u*u, u, 1.0);
  GMANVector4 pb(v*v*v, v*v, v, 1.0);
  pa*=uBasis;
  pb*=vBasis;

  for (RtInt i=0; i<4; i++) {
    GMANVector4 p1(&pts[0+i*16]), p2(&pts[4+i*16]), p3(&pts[8+i*16]), p4(&pts[12+i*16]);
    q[i]=p1*pa.getX()+p2*pa.getY()+p3*pa.getZ()+p4*pa.getW();
  }
  GMANVector res(q[0]*pb.getX()+q[1]*pb.getY()+q[2]*pb.getZ()+q[3]*pb.getW());
  return res;
}


/*----------------------------------------------------------
 * Bicubic PatchMesh tools
 */
RtInt GMANBasis::offset (RtInt astart, RtInt bstart, RtInt a, RtInt b,
			 RtInt nu, RtInt nv,
			 RtInt pntSize)
{
  RtInt i,j;
  i=astart+a;
  j=bstart+b;
  if (i>nu)
    i-=nu;
  if (j>nv)
    j-=nv;
  return pntSize*(i+nu*j);
}

GMANPoint  GMANBasis::bicubicMesh  (RtFloat u, RtFloat v,
				    RtInt nu, bool uwrap,
				    RtInt nv, bool vwrap,
				    RtFloat *pts)
{
  // number of patchs in u and v direction 
  RtInt nbupatch, nbvpatch;
 if (uwrap==true) {
    nbupatch= nu/uStep;
  } else {
    nbupatch= 1 + (nu-4)/uStep;
  }
  if (vwrap==true) {
    nbvpatch= nv/vStep;
  } else {
    nbvpatch= 1 + (nv-4)/vStep;
  }

  // find which patch to draw
  RtInt patchUStart=(RtInt) floor(u*nbupatch);
  RtInt patchVStart=(RtInt) floor(v*nbvpatch);
  RtFloat newU=u*nbupatch-patchUStart;
  RtFloat newV=v*nbvpatch-patchVStart;
  patchUStart*=uStep;
  patchVStart*=vStep;

  // Calculate Point coords at u,v
  GMANVector q[4];
  GMANVector4 pa(newU*newU*newU, newU*newU, newU, 1.0);
  GMANVector4 pb(newV*newV*newV, newV*newV, newV, 1.0);
  pa*=uBasis;
  pb*=vBasis;

  RtInt ofst;
  for (RtInt i=0; i<4; i++) {
    ofst=offset(patchUStart, patchVStart, 0, i, nu, nv, 3);
    GMANVector p1(&pts[ofst]);
    
    ofst=offset(patchUStart, patchVStart, 1, i, nu, nv, 3);
    GMANVector p2(&pts[ofst]);
    
    ofst=offset(patchUStart, patchVStart, 2, i, nu, nv, 3);
    GMANVector p3(&pts[ofst]);
    
    ofst=offset(patchUStart, patchVStart, 3, i, nu, nv, 3);
    GMANVector p4(&pts[ofst]);
    
    q[i]=p1*pa.getX()+p2*pa.getY()+p3*pa.getZ()+p4*pa.getW();
  }
  GMANVector res(q[0]*pb.getX()+q[1]*pb.getY()+q[2]*pb.getZ()+q[3]*pb.getW());
  return res;
}

GMANPoint  GMANBasis::bicubicMeshZ (RtFloat u, RtFloat v,
				    RtInt nu, RtInt nv,
				    RtFloat *pts)
{
  // number of patchs in u and v direction 
  RtInt nbupatch= 1 + (nu-4)/uStep;
  RtInt nbvpatch= 1 + (nv-4)/vStep;

  // find which patch to draw
  RtInt scustart=(RtInt) floor(u*nbupatch);
  RtInt scvstart=(RtInt) floor(v*nbvpatch);
  RtFloat newU=u*nbupatch-scustart;
  RtFloat newV=v*nbvpatch-scvstart;
  RtInt patchUStart=scustart*uStep;
  RtInt patchVStart=scvstart*vStep;

  // Calculate Point coords at u,v
  GMANVector q[4];
  GMANVector4 pa(newU*newU*newU, newU*newU, newU, 1.0);
  GMANVector4 pb(newV*newV*newV, newV*newV, newV, 1.0);
  pa*=uBasis;
  pb*=vBasis;

  RtFloat scu=1.0/(nu-1);
  RtFloat scv=1.0/(nv-1);

  RtInt ofst;
  for (RtInt i=0; i<4; i++) {
    ofst=patchUStart+0 +(patchVStart+i)*nu;
    GMANVector p1( scustart*scu,     (scvstart+i)*scv, pts[ofst]);
    
    ofst=patchUStart+1 +(patchVStart+i)*nu;
    GMANVector p2( (scustart+1)*scu, (scvstart+i)*scv, pts[ofst]);
    
    ofst=patchUStart+2 +(patchVStart+i)*nu;
    GMANVector p3( (scustart+2)*scu, (scvstart+i)*scv, pts[ofst]);
    
    ofst=patchUStart+3 +(patchVStart+i)*nu;
    GMANVector p4( (scustart+3)*scu, (scvstart+i)*scv, pts[ofst]);
    
    q[i]=p1*pa.getX()+p2*pa.getY()+p3*pa.getZ()+p4*pa.getW();
  }
  GMANVector res(q[0]*pb.getX()+q[1]*pb.getY()+q[2]*pb.getZ()+q[3]*pb.getW());
  return res;
}

GMANPoint GMANBasis::bicubicMeshW (RtFloat u, RtFloat v,
				   RtInt nu, bool uwrap,
				   RtInt nv, bool vwrap,
				   RtFloat *pts)
{
  // number of patchs in u and v direction 
  RtInt nbupatch, nbvpatch;
  if (uwrap==true) {
    nbupatch= nu/uStep;
  } else {
    nbupatch= 1 + (nu-4)/uStep;
  }
  if (vwrap==true) {
    nbvpatch= nv/vStep;
  } else {
    nbvpatch= 1 + (nv-4)/vStep;
  }

  // find which patch to draw
  RtInt patchUStart=(RtInt) floor(u*nbupatch);
  RtInt patchVStart=(RtInt) floor(v*nbvpatch);
  RtFloat newU=u*nbupatch-patchUStart;
  RtFloat newV=v*nbvpatch-patchVStart;
  patchUStart*=uStep;
  patchVStart*=vStep;

  // Calculate Point coords at u,v
  GMANVector4 q[4];
  GMANVector4 pa(newU*newU*newU, newU*newU, newU, 1.0);
  GMANVector4 pb(newV*newV*newV, newV*newV, newV, 1.0);
  pa*=uBasis;
  pb*=vBasis;

  RtInt ofst;
  for (RtInt i=0; i<4; i++) {
    ofst=offset(patchUStart, patchVStart, 0, i, nu, nv, 4);
    GMANVector4 p1(&pts[ofst]);
    
    ofst=offset(patchUStart, patchVStart, 1, i, nu, nv, 4);
    GMANVector4 p2(&pts[ofst]);
    
    ofst=offset(patchUStart, patchVStart, 2, i, nu, nv, 4);
    GMANVector4 p3(&pts[ofst]);
    
    ofst=offset(patchUStart, patchVStart, 3, i, nu, nv, 4);
    GMANVector4 p4(&pts[ofst]);
    
    q[i]=p1*pa.getX()+p2*pa.getY()+p3*pa.getZ()+p4*pa.getW();
  }
  GMANVector res(q[0]*pb.getX()+q[1]*pb.getY()+q[2]*pb.getZ()+q[3]*pb.getW());
  return res;
}







