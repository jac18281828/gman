/*---------------------------------------------------------
  Copyright (C) Lionel Joseph Lacour 2001, 2002
  February 2001  First release
  ---------------------------------------------------------
  Some tools
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

#include "gmantools.h"
#include "gmanmath.h"


/*----------------------------------------------------------
 * Bilinear Patch tools
 */
GMANPoint  GMANTools::bilinear  (RtFloat u, RtFloat v, RtFloat *pnts)
{
  RtFloat uv=u*v;
  GMANPoint p1(&pnts[0]);
  GMANPoint p2(&pnts[3]);
  GMANPoint p3(&pnts[6]);
  GMANPoint p4(&pnts[9]);
  return (p1*(1.0-u-v+uv) + p2*(u-uv) + p3*(v-uv) + p4*uv);
}
GMANPoint  GMANTools::bilinearZ (RtFloat u, RtFloat v, RtFloat *pnts)
{
  RtFloat uv=u*v;
  GMANPoint p1(0.0, 0.0, pnts[0]);
  GMANPoint p2(1.0, 0.0, pnts[1]);
  GMANPoint p3(0.0, 1.0, pnts[2]);
  GMANPoint p4(1.0, 1.0, pnts[3]);
  return (p1*(1.0-u-v+uv) + p2*(u-uv) + p3*(v-uv) + p4*uv);
}
GMANPoint GMANTools::bilinearW (RtFloat u, RtFloat v, RtFloat *pnts)
{
  RtFloat uv=u*v;
  GMANHPoint p1(&pnts[0]);
  GMANHPoint p2(&pnts[4]);
  GMANHPoint p3(&pnts[8]);
  GMANHPoint p4(&pnts[12]);
  GMANPoint res(p1*(1.0-u-v+uv) + p2*(u-uv) + p3*(v-uv) + p4*uv);
  return res;
}

/*----------------------------------------------------------
 * Bilinear PatchMesh tools
 */
GMANPoint  GMANTools::bilinearMesh  (RtFloat u, RtFloat v,
				     RtInt nu, bool uwrap,
				     RtInt nv, bool vwrap,
				     RtFloat *pnts)
{
  // number of patchs in u and v direction 
  RtInt nbupatch=nu, nbvpatch=nv;
  if (uwrap==false) // aperiodic
    nbupatch-=1;
  if (vwrap==false) // aperiodic
    nbvpatch-=1;

  // find which patch to draw
  RtInt patchUStart=(RtInt) floor(u*nbupatch);
  RtInt patchVStart=(RtInt) floor(v*nbvpatch);
  RtFloat newU=u*nbupatch-patchUStart;
  RtFloat newV=v*nbvpatch-patchVStart;

  // Calculate Point coords at u,v
  RtInt pos1=patchUStart;
  RtInt pos2=patchUStart+1;
  RtInt pos3=patchVStart;
  RtInt pos4=patchVStart+1;

  if (pos2==nu)
    pos2=0;
  if (pos4==nv)
    pos4=0;

  RtFloat uv=newU*newV;
  u=newU;
  v=newV;
  
  GMANPoint p1(&pnts[pos1*3+pos3*3*nu]);
  GMANPoint p2(&pnts[pos2*3+pos3*3*nu]);
  GMANPoint p3(&pnts[pos1*3+pos4*3*nu]);
  GMANPoint p4(&pnts[pos2*3+pos4*3*nu]);
  return (p1*(1.0-u-v+uv) + p2*(u-uv) + p3*(v-uv) + p4*uv);
  
}
GMANPoint  GMANTools::bilinearMeshZ (RtFloat u, RtFloat v,
				     RtInt nu, RtInt nv,
				     RtFloat *pnts)
{
  // number of patchs in u and v direction 
  RtInt nbupatch=nu-1, nbvpatch=nv-1;

  // find which patch to draw
  RtInt patchUStart=(RtInt) floor(u*nbupatch);
  RtInt patchVStart=(RtInt) floor(v*nbvpatch);
  RtFloat newU=u*nbupatch-patchUStart;
  RtFloat newV=v*nbvpatch-patchVStart;

  // Calculate Point coords at u,v
  RtInt pos1=patchUStart;
  RtInt pos2=patchUStart+1;
  RtInt pos3=patchVStart;
  RtInt pos4=patchVStart+1;

  RtFloat uv=newU*newV;
  u=newU;
  v=newV;
  RtFloat scu=1.0/nbupatch;
  RtFloat scv=1.0/nbvpatch;
  
  GMANPoint p1(pos1*scu, pos3*scv, pnts[pos1+pos3*nu]);
  GMANPoint p2(pos2*scu, pos3*scv, pnts[pos2+pos3*nu]);
  GMANPoint p3(pos1*scu, pos4*scv, pnts[pos1+pos4*nu]);
  GMANPoint p4(pos2*scu, pos4*scv, pnts[pos2+pos4*nu]);
  return (p1*(1.0-u-v+uv) + p2*(u-uv) + p3*(v-uv) + p4*uv);
}
GMANPoint GMANTools::bilinearMeshW (RtFloat u, RtFloat v,
				     RtInt nu, bool uwrap,
				     RtInt nv, bool vwrap,
				     RtFloat *pnts)
{
  // number of patchs in u and v direction 
  RtInt nbupatch=nu, nbvpatch=nv;
  if (uwrap==false) // aperiodic
    nbupatch-=1;
  if (vwrap==false) // aperiodic
    nbvpatch-=1;

  // find which patch to draw
  RtInt patchUStart=(RtInt) floor(u*nbupatch);
  RtInt patchVStart=(RtInt) floor(v*nbvpatch);
  RtFloat newU=u*nbupatch-patchUStart;
  RtFloat newV=v*nbvpatch-patchVStart;

  // Calculate Point coords at u,v
  RtInt pos1=patchUStart;
  RtInt pos2=patchUStart+1;
  RtInt pos3=patchVStart;
  RtInt pos4=patchVStart+1;

  if (pos2==nu)
    pos2=0;
  if (pos4==nv)
    pos4=0;

  RtFloat uv=newU*newV;
  u=newU;
  v=newV;
  
  GMANHPoint p1(&pnts[pos1*4+pos3*4*nu]);
  GMANHPoint p2(&pnts[pos2*4+pos3*4*nu]);
  GMANHPoint p3(&pnts[pos1*4+pos4*4*nu]);
  GMANHPoint p4(&pnts[pos2*4+pos4*4*nu]);
  GMANPoint res(p1*(1.0-u-v+uv) + p2*(u-uv) + p3*(v-uv) + p4*uv);
  return res;
}

