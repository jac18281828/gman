/* SPDX-License-Identifier: LGPL-2.1-or-later */

/*---------------------------------------------------------
  Copyright (C) Lionel Joseph Lacour 2000, 2001
  November 2000  First release

  April 2002:   JAC - major rewrite of this code.

  ---------------------------------------------------------
  4x4 matrix
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
*/

#include "gmanmatrix4.h"
#include "gmanvector.h"

GMANMatrix4::GMANMatrix4 ()
{
  identity();
}

GMANMatrix4::GMANMatrix4 (RtMatrix m)
{
    for(RtInt i=0;i<4;i++)
	for(RtInt j=0;j<4;j++)
	    mtrx[i][j] = m[i][j];
}

RtVoid GMANMatrix4::identity (RtVoid)
{
    for(RtInt i=0;i<4;i++)
	for(RtInt j=0;j<4;j++)
	    if(i==j) mtrx[i][j] = 1.0;
	    else mtrx[i][j] = 0.0;
}

GMANMatrix4 &GMANMatrix4::operator = (const GMANMatrix4 &m)
{
    for(RtInt i=0; i<4; i++) {
	for(RtInt j=0; j<4; j++) {
	    mtrx[i][j] = m[i][j];
	}
    }
    
    return *this;
}

RtVoid GMANMatrix4::concat (const GMANMatrix4 &m)
{
    *this *= m;
}

// near=1 far=+infinity (The RenderMan Companion)
RtVoid GMANMatrix4::persp (RtFloat fov)
{
    GMANMatrix4 m;
    RtFloat t=tan((fov/2)*DEGTORAD);
    m.mtrx[0][3]=t;
    m.mtrx[1][3]=-t;
    m.mtrx[2][3]=t;
    m.mtrx[3][3]=0.0;
    concat (m);
}

RtVoid GMANMatrix4::trans (RtFloat dx, RtFloat dy, RtFloat dz)
{
    GMANMatrix4 m;
    m.mtrx[3][0]=dx;
    m.mtrx[3][1]=dy;
    m.mtrx[3][2]=dz;
    concat(m);
}

RtVoid GMANMatrix4::rot (RtFloat angle, RtFloat dx, RtFloat dy, RtFloat dz)
{
    GMANMatrix4 m;
    RtFloat s,c,omc, xx,xy,xz,yy,yz,zz;
    
    // Normalize the axis
    float mag = sqrt(dx*dx + dy*dy + dz*dz);
    dx /= mag;
    dy /= mag;
    dz /= mag;
    
    s=sin(angle);
    c=cos(angle);
    omc=1-c;
    xx=dx*dx;
    xy=dx*dy;
    xz=dx*dz;
    yy=dy*dy;
    yz=dy*dz;
    zz=dz*dz;
    
    m.mtrx[0][0]=omc*xx+c;
    m.mtrx[1][0]=omc*xy-dz*s;
    m.mtrx[2][0]=omc*xz+dy*s;
    
    m.mtrx[0][1]=omc*xy+dz*s;
    m.mtrx[1][1]=omc*yy+c;
    m.mtrx[2][1]=omc*yz-dx*s;
    
    m.mtrx[0][2]=omc*xz-dy*s;
    m.mtrx[1][2]=omc*yz+dx*s;
    m.mtrx[2][2]=omc*zz+c;
    
    concat(m);
}

RtVoid GMANMatrix4::scale (RtFloat sx, RtFloat sy, RtFloat sz)
{
    GMANMatrix4 m;
    m.mtrx[0][0]=sx;
    m.mtrx[1][1]=sy;
    m.mtrx[2][2]=sz;
    concat(m);
}

