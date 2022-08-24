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

#ifndef  __GMANGMANPRIMITIVES_H
#define  __GMANGMANPRIMITIVES_H 1

#include "ri.h"
#include "gmanparameterlist.h"
#include "gmanoptions.h"
#include "gmanattributes.h"
#include "gmantransform.h"
#include "gmansegment.h"
#include "universalsuperclass.h"
#include "gmanpoint.h"
#include "gmanprimitive.h"


class GMANDLL GMANParametric : public virtual GMANPrimitive
{
 public:
  virtual GMANPoint getLocation (double u, double v) = 0;
  virtual GMANVector getNormal (double u, double v) = 0;
};


///////////////////////////////////////////////////////////////////////////////////////////////
////  GMAN__PRIMDATSTORAGE.HH
///////////////////////////////////////////////////////////////////////////////////////////////
class GMANDLL GMANPrimDatStorage : public virtual GMANPrimitive
{
protected:
  GMANParameterList pl;
public:
  GMANPrimDatStorage(GMANParameterList p) : pl(p) {};
  ~GMANPrimDatStorage() {};
};






///////////////////////////////////////////////////////////////////////////////////////////////
////  GMAN_BLOBBY.HH
///////////////////////////////////////////////////////////////////////////////////////////////
class GMANDLL GMANBlobby : public GMANPrimDatStorage
{
private:
  int *counter;
  RtVoid copy(GMANBlobby const &b);
  RtVoid destroy();

protected:
  RtInt nleaf;
  RtInt ncode;
  RtInt *code;
  RtInt nfloat;
  RtFloat *floats;
  RtInt nstrings;
  string *strings;
  
public:
  GMANBlobby(RtInt nlf, RtInt ncd, RtInt cd[],
	     RtInt nf, RtFloat f[],
	     RtInt ns, RtString s[], GMANParameterList p);
  GMANBlobby(GMANBlobby const &b);
  ~GMANBlobby();
  GMANBlobby const &operator=(GMANBlobby const &b);
};






///////////////////////////////////////////////////////////////////////////////////////////////
////  GMAN_CONE.HH
///////////////////////////////////////////////////////////////////////////////////////////////
class GMANDLL GMANCone : public GMANPrimDatStorage, public GMANParametric
{
protected:
  RtFloat height;
  RtFloat radius;
  RtFloat thetamax;

public:
  GMANCone(RtFloat h, RtFloat rad, RtFloat theta, GMANParameterList p)
    : GMANPrimDatStorage(p), height(h), radius(rad), thetamax(theta) {}
  GMANPoint getLocation (double u, double v);
  GMANVector getNormal (double u, double v);
};






///////////////////////////////////////////////////////////////////////////////////////////////
////  GMAN_CURVES.HH
///////////////////////////////////////////////////////////////////////////////////////////////
class GMANDLL GMANCurves : public GMANPrimDatStorage
{
private:
  RtVoid copy(GMANCurves const &c);
protected:
  RtToken type;
  RtInt ncurves;
  RtInt *nvertices;
  RtToken wrap;

public:
  GMANCurves(RtToken type, RtInt ncurves, RtInt *nvertices, RtToken wrap,
	     GMANParameterList p);
  GMANCurves(GMANCurves const &c);
  GMANCurves const &operator=(GMANCurves const &c);
  ~GMANCurves();
};






///////////////////////////////////////////////////////////////////////////////////////////////
////  GMAN_CYLINDER.HH
///////////////////////////////////////////////////////////////////////////////////////////////
class GMANDLL GMANCylinder : public GMANPrimDatStorage, public GMANParametric
{
protected:
  RtFloat radius;
  RtFloat zmin;
  RtFloat zmax;
  RtFloat thetamax;

public:
  GMANCylinder(RtFloat rad, RtFloat zmn, RtFloat zmx, RtFloat theta,
	       GMANParameterList p)
    : GMANPrimDatStorage(p), radius(rad), zmin(zmn), zmax(zmx), thetamax(theta) {}
  GMANPoint getLocation (double u, double v);
  GMANVector getNormal (double u, double v);
};






///////////////////////////////////////////////////////////////////////////////////////////////
////  GMAN_DISK.HH
///////////////////////////////////////////////////////////////////////////////////////////////
class GMANDLL GMANDisk : public GMANPrimDatStorage, public GMANParametric
{
protected:
  RtFloat height;
  RtFloat radius;
  RtFloat thetamax;

public:
  GMANDisk(RtFloat h, RtFloat rad, RtFloat theta, GMANParameterList p)
    : GMANPrimDatStorage(p), height(h), radius(rad), thetamax(theta) {}
  GMANPoint getLocation (double u, double v);
  GMANVector getNormal (double u, double v);
};






