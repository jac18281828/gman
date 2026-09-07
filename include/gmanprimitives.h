/* SPDX-License-Identifier: LGPL-2.1-or-later */

/*---------------------------------------------------------
  Copyright (C) Lionel Joseph Lacour 2000, 2001
  December 2000  First release
  ---------------------------------------------------------
  Primitives datas storage.
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

#ifndef  __GMANGMANPRIMITIVES_H
#define  __GMANGMANPRIMITIVES_H 1

#include "ri.h"
#include "gmanparameterlist.h"
#include "gmanoptions.h"
#include "gmanattributes.h"
#include "gmanbasis.h"
#include "gmantransform.h"
#include "gmansegment.h"
#include "gmanlog.h"
#include "gmanpoint.h"
#include "gmanprimitive.h"


class GMAN_EXPORT GMANParametric : public virtual GMANPrimitive
{
 public:
  virtual GMANPoint getLocation (double u, double v) = 0;
  virtual GMANVector getNormal (double u, double v) = 0;
};


///////////////////////////////////////////////////////////////////////////////////////////////
////  GMAN__PRIMDATSTORAGE.HH
///////////////////////////////////////////////////////////////////////////////////////////////
class GMAN_EXPORT GMANPrimDatStorage : public virtual GMANPrimitive
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
class GMAN_EXPORT GMANBlobby : public GMANPrimDatStorage
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
  std::string *strings;
  
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
class GMAN_EXPORT GMANCone : public GMANPrimDatStorage, public GMANParametric
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
class GMAN_EXPORT GMANCurves : public GMANPrimDatStorage
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
class GMAN_EXPORT GMANCylinder : public GMANPrimDatStorage, public GMANParametric
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
class GMAN_EXPORT GMANDisk : public GMANPrimDatStorage, public GMANParametric
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
class GMAN_EXPORT GMANGeneralPolygon : public GMANPrimDatStorage
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
class GMAN_EXPORT GMANHyperboloid : public GMANPrimDatStorage, public GMANParametric
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
class GMAN_EXPORT GMANNuPatch : public GMANPrimDatStorage
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
class GMAN_EXPORT GMANParaboloid : public GMANPrimDatStorage, public GMANParametric
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
class GMAN_EXPORT GMANPatch : public GMANPrimDatStorage, public GMANParametric
{
protected:
  RtToken pt;
  bool bicubic;
  GMANPoint corner[4];

  // GMANBasis::bicubic reads one float past a 16-point, 3-float-per-point
  // array (stride-12-by-row indexing built through a 4-float-per-point
  // constructor); 49 rather than 48 absorbs that read without changing
  // gmanbasis.cpp.
  RtFloat cpts[49];
  GMANBasis basis;

public:
  // Bilinear: four control points, RiSpec order P(0,0) P(1,0) P(0,1) P(1,1).
  GMANPatch(RtToken pat, RtFloat *p, GMANParameterList pl);
  // Bicubic: sixteen control points, u-fastest/v-major, blended with b.
  GMANPatch(RtToken pat, RtFloat *p, GMANBasis const &b, GMANParameterList pl);

  GMANPoint getLocation (double u, double v);
  GMANVector getNormal (double u, double v);
};






///////////////////////////////////////////////////////////////////////////////////////////////
////  GMAN_PATCHMESH.HH
///////////////////////////////////////////////////////////////////////////////////////////////
class GMAN_EXPORT GMANPatchMesh : public GMANPrimDatStorage
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
class GMAN_EXPORT GMANPoints : public GMANPrimDatStorage
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
class GMAN_EXPORT GMANPointsGeneralPolygons : public GMANPrimDatStorage
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
class GMAN_EXPORT GMANPointsPolygons : public GMANPrimDatStorage
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
class GMAN_EXPORT GMANPolygon : public GMANPrimDatStorage
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
class GMAN_EXPORT GMANSphere : public GMANPrimDatStorage, public GMANParametric
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
class GMAN_EXPORT GMANSubdivisionTag
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


class GMAN_EXPORT GMANSubdivisionMesh : public GMANPrimDatStorage
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
class GMAN_EXPORT GMANTorus : public GMANPrimDatStorage, public GMANParametric
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