RtVoid GMANMatrix4::skew (RtFloat angle, GMANVector &a, GMANVector &b)
{
    GMANMatrix4 m;
    GMANVector n1,n2;
    GMANVector a1,a2;
    RtFloat an1,an2;
    RtFloat rx,ry;
    RtFloat alpha;

    n2=b.normalize();
    a1=n2*a.dot(n2);
    a2=a-a1;
    n1=a2.normalize();
    
    an1=a.dot(n1);
    an2=a.dot(n2);
    
    rx=an1*cos(angle*DEGTORAD)-an2*sin(angle*DEGTORAD);
    ry=an1*sin(angle*DEGTORAD)+an2*cos(angle*DEGTORAD);

    if (rx<=0.0) {
	GMANError error(RIE_CONSISTENCY,RIE_ERROR,"Skew: angle too large");
	throw error;
    }
    
    // A parallel to B??
    if (an1==0) {
	alpha=0;
    } else {
	alpha=ry/rx-an2/an1;
    }
    
    m.mtrx[0][0]=n1.getX()*n2.getX()*alpha+1.0;
    m.mtrx[1][0]=n1.getY()*n2.getX()*alpha;
    m.mtrx[2][0]=n1.getZ()*n2.getX()*alpha;
    
    m.mtrx[0][1]=n1.getX()*n2.getY()*alpha;
    m.mtrx[1][1]=n1.getY()*n2.getY()*alpha+1.0;
    m.mtrx[2][1]=n1.getZ()*n2.getY()*alpha;
    
    m.mtrx[0][2]=n1.getX()*n2.getZ()*alpha;
    m.mtrx[1][2]=n1.getY()*n2.getZ()*alpha;
    m.mtrx[2][2]=n1.getZ()*n2.getZ()*alpha+1.0;
    
    concat(m);
}

// RiProjection perspective and orthographic matrix.
//
// Consumed via GMANVector4::projTransform / GMANPoint::operator*=, which
// both compute out[i] = row_i(M).(x,y,z) + M[i][3] -- the convention already
// in use for the projection stage (see persp(), above, which the RiPerspective
// transform command builds the same way). w is placed in row 3 so it carries
// z directly (RenderMan's camera looks down +z), never identically zero.
RtVoid GMANMatrix4::prjPersp (RtFloat fov, RtFloat nearDist, RtFloat farDist)
{
  RtFloat invT = 1.0/tan((fov/2)*DEGTORAD);

  identity();
  mtrx[0][0]=invT;
  mtrx[1][1]=invT;
  mtrx[2][2]=(farDist+nearDist)/(farDist-nearDist);
  mtrx[2][3]=-2.0*farDist*nearDist/(farDist-nearDist);
  mtrx[3][2]=1.0;
  mtrx[3][3]=0.0;
}

RtVoid GMANMatrix4::prjOrtho (RtFloat nearDist, RtFloat farDist)
{
  identity();
  mtrx[2][2]=2.0/(farDist-nearDist);
  mtrx[2][3]=-(farDist+nearDist)/(farDist-nearDist);
}

RtFloat GMANMatrix4::determinant ()
{
  RtFloat const (&m)[4][4] = mtrx;

  RtFloat a0 = m[0][0]*m[1][1] - m[0][1]*m[1][0];
  RtFloat a1 = m[0][0]*m[1][2] - m[0][2]*m[1][0];
  RtFloat a2 = m[0][0]*m[1][3] - m[0][3]*m[1][0];
  RtFloat a3 = m[0][1]*m[1][2] - m[0][2]*m[1][1];
  RtFloat a4 = m[0][1]*m[1][3] - m[0][3]*m[1][1];
  RtFloat a5 = m[0][2]*m[1][3] - m[0][3]*m[1][2];
  RtFloat b0 = m[2][0]*m[3][1] - m[2][1]*m[3][0];
  RtFloat b1 = m[2][0]*m[3][2] - m[2][2]*m[3][0];
  RtFloat b2 = m[2][0]*m[3][3] - m[2][3]*m[3][0];
  RtFloat b3 = m[2][1]*m[3][2] - m[2][2]*m[3][1];
  RtFloat b4 = m[2][1]*m[3][3] - m[2][3]*m[3][1];
  RtFloat b5 = m[2][2]*m[3][3] - m[2][3]*m[3][2];

  return a0*b5 - a1*b4 + a2*b3 + a3*b2 - a4*b1 + a5*b0;
}