///////////////////////////////////////////////////////////////////////////////////////////////
////  GMAN_GENERALPOLYGON.HH
///////////////////////////////////////////////////////////////////////////////////////////////
class GMANDLL GMANGeneralPolygon : public GMANPrimDatStorage
{
private:
  RtVoid copy(GMANGeneralPolygon const &gp);
protected:
  RtInt nloop;
  RtInt *nvert;

public:
  GMANGeneralPolygon(RtInt,RtInt[],
		     GMANParameterList p);
  GMANGeneralPolygon(GMANGeneralPolygon const &);
  GMANGeneralPolygon const &operator=(GMANGeneralPolygon const &);
  ~GMANGeneralPolygon();
};






///////////////////////////////////////////////////////////////////////////////////////////////
////  GMAN_HYPERBOLOID.HH
///////////////////////////////////////////////////////////////////////////////////////////////
class GMANDLL GMANHyperboloid : public GMANPrimDatStorage, public GMANParametric
{
protected:
  GMANPoint point1;
  GMANPoint point2;
  RtFloat thetamax;

public:
  GMANHyperboloid(RtPoint p1, RtPoint p2, RtFloat theta,
		  GMANParameterList p)
    : GMANPrimDatStorage(p), point1(p1), point2(p2), thetamax(theta) {}
  GMANPoint getLocation (double u, double v);
  GMANVector getNormal (double u, double v);
};






///////////////////////////////////////////////////////////////////////////////////////////////
////  GMAN_NUPATCH.HH
///////////////////////////////////////////////////////////////////////////////////////////////
class GMANDLL GMANNuPatch : public GMANPrimDatStorage
{
private:
  RtVoid copy(GMANNuPatch const &np);
  RtVoid destroy ();
protected:
  RtInt nu;
  RtInt uorder;
  RtFloat *uknot;
  RtFloat umin;
  RtFloat umax;
  RtInt nv;
  RtInt vorder;
  RtFloat *vknot;
  RtFloat vmin;
  RtFloat vmax;

public:
  GMANNuPatch(RtInt nu, RtInt uorder, RtFloat uknot[], RtFloat umin, RtFloat umax,
	      RtInt nv, RtInt vorder, RtFloat vknot[], RtFloat vmin, RtFloat vmax,
	      GMANParameterList p);
  GMANNuPatch(GMANNuPatch const &np);
  GMANNuPatch const &operator=(GMANNuPatch const &np);
  ~GMANNuPatch();
};






///////////////////////////////////////////////////////////////////////////////////////////////
////  GMAN_PARABOLOID.HH
///////////////////////////////////////////////////////////////////////////////////////////////
class GMANDLL GMANParaboloid : public GMANPrimDatStorage, public GMANParametric
{
protected:
  RtFloat rmax;
  RtFloat zmin;
  RtFloat zmax;
  RtFloat thetamax;

public:
  GMANParaboloid(RtFloat rmx, RtFloat zmn, RtFloat zmx, RtFloat theta,
		 GMANParameterList p)
    : GMANPrimDatStorage(p), rmax(rmx), zmin(zmn), zmax(zmx), thetamax(theta) {}
  GMANPoint getLocation (double u, double v);
  GMANVector getNormal (double u, double v);
};






///////////////////////////////////////////////////////////////////////////////////////////////
////  GMAN_PATCH.HH
///////////////////////////////////////////////////////////////////////////////////////////////
class GMANDLL GMANPatch : public GMANPrimDatStorage
{
protected:
  RtToken pt;

public:
  GMANPatch(RtToken, GMANParameterList p);
};






///////////////////////////////////////////////////////////////////////////////////////////////
////  GMAN_PATCHMESH.HH
///////////////////////////////////////////////////////////////////////////////////////////////
class GMANDLL GMANPatchMesh : public GMANPrimDatStorage
{
protected:
  RtToken pt;
  RtInt nu;
  RtToken uwrap;
  RtInt nv;
  RtToken vwrap;

public:
  GMANPatchMesh(RtToken pat, RtInt u, RtToken uw, RtInt v, RtToken vw, 
		GMANParameterList p);
};






///////////////////////////////////////////////////////////////////////////////////////////////
////  GMAN_POINTS.HH
///////////////////////////////////////////////////////////////////////////////////////////////
class GMANDLL GMANPoints : public GMANPrimDatStorage
{
protected:
  RtInt npoints;

public:
  GMANPoints(RtInt npts, GMANParameterList p)
    : GMANPrimDatStorage(p), npoints(npts) {}
};






///////////////////////////////////////////////////////////////////////////////////////////////
////  GMAN_POINTSGENERALPOLYGONS.HH
///////////////////////////////////////////////////////////////////////////////////////////////
class GMANDLL GMANPointsGeneralPolygons : public GMANPrimDatStorage
{
private:
  RtVoid copy(GMANPointsGeneralPolygons const &pgp);
  RtVoid destroy();
protected:
  RtInt npolys;
  RtInt *nloops;
  RtInt *nvertices;
  RtInt *vertices;

public:
  GMANPointsGeneralPolygons(RtInt, RtInt[], RtInt[], RtInt[],
			    GMANParameterList p);
  GMANPointsGeneralPolygons(GMANPointsGeneralPolygons const &);
  GMANPointsGeneralPolygons const &operator=(GMANPointsGeneralPolygons const &);
  ~GMANPointsGeneralPolygons();
};






