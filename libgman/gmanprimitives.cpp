/*---------------------------------------------------------
  Copyright (C) Lionel Joseph Lacour 2000, 2001
  December 2000  First release
  ---------------------------------------------------------
  Primitives datas storage.
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

#include <math.h>

#include "gmanprimitives.h"

///////////////////////////////////////////////////////////////////////////////////////////////
////  GMAN_BLOBBY.CPP
///////////////////////////////////////////////////////////////////////////////////////////////

RtVoid GMANBlobby::copy(GMANBlobby const &b)
{
  counter=b.counter;
  *counter+=1;
  nleaf=b.nleaf;
  code=b.code;
  nfloat=b.nfloat;
  floats=b.floats;
  nstrings=b.nstrings;
  strings=b.strings;
}
RtVoid GMANBlobby::destroy()
{
  if ((*counter-=1)==0) {
    delete counter;
    delete [] code;
    delete [] floats;
    delete [] strings;
  }
}
GMANBlobby::GMANBlobby(RtInt nlf, RtInt ncd, RtInt cd[],
		       RtInt nf, RtFloat f[],
		       RtInt ns, RtString s[], GMANParameterList p) : GMANPrimDatStorage(p)
{
  int i;
  counter=new int;
  *counter=1;

  nleaf=nlf;
  ncode=ncd;
  code=new RtInt[ncd];
  for(i=0;i<ncd;i++)
    code[i]=cd[i];
  nfloat=nf;
  floats=new RtFloat[nf];
  for(i=0;i<nf;i++)
    floats[i]=f[i];
  nstrings=ns;
  strings=new string[ns];
  for(i=0;i<ns;i++)
    strings[i]=string(s[i]);
}
GMANBlobby::GMANBlobby(GMANBlobby const &b) : GMANPrimDatStorage(b)
{
  copy(b);
}
GMANBlobby::~GMANBlobby()
{
  destroy();
}
GMANBlobby const &GMANBlobby::operator=(GMANBlobby const &b)
{
  if(this!=&b) {
    destroy();
    GMANPrimDatStorage *a1 = this;
    GMANPrimDatStorage const *a2=&b;
    *a1=*a2;
    copy(b);
  }
  return (*this);
}







///////////////////////////////////////////////////////////////////////////////
////  GMAN_CONE.CPP
///////////////////////////////////////////////////////////////////////////////
GMANPoint GMANCone::getLocation (double u, double v)
{
  float theta = u * (thetamax / 360.0) * 2.0 * PI;

  float r = (1.0 - v) * radius;
  float x = r * cos(theta);
  float y = r * sin(theta);
  float z = v * height;
  
  return GMANPoint(x, y, z);
}

GMANVector GMANCone::getNormal (double u, double v)
{
  throw GMANError(RIE_SYSTEM, RIE_ERROR, "Not implemented");
}






///////////////////////////////////////////////////////////////////////////////////////////////
////  GMAN_CURVES.CPP
///////////////////////////////////////////////////////////////////////////////////////////////
RtVoid GMANCurves::copy(GMANCurves const &c)
{
  type=c.type;
  ncurves=c.ncurves;
  nvertices=new RtInt [ncurves];
  for(int i=0;i<ncurves;i++)
    nvertices[i]=c.nvertices[i];
  wrap=c.wrap;
}

GMANCurves::GMANCurves(RtToken ty, RtInt nc, RtInt *nv, RtToken wr, GMANParameterList p) 
  : GMANPrimDatStorage(p)
{
  type=ty;
  ncurves=nc;
  nvertices=new RtInt[ncurves];
  for (int i=0;i<nc;i++)
    nvertices[i]=nv[i];
  wrap=wr;
}

GMANCurves::GMANCurves(GMANCurves const &c) : GMANPrimDatStorage (c)
{
  copy(c);
}

GMANCurves const &GMANCurves::operator=(GMANCurves const &c)
{
  if (this!=&c) {
    delete [] nvertices;
    GMANPrimDatStorage *a1=this;
    GMANPrimDatStorage const *a2=&c;
    *a1=*a2;
    copy(c);
  }
  return (*this);
}

GMANCurves::~GMANCurves()
{
  delete [] nvertices;
}







///////////////////////////////////////////////////////////////////////////////
////  GMAN_CYLINDER.CPP
///////////////////////////////////////////////////////////////////////////////
GMANPoint GMANCylinder::getLocation (double u, double v)
{
  float theta = u * (thetamax / 360.0) * 2.0 * PI;

  float x = radius * cos(theta);
  float y = radius * sin(theta);
  float z = zmin + (zmax - zmin) * v;
  
  return GMANPoint(x, y, z);
}

GMANVector GMANCylinder::getNormal (double u, double v)
{
  throw GMANError(RIE_SYSTEM, RIE_ERROR, "Not implemented");
}






///////////////////////////////////////////////////////////////////////////////
////  GMAN_DISK.CPP
///////////////////////////////////////////////////////////////////////////////
GMANPoint GMANDisk::getLocation (double u, double v)
{
  throw GMANError(RIE_SYSTEM, RIE_ERROR, "Not implemented");
}

GMANVector GMANDisk::getNormal (double u, double v)
{
  throw GMANError(RIE_SYSTEM, RIE_ERROR, "Not implemented");
}






///////////////////////////////////////////////////////////////////////////////////////////////
////  GMAN_GENERALPOLYGON.CPP
///////////////////////////////////////////////////////////////////////////////////////////////
RtVoid GMANGeneralPolygon::copy (GMANGeneralPolygon const &gp)
{
  nloop=gp.nloop;
  nvert=new RtInt[nloop];
  for(int i=0;i<nloop;i++)
    nvert[i]=gp.nvert[i];
}

GMANGeneralPolygon::GMANGeneralPolygon (RtInt nl, RtInt nv[], GMANParameterList p) 
  : GMANPrimDatStorage(p)
{
  nloop=nl;
  nvert=new RtInt[nl];
  for(int i=0;i<nl;i++)
    nvert[i]=nv[i];
}

GMANGeneralPolygon::GMANGeneralPolygon (GMANGeneralPolygon const &gp) : GMANPrimDatStorage(gp)
{
  copy (gp);
}

GMANGeneralPolygon const &GMANGeneralPolygon::operator=(GMANGeneralPolygon const &gp)
{
  if (this!=&gp) {
    delete [] nvert;
    GMANPrimDatStorage *a1=this;
    GMANPrimDatStorage const *a2=&gp;
    *a1=*a2;
    copy (gp);
  }
  return (*this);
}

GMANGeneralPolygon::~GMANGeneralPolygon()
{
  delete [] nvert;
}







///////////////////////////////////////////////////////////////////////////////
////  GMAN_HYPERBOLOID.CPP
///////////////////////////////////////////////////////////////////////////////
GMANPoint GMANHyperboloid::getLocation (double u, double v)
{
  throw GMANError(RIE_SYSTEM, RIE_ERROR, "Not implemented");
}

GMANVector GMANHyperboloid::getNormal (double u, double v)
{
  throw GMANError(RIE_SYSTEM, RIE_ERROR, "Not implemented");
}






///////////////////////////////////////////////////////////////////////////////////////////////
////  GMAN_NUPATCH.CPP
///////////////////////////////////////////////////////////////////////////////////////////////
RtVoid GMANNuPatch::copy(GMANNuPatch const &np)
{
  int i;
  nu=np.nu;
  uorder=np.uorder;
  uknot=new RtFloat[nu+uorder];
  for (i=0; i<nu+uorder; i++) {
    uknot[i]=np.uknot[i];
  }
  umin=np.umin;
  umax=np.umax;

  nv=np.nv;
  vorder=np.vorder;
  vknot=new RtFloat[nv+vorder];
  for (i=0; i<nv+vorder; i++) {
    vknot[i]=np.vknot[i];
  }
  vmin=np.vmin;
  vmax=np.vmax;
}

RtVoid GMANNuPatch::destroy ()
{
  delete [] uknot;
  delete [] vknot;
}

GMANNuPatch::GMANNuPatch(RtInt nu, RtInt uorder, RtFloat uknot[], RtFloat umin, RtFloat umax,
			 RtInt nv, RtInt vorder, RtFloat vknot[], RtFloat vmin, RtFloat vmax,
			 GMANParameterList p) : GMANPrimDatStorage(p)
{
  int i;
  this->nu=nu;
  this->uorder=uorder;
  this->uknot=new RtFloat[nu+uorder];
  for (i=0; i<nu+uorder; i++) {
    this->uknot[i]=uknot[i];
  }
  this->umin=umin;
  this->umax=umax;

  this->nv=nv;
  this->vorder=vorder;
  this->vknot=new RtFloat[nv+vorder];
  for (i=0; i<nv+vorder; i++) {
    this->vknot[i]=vknot[i];
  }
  this->vmin=vmin;
  this->vmax=vmax;
}

GMANNuPatch::GMANNuPatch(GMANNuPatch const &np) : GMANPrimDatStorage (np)
{
  copy(np);
}

GMANNuPatch const &GMANNuPatch::operator=(GMANNuPatch const &np)
{
  if (this!=&np) {
    destroy();
    GMANPrimDatStorage *a1=this;
    GMANPrimDatStorage const *a2=&np;
    *a1=*a2;
    copy(np);
  }
  return (*this);
}

GMANNuPatch::~GMANNuPatch()
{
  destroy();
}







///////////////////////////////////////////////////////////////////////////////
////  GMAN_PARABOLOID.CPP
///////////////////////////////////////////////////////////////////////////////
GMANPoint GMANParaboloid::getLocation (double u, double v)
{
  float theta = u * (thetamax / 360.0) * 2.0 * PI;

  float factor = rmax * sqrt(v / zmax);
  float x = factor * cos(theta);
  float y = factor * sin(theta);
  float z = zmin + (zmax - zmin) * v;
  
  return GMANPoint(x, y, z);
}

GMANVector GMANParaboloid::getNormal (double u, double v)
{
  throw GMANError(RIE_SYSTEM, RIE_ERROR, "Not implemented");
}






///////////////////////////////////////////////////////////////////////////////////////////////
////  GMAN_PATCH.CPP
///////////////////////////////////////////////////////////////////////////////////////////////
GMANPatch::GMANPatch (RtToken pat, GMANParameterList p) : GMANPrimDatStorage(p)
{
  pt=pat;
}







///////////////////////////////////////////////////////////////////////////////////////////////
////  GMAN_PATCHMESH.CPP
///////////////////////////////////////////////////////////////////////////////////////////////
GMANPatchMesh::GMANPatchMesh (RtToken pat, RtInt u, RtToken uw, RtInt v, RtToken vw,
			      GMANParameterList p) : GMANPrimDatStorage(p)
{
  pt=pat;
  nu=u;
  uwrap=uw;
  nv=v;
  vwrap=vw;
}







///////////////////////////////////////////////////////////////////////////////////////////////
////  GMAN_POINTSGENERALPOLYGONS.CPP
///////////////////////////////////////////////////////////////////////////////////////////////
RtVoid GMANPointsGeneralPolygons::copy(GMANPointsGeneralPolygons const &pgp)
{
  int i;
  int j=0;
  npolys=pgp.npolys;
  nloops=new RtInt[npolys];
  for(i=0;i<npolys;i++) {
    nloops[i]=pgp.nloops[i];
    j+=nloops[i];
  }
  int k=0;
  nvertices=new RtInt[j];
  for(i=0;i<j;i++) {
    nvertices[i]=pgp.nvertices[i];
    k+=nvertices[i];
  }
  vertices=new RtInt[k];
  for(i=0;i<k;i++) {
    vertices[i]=pgp.vertices[i];
  }
}

RtVoid GMANPointsGeneralPolygons::destroy()
{
  delete [] nloops;
  delete [] nvertices;
  delete [] vertices;
}

GMANPointsGeneralPolygons::GMANPointsGeneralPolygons(RtInt np, RtInt nl[], RtInt nv[], RtInt v[],
						     GMANParameterList p) : GMANPrimDatStorage(p)
{
  int i;
  int j=0;
  npolys=np;
  nloops=new RtInt[npolys];
  for(i=0;i<npolys;i++) {
    nloops[i]=nl[i];
    j+=nloops[i];
  }
  int k=0;
  nvertices=new RtInt[j];
  for(i=0;i<j;i++) {
    nvertices[i]=nv[i];
    k+=nvertices[i];
  }
  vertices=new RtInt[k];
  for(i=0;i<k;i++) {
    vertices[i]=v[i];
  }
}

GMANPointsGeneralPolygons::GMANPointsGeneralPolygons(GMANPointsGeneralPolygons const &pgp) 
  : GMANPrimDatStorage(pgp)
{
  int i;
  int j=0;
  npolys=pgp.npolys;
  nloops=new RtInt[npolys];
  for(i=0;i<npolys;i++) {
    nloops[i]=pgp.nloops[i];
    j+=nloops[i];
  }
  int k=0;
  nvertices=new RtInt[j];
  for(i=0;i<j;i++) {
    nvertices[i]=pgp.nvertices[i];
    k+=nvertices[i];
  }
  vertices=new RtInt[k];
  for(i=0;i<k;i++) {
    vertices[i]=pgp.vertices[i];
  }
}

GMANPointsGeneralPolygons const &GMANPointsGeneralPolygons::operator=(GMANPointsGeneralPolygons const &pgp)
{
  if (this!=&pgp) {
    destroy();
    GMANPrimDatStorage *a1=this;
    GMANPrimDatStorage const *a2=&pgp;
    *a1=*a2;
    copy(pgp);
  }
  return (*this);
}

GMANPointsGeneralPolygons::~GMANPointsGeneralPolygons()
{
  destroy();
}







///////////////////////////////////////////////////////////////////////////////////////////////
////  GMAN_POINTSPOLYGONS.CPP
///////////////////////////////////////////////////////////////////////////////////////////////
RtVoid GMANPointsPolygons::copy(GMANPointsPolygons const &pp)
{
  int j=0, i;
  npolys=pp.npolys;
  nvertices=new RtInt[npolys];
  for(i=0;i<npolys;i++) {
    nvertices[i]=pp.nvertices[i];
    j+=nvertices[i];
  }
  vertices=new RtInt[j];
  for(i=0;i<j;i++)
    vertices[i]=pp.vertices[i];
}
RtVoid GMANPointsPolygons::destroy()
{
  delete [] nvertices;
  delete [] vertices;
}

GMANPointsPolygons::GMANPointsPolygons(RtInt np,RtInt nv[], RtInt v[],
				       GMANParameterList p) : GMANPrimDatStorage(p)
{
  int i,j=0;
  npolys=np;
  nvertices=new RtInt[npolys];
  for(i=0;i<npolys;i++) {
    nvertices[i]=nv[i];
    j+=nvertices[i];
  }
  vertices=new RtInt[j];
  for(i=0;i<j;i++)
    vertices[i]=v[i];
}

GMANPointsPolygons::GMANPointsPolygons(GMANPointsPolygons const &pp) : GMANPrimDatStorage(pp)
{
  copy(pp);
}

GMANPointsPolygons const &GMANPointsPolygons::operator=(GMANPointsPolygons const &pp)
{
  if (this!=&pp) {
    destroy();
    GMANPrimDatStorage *a1=this;
    GMANPrimDatStorage const *a2=&pp;
    *a1=*a2;
    copy(pp);
  }
  return (*this);
}

GMANPointsPolygons::~GMANPointsPolygons()
{
  destroy();
}







///////////////////////////////////////////////////////////////////////////////
////  GMAN_SPHERE.CPP
///////////////////////////////////////////////////////////////////////////////
GMANPoint GMANSphere::getLocation (double u, double v)
{
  static bool warned = false;
  if (!warned) {
    debug("FIXME: sphere parameterization zmin, zmax");
    warned = true;
  }
  float theta = (u * (thetamax / 360.0) - 0.5) * PI * 2.0;
  float phi = v * 2 * PI;

  float cosPhi = cos(phi);
  float x = radius * cosPhi * cos(theta);
  float y = radius * cosPhi * sin(theta);
  float z = radius * sin(phi);
  
  return GMANPoint(x, y, z);
}

GMANVector GMANSphere::getNormal (double u, double v)
{
  throw GMANError(RIE_SYSTEM, RIE_ERROR, "Not implemented");
}






///////////////////////////////////////////////////////////////////////////////////////////////
////  GMAN_SUBDIVISIONMESH.CPP
///////////////////////////////////////////////////////////////////////////////////////////////
GMANSubdivisionTag::GMANSubdivisionTag(RtToken t, 
				       RtInt isize, RtInt fsize, 
				       RtInt *iargs, RtFloat *fargs)
{
  int i;
  tag=t;
  intsize=isize;
  floatsize=fsize;
  intargs=new RtInt[intsize];
  for(i=0;i<intsize;i++)
    intargs[i]=iargs[i];
  floatargs=new RtFloat[floatsize];
  for(i=0;i<floatsize;i++)
    floatargs[i]=fargs[i];
}

GMANSubdivisionTag::GMANSubdivisionTag(GMANSubdivisionTag const &st)
{
  copy(st);
}

GMANSubdivisionTag const &GMANSubdivisionTag::operator=(GMANSubdivisionTag const &st)
{
  if (this!=&st) {
    delete intargs;
    delete floatargs;
    copy(st);
  }
  return (*this);
}

GMANSubdivisionTag::~GMANSubdivisionTag()
{
  delete intargs;
  delete floatargs;
}

RtVoid GMANSubdivisionTag::copy(GMANSubdivisionTag const &st)
{
  int i;
  tag=st.tag;
  intsize=st.intsize;
  floatsize=st.floatsize;
  intargs=new RtInt[intsize];
  for(i=0;i<intsize;i++)
    intargs[i]=st.intargs[i];
  floatargs=new RtFloat[floatsize];
  for(i=0;i<floatsize;i++)
    floatargs[i]=st.floatargs[i];
}

GMANSubdivisionMesh::GMANSubdivisionMesh(RtToken schm, RtInt nf, RtInt nverts[],
					 RtInt verts[],
					 RtInt ntgs, RtToken tgs[], RtInt numargs[],
					 RtInt iargs[], RtFloat fargs[],
					 GMANParameterList p) : GMANPrimDatStorage(p)
{
  counter=new RtInt;
  *counter=1;

  int i,j,k;
  scheme=schm;
  nfaces=nf;
  nvertices=new RtInt[nfaces];
  for(i=0,j=0;i<nfaces;i++) {
    nvertices[i]=nverts[i];
    j+=nverts[i];
  }
  vertices=new RtInt[j];
  for(i=0;i<j;i++)
    vertices[i]=verts[i];

  ntags=ntgs;
  tags=new GMANSubdivisionTag* [ntags];
  for(i=j=k=0;i<ntags;i++) {
    tags[i]=new GMANSubdivisionTag(tgs[i],numargs[i*2],numargs[i*2+1],&iargs[j],&fargs[k]); 
    j+=numargs[i*2];
    k+=numargs[i*2+1];
  }
}

GMANSubdivisionMesh::GMANSubdivisionMesh(GMANSubdivisionMesh const &sbd) : GMANPrimDatStorage(sbd)
{
  copy(sbd);
}

GMANSubdivisionMesh const &GMANSubdivisionMesh::operator=(GMANSubdivisionMesh const &sbd)
{
  if (this!=&sbd) {
    destroy();
    GMANPrimDatStorage *a1=this;
    GMANPrimDatStorage const *a2=&sbd;
    *a1=*a2;
    copy(sbd);
  }
  return (*this);
}

GMANSubdivisionMesh::~GMANSubdivisionMesh()
{
  destroy ();
}

RtVoid GMANSubdivisionMesh::copy(GMANSubdivisionMesh const &sbd)
{
  counter=sbd.counter;
  *counter+=1;
  scheme=sbd.scheme;
  nfaces=sbd.nfaces;
  nvertices=sbd.nvertices;
  vertices=sbd.vertices;
  ntags=sbd.ntags;
  tags=sbd.tags;
}

RtVoid GMANSubdivisionMesh::destroy()
{
  if (!(*counter-=1)) {
    delete counter;
    delete [] nvertices;
    delete [] vertices;
    for(int i=0;i<ntags;i++)
      delete tags[i];
    delete [] tags;
  }
}






///////////////////////////////////////////////////////////////////////////////
////  GMAN_TORUS.CPP
///////////////////////////////////////////////////////////////////////////////
GMANPoint GMANTorus::getLocation (double u, double v)
{
  float theta = u * (thetamax / 360.0) * 2.0 * PI;
  float phi = (phimin + (phimax - phimin) * v) / 360.0 * 2.0 * PI;

  float factor = majorradius + minorradius * cos(phi);
  float x = factor * cos(theta);
  float y = factor * sin(theta);
  float z = minorradius * sin(phi);
  
  return GMANPoint(x, y, z);
}

GMANVector GMANTorus::getNormal (double u, double v)
{
  throw GMANError(RIE_SYSTEM, RIE_ERROR, "Not implemented");
}