// Adjugate / determinant, via the same 2x2-cofactor pairing determinant()
// uses. Throws on a singular matrix.
RtVoid GMANMatrix4::invert ()
{
  RtFloat const (&m)[4][4] = mtrx;

  RtFloat a0 = m[0][0]*m[1][1] - m[0][1]*m[1][0];
  RtFloat a1 = m[0][0]*m[1][2] - m[0][2]*m[1][0];
  RtFloat a2 = m[0][0]*m[1][3] - m[0][3]*m[1][0];
  RtFloat a3 = m[0][1]*m[1][2] - m[0][2]*m[1][1];
  RtFloat a4 = m[0][1]*m[1][3] - m[0][3]*m[1][1];
  RtFloat a5 = m[0][2]*m[1][3] - m[0][3]*m[1][2];
  RtFloat b0 = m[2][0]*m[3][1] - m[2][1]*m[3][0];
  RtFloat b1 = m[2][0]*m[3][2] - m[2][2]*m[3][0];
  RtFloat b2 = m[2][0]*m[3][3] - m[2][3]*m[3][0];
  RtFloat b3 = m[2][1]*m[3][2] - m[2][2]*m[3][1];
  RtFloat b4 = m[2][1]*m[3][3] - m[2][3]*m[3][1];
  RtFloat b5 = m[2][2]*m[3][3] - m[2][3]*m[3][2];

  RtFloat d = a0*b5 - a1*b4 + a2*b3 + a3*b2 - a4*b1 + a5*b0;
  if (d == 0.0) {
    GMANError error(RIE_MATH, RIE_ERROR, "Cannot invert matrix");
    throw error;
  }
  RtFloat id = 1.0/d;

  GMANMatrix4 inv;
  inv.mtrx[0][0] = ( m[1][1]*b5 - m[1][2]*b4 + m[1][3]*b3) * id;
  inv.mtrx[0][1] = (-m[0][1]*b5 + m[0][2]*b4 - m[0][3]*b3) * id;
  inv.mtrx[0][2] = ( m[3][1]*a5 - m[3][2]*a4 + m[3][3]*a3) * id;
  inv.mtrx[0][3] = (-m[2][1]*a5 + m[2][2]*a4 - m[2][3]*a3) * id;
  inv.mtrx[1][0] = (-m[1][0]*b5 + m[1][2]*b2 - m[1][3]*b1) * id;
  inv.mtrx[1][1] = ( m[0][0]*b5 - m[0][2]*b2 + m[0][3]*b1) * id;
  inv.mtrx[1][2] = (-m[3][0]*a5 + m[3][2]*a2 - m[3][3]*a1) * id;
  inv.mtrx[1][3] = ( m[2][0]*a5 - m[2][2]*a2 + m[2][3]*a1) * id;
  inv.mtrx[2][0] = ( m[1][0]*b4 - m[1][1]*b2 + m[1][3]*b0) * id;
  inv.mtrx[2][1] = (-m[0][0]*b4 + m[0][1]*b2 - m[0][3]*b0) * id;
  inv.mtrx[2][2] = ( m[3][0]*a4 - m[3][1]*a2 + m[3][3]*a0) * id;
  inv.mtrx[2][3] = (-m[2][0]*a4 + m[2][1]*a2 - m[2][3]*a0) * id;
  inv.mtrx[3][0] = (-m[1][0]*b3 + m[1][1]*b1 - m[1][2]*b0) * id;
  inv.mtrx[3][1] = ( m[0][0]*b3 - m[0][1]*b1 + m[0][2]*b0) * id;
  inv.mtrx[3][2] = (-m[3][0]*a3 + m[3][1]*a1 - m[3][2]*a0) * id;
  inv.mtrx[3][3] = ( m[2][0]*a3 - m[2][1]*a1 + m[2][2]*a0) * id;

  *this = inv;
}