///////////////////////////////////////////////////////////////////////////////////////////////
////  GMAN_POINTSPOLYGONS.HH
///////////////////////////////////////////////////////////////////////////////////////////////
class GMANDLL GMANPointsPolygons : public GMANPrimDatStorage
{
private:
  RtVoid copy(GMANPointsPolygons const &pp);
  RtVoid destroy();
protected:
  RtInt npolys;
  RtInt *nvertices;
  RtInt *vertices;

public:
  GMANPointsPolygons(RtInt,RtInt[],RtInt[],
		     GMANParameterList p);
  GMANPointsPolygons(GMANPointsPolygons const &);
  GMANPointsPolygons const &operator=(GMANPointsPolygons const &);
  ~GMANPointsPolygons();
};






///////////////////////////////////////////////////////////////////////////////////////////////
////  GMAN_POLYGON.HH
///////////////////////////////////////////////////////////////////////////////////////////////
class GMANDLL GMANPolygon : public GMANPrimDatStorage
{
protected:
  RtInt nvert;

public:
  GMANPolygon(RtInt n, GMANParameterList p)
    : GMANPrimDatStorage(p), nvert(n) {}
};






///////////////////////////////////////////////////////////////////////////////////////////////
////  GMAN_SPHERE.HH
///////////////////////////////////////////////////////////////////////////////////////////////
class GMANDLL GMANSphere : public GMANPrimDatStorage, public GMANParametric
{
protected:
  RtFloat radius;
  RtFloat zmin;
  RtFloat zmax;
  RtFloat thetamax;

public:
  GMANSphere(RtFloat rad, RtFloat zmn, RtFloat zmx, RtFloat theta,
	     GMANParameterList p)
    : GMANPrimDatStorage(p), radius(rad), zmin(zmn), zmax(zmx), thetamax(theta) {}
  GMANPoint getLocation (double u, double v);
  GMANVector getNormal (double u, double v);
};





///////////////////////////////////////////////////////////////////////////////////////////////
////  GMAN_SUBDIVISIONMESH.HH
///////////////////////////////////////////////////////////////////////////////////////////////
class GMANDLL GMANSubdivisionTag
{
private:
  RtToken tag;
  RtInt intsize,floatsize;
  RtInt *intargs;
  RtFloat *floatargs;

  RtVoid copy(GMANSubdivisionTag const& st);

  GMANSubdivisionTag(RtToken t, RtInt isize, RtInt fsize, RtInt *iargs, RtFloat *fargs);
  GMANSubdivisionTag(GMANSubdivisionTag const &st);
  GMANSubdivisionTag const &operator=(GMANSubdivisionTag const &st);
  ~GMANSubdivisionTag();
  
  friend class GMANSubdivisionMesh;
};


class GMANDLL GMANSubdivisionMesh : public GMANPrimDatStorage
{
private:
  RtInt *counter;
  RtVoid copy(GMANSubdivisionMesh const &sbd);
  RtVoid destroy();
protected:
  RtToken scheme;

  RtInt nfaces;
  RtInt *nvertices;
  RtInt *vertices;

  RtInt ntags;
  GMANSubdivisionTag **tags;

public:
  GMANSubdivisionMesh(RtToken schm, RtInt nf, RtInt nverts[],
		      RtInt verts[],
		      RtInt ntgs, RtToken tgs[], RtInt numargs[],
		      RtInt iargs[], RtFloat fltargs[],
		      GMANParameterList p);
  GMANSubdivisionMesh(GMANSubdivisionMesh const &sbd);
  GMANSubdivisionMesh const &operator=(GMANSubdivisionMesh const &sbd);
  ~GMANSubdivisionMesh();
};






///////////////////////////////////////////////////////////////////////////////////////////////
////  GMAN_TORUS.HH
///////////////////////////////////////////////////////////////////////////////////////////////
class GMANDLL GMANTorus : public GMANPrimDatStorage, public GMANParametric
{
protected:
  RtFloat majorradius;
  RtFloat minorradius;
  RtFloat phimin;
  RtFloat phimax;
  RtFloat thetamax;

public:
  GMANTorus(RtFloat mjrrad, RtFloat mnrrad,
	    RtFloat phimn, RtFloat phimx, RtFloat theta,
	    GMANParameterList p)
    : GMANPrimDatStorage(p), majorradius(mjrrad), minorradius(mnrrad),
    phimin(phimn), phimax(phimx), thetamax(theta) {}
  GMANPoint getLocation (double u, double v);
  GMANVector getNormal (double u, double v);
};






#endif