/*----------------------------------------------------------
 * NuPatch tools
 */
RtFloat GMANTools::nurbsBlendFactor (RtInt i, RtInt degree, RtFloat u, RtFloat *knot)
{
  if (degree==1) {
    if (u>=knot[i] && u<=knot[i+1]) return 1.0;
    else return 0.0;
  }

  RtFloat a,b;
  a=knot[i+degree-1]-knot[i];
  b=knot[i+degree]-knot[i+1];

  if (a!=0) {
    a=(u-knot[i])/a*nurbsBlendFactor (i,degree-1,u,knot);
  }
  if (b!=0) {
    b=(knot[i+degree]-u)/b*nurbsBlendFactor (i+1,degree-1,u,knot);
  }
  return a+b;
}
/* Be carefull! unlike other prims, u & v parameters range is
   not [0,1] but umin,umax and vmin,vmax (see RiSpec)
*/
GMANPoint GMANTools::nurbs  (RtFloat u, RtFloat v,
			     RtInt nu, RtInt uorder, RtFloat *uknot,
			     RtInt nv, RtInt vorder, RtFloat *vknot,
			     RtFloat *points)
{
  GMANPoint *pts=new GMANPoint[nu];
  GMANPoint *temp=new GMANPoint[nv];
  GMANPoint res;
  RtFloat *ubf=new RtFloat[nu];
  RtFloat *vbf=new RtFloat[nv];
 
  RtInt i,j;
  for (i=0;i<nu;i++)
    ubf[i]=nurbsBlendFactor(i,uorder,u,uknot);
  for (i=0;i<nv;i++)
    vbf[i]=nurbsBlendFactor(i,vorder,v,vknot);

  for(j=0; j<nv;j++) {
    for(i=0; i<nu;i++) {
      pts[i]=(&points[3*(i+j*nu)]);
      temp[j]=temp[j]+(pts[i]*ubf[i]);
    }
    res=res+(temp[j]*vbf[j]);
  }
  if(pts) delete []pts;
  if(temp) delete []temp;
  if(ubf) delete []ubf;
  if(vbf) delete []vbf;
  return res;
}
GMANPoint GMANTools::nurbsW (RtFloat u, RtFloat v,
			     RtInt nu, RtInt uorder, RtFloat *uknot,
			     RtInt nv, RtInt vorder, RtFloat *vknot,
			     RtFloat *points)
{
  GMANHPoint *pts=new GMANHPoint[nu];
  GMANHPoint *temp=new GMANHPoint[nv];
  GMANHPoint t2(0,0,0,0);
  RtFloat *ubf=new RtFloat[nu];
  RtFloat *vbf=new RtFloat[nv];

  RtInt i,j;
  for(i=0;i<nu;i++) {
    pts[i].setW(0);
    ubf[i]=nurbsBlendFactor(i,uorder,u,uknot);
  }
  for(i=0;i<nv;i++) {
    temp[i].setW(0);
    vbf[i]=nurbsBlendFactor(i,vorder,v,vknot);
  }

  for(j=0; j<nv;j++) {
    for(i=0; i<nu;i++) {
      pts[i]=(&points[4*(i+j*nu)]);
      temp[j]=temp[j]+(pts[i]*ubf[i]);
    }
    t2=t2+(temp[j]*vbf[j]);
  }
  GMANPoint res(t2);
  if(pts) delete []pts;
  if(temp) delete []temp;
  if(ubf) delete []ubf;
  if(vbf) delete []vbf;
  return res;
}