// Row-vector (p*M) transform of a non-homogeneous point: dest_j =
// sum_i src_i*M[i][j] + M[3][j], matching trans()/rot()/scale()/concat(),
// which store translation in row 3. Divides by the resulting w so a
// (rare) projective matrix passed here still produces a valid point.
RtVoid GMANMatrix4::p3m(RtInt nbpts, RtFloat *src, RtFloat *dest)
{
  for (RtInt i=0;i<nbpts*3;i+=3) {
    RtFloat x=src[0+i], y=src[1+i], z=src[2+i];
    RtFloat rx = x*mtrx[0][0] + y*mtrx[1][0] + z*mtrx[2][0] + mtrx[3][0];
    RtFloat ry = x*mtrx[0][1] + y*mtrx[1][1] + z*mtrx[2][1] + mtrx[3][1];
    RtFloat rz = x*mtrx[0][2] + y*mtrx[1][2] + z*mtrx[2][2] + mtrx[3][2];
    RtFloat rw = x*mtrx[0][3] + y*mtrx[1][3] + z*mtrx[2][3] + mtrx[3][3];
    if (rw != 1.0 && rw != 0.0) {
      RtFloat invw = 1.0/rw;
      rx *= invw; ry *= invw; rz *= invw;
    }
    dest[0+i]=rx; dest[1+i]=ry; dest[2+i]=rz;
  }
}

// Homogeneous counterpart of p3m: carries w through unnormalized, for
// callers (e.g. normal transforms) that need the raw row-vector product
// rather than a perspective-divided point.
RtVoid GMANMatrix4::p4m(RtInt nbpts, RtFloat *src, RtFloat *dest)
{
  for (RtInt i=0;i<nbpts*4;i+=4) {
    RtFloat x=src[0+i], y=src[1+i], z=src[2+i], w=src[3+i];
    dest[0+i] = x*mtrx[0][0] + y*mtrx[1][0] + z*mtrx[2][0] + w*mtrx[3][0];
    dest[1+i] = x*mtrx[0][1] + y*mtrx[1][1] + z*mtrx[2][1] + w*mtrx[3][1];
    dest[2+i] = x*mtrx[0][2] + y*mtrx[1][2] + z*mtrx[2][2] + w*mtrx[3][2];
    dest[3+i] = x*mtrx[0][3] + y*mtrx[1][3] + z*mtrx[2][3] + w*mtrx[3][3];
  }
}
GMANMatrix4 GMANMatrix4::operator*(RtFloat f) const
{
  GMANMatrix4 res(*this);
  
  res *= f;

  return res;
}

GMANMatrix4 &GMANMatrix4::operator*=(RtFloat f) {
    for(RtInt i=0;i<4;i++)
	for(RtInt j=0;i<4;i++)
	  mtrx[i][j]*=f;

    return *this;
}

GMANMatrix4 GMANMatrix4::operator*(const GMANMatrix4 &m) const {
    GMANMatrix4 res(*this);

    res *= m;
    return res;
}


GMANMatrix4 &GMANMatrix4::operator*=(const GMANMatrix4 &m) {
    GMANMatrix4 res;

    for(RtInt i=0; i<4; i++) {
	for(RtInt j=0; j<4; j++) {
	    res[i][j] = 0.0;
	    for(RtInt k=0; k<4; k++) {
		res[i][j] += mtrx[i][k]*m[k][j];
	    }
	}
    }
    *this = res;
    return *this;
}

GMANMatrix4 GMANMatrix4::operator+(const GMANMatrix4 &m) const
{
  GMANMatrix4 res(*this);
  
  res += m;
  return res;
}

GMANMatrix4 &GMANMatrix4::operator+=(const GMANMatrix4 &m)
{
    for(RtInt i=0;i<4;i++) {
	for(RtInt j=0; j<4; j++) {
	    mtrx[i][j]+=m.mtrx[i][j];
	}
    }
    return *this;
}

RtVoid GMANMatrix4::setBasis(RtBasis &b) {
  for(RtInt i=0; i<4; i++) {
    for(RtInt j=0; j<4; j++) {
      mtrx[i][j] = b[i][j];
    }
  }
}


GMANMatrix4 &GMANMatrix4::assign(const GMANMatrix4 &m) {
	*this = m;
	return *this;
}