/*----------------------------------------------------------
 * Quadrics tools
 */
GMANPoint GMANTools::sphere      (RtFloat u, RtFloat v,
				  RtFloat rad, RtFloat zmin, RtFloat zmax,
				  RtFloat theta)
{
  RtFloat phimin, phimax, phi;
  
  if (zmin > -rad)
    phimin=asin(zmin/rad);
  else
    phimin=-PI/2;
  if (zmax > rad)
    phimax=asin(zmax/rad);
  else
    phimax=PI/2;
  
  phi=phimin+v*(phimax-phimin);
  GMANPoint res( cos(theta*u)*cos(phi),
		 sin(theta*u)*cos(phi),
		 sin(phi));
  return (res*rad);
}
GMANPoint GMANTools::cone        (RtFloat u, RtFloat v,
				  RtFloat height, RtFloat rad, 
				  RtFloat theta)
{
  GMANPoint res( cos(u*theta)*(1-v)*rad,
		 sin(u*theta)*(1-v)*rad,
		 v*height);
  return res;
}
GMANPoint GMANTools::cylinder    (RtFloat u, RtFloat v,
				  RtFloat rad, RtFloat zmin, RtFloat zmax,
				  RtFloat theta)
{
  GMANPoint res( cos(u*theta)*rad,
		 sin(u*theta)*rad,
		 zmin+v*(zmax-zmin));
  return res;
}
GMANPoint GMANTools::hyperboloid (RtFloat u, RtFloat v,
				  GMANPoint const &p1, GMANPoint const &p2,
				  RtFloat theta)
{
  RtFloat x,y;
  GMANPoint vi=p1*(1-v)+p2*v;
  x=vi.getX();
  y=vi.getY();
  vi.setX( x*cos(u*theta)-y*sin(u*theta));
  vi.setY( x*sin(u*theta)+y*cos(u*theta));
  return vi;
}
GMANPoint GMANTools::paraboloid  (RtFloat u, RtFloat v,
				  RtFloat rmax, RtFloat zmin, RtFloat zmax,
				  RtFloat theta)
{
  RtFloat a=zmin+v*(zmax-zmin);
  RtFloat b=sqrt(a/zmax);
  GMANPoint res( b*cos(u*theta),
		 b*sin(u*theta),
		 a);
  return res;
}
GMANPoint GMANTools::disk        (RtFloat u, RtFloat v,
				  RtFloat height, RtFloat radius,
				  RtFloat theta)
{
  GMANPoint res( (1-v)*radius*cos(u*theta),
		 (1-v)*radius*sin(u*theta),
		 height);
  return res;
}
GMANPoint GMANTools::torus       (RtFloat u, RtFloat v,
				  RtFloat majorr, RtFloat minorr,
				  RtFloat phimin, RtFloat phimax,
				  RtFloat theta)
{
  RtFloat x,a;
  a=(phimax-phimin)*v+phimin;
  GMANPoint res( majorr+cos(a)*minorr,
		 0,
		 sin(a)*minorr);
  x=res.getX();
  res.setX( x*cos(u*theta));
  res.setY( x*sin(u*theta));
  return res;
}
