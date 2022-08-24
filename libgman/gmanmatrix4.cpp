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

  To contact the author of GNU GMAN, write to John Cairns, 607 E STUART ST, 
  FT COLLINS, CO, 80525, USA, or write via E-mail john@2ad.com.
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
RtVoid GMANMatrix4::prjPersp (RtFloat fov, RtFloat nearDist, RtFloat farDist)
{
  RtFloat t=tan((fov/2)*DEGTORAD);
  RtFloat q=1.0-nearDist/farDist;

  identity();
  mtrx[0][3]=t/q;
  mtrx[1][3]=-nearDist*t/q;
  mtrx[2][3]=t;
  mtrx[3][3]=0.0;
}

RtVoid GMANMatrix4::prjOrtho (RtFloat nearDist, RtFloat farDist)
{
    identity();
    mtrx[0][3]=1.0/(farDist-nearDist);
    mtrx[1][3]=-nearDist/(farDist-nearDist);
}

#if 0
#define DET3(A,B,C,D,E,F,G,H,I) (+mtrx[A]*(mtrx[E]*mtrx[I]-mtrx[F]*mtrx[H]) \
				 -mtrx[B]*(mtrx[D]*mtrx[I]-mtrx[F]*mtrx[G]) \
				 +mtrx[C]*(mtrx[D]*mtrx[H]-mtrx[E]*mtrx[G]))

RtFloat GMANMatrix4::determinant ()
{
  return (+mtrx[0]*DET3(5,6,7,9,10,11,13,14,15)
	  -mtrx[1]*DET3(4,6,7,8,10,11,12,14,15)
	  +mtrx[2]*DET3(4,5,7,8,9,11,12,13,15)
	  -mtrx[3]*DET3(4,5,6,8,9,10,12,13,14));
}

#undef DET3
#define DET3(A,B,C,D,E,F,G,H,I) (+m.mtrx[A]*(m.mtrx[E]*m.mtrx[I]-m.mtrx[F]*m.mtrx[H]) \
				 -m.mtrx[B]*(m.mtrx[D]*m.mtrx[I]-m.mtrx[F]*m.mtrx[G]) \
				 +m.mtrx[C]*(m.mtrx[D]*m.mtrx[H]-m.mtrx[E]*m.mtrx[G]))

RtVoid GMANMatrix4::invert ()
{
  GMANMatrix4 m(*this);
  RtFloat d=determinant();
  if (d==0.0) {
    GMANError error(RIE_MATH, RIE_ERROR, "Cannot invert matrix");
    throw error;
  }

  d=1.0/d;
  mtrx[0]=d*(+DET3(5,6,7,9,10,11,13,14,15));
  mtrx[4]=d*(-DET3(4,6,7,8,10,11,12,14,15));
  mtrx[8]=d*(+DET3(4,5,7,8,9,11,12,13,15));
  mtrx[12]=d*(-DET3(4,5,6,8,9,10,12,13,14));
  mtrx[1]=d*(-DET3(1,2,3,9,10,11,13,14,15));
  mtrx[5]=d*(+DET3(0,2,3,8,10,11,12,14,15));
  mtrx[9]=d*(-DET3(0,1,3,8,9,11,12,13,15));
  mtrx[13]=d*(+DET3(0,1,2,8,9,10,12,13,14));
  mtrx[2]=d*(+DET3(1,2,3,5,6,7,13,14,15));
  mtrx[6]=d*(-DET3(0,2,3,4,6,7,12,14,15));
  mtrx[10]=d*(+DET3(0,1,3,4,5,7,12,13,15));
  mtrx[14]=d*(-DET3(0,1,2,4,5,6,12,13,14));
  mtrx[3]=d*(-DET3(1,2,3,5,6,7,9,10,11));
  mtrx[7]=d*(+DET3(0,2,3,4,6,7,8,10,11));
  mtrx[11]=d*(-DET3(0,1,3,4,5,7,8,9,11));
  mtrx[15]=d*(+DET3(0,1,2,4,5,6,8,9,10));
}

RtVoid GMANMatrix4::p3m(RtInt nbpts, RtFloat *src, RtFloat *dest)
{
  RtFloat w,k,l,m;

  for (RtInt i=0;i<nbpts*3;i+=3) {
    k=src[0+i]*mtrx[0]+src[1+i]*mtrx[1]+src[2+i]*mtrx[2]+mtrx[3];
    l=src[0+i]*mtrx[4]+src[1+i]*mtrx[5]+src[2+i]*mtrx[6]+mtrx[7];
    m=src[0+i]*mtrx[8]+src[1+i]*mtrx[9]+src[2+i]*mtrx[10]+mtrx[11];
    w=src[0+i]*mtrx[12]+src[1+i]*mtrx[13]+src[2+i]*mtrx[14]+mtrx[15];
    w=1.0/w;
    dest[0+i]=k*w; dest[1+i]=l*w; dest[2+i]=m*w;
  }
}

RtVoid GMANMatrix4::p4m(RtInt nbpts, RtFloat *src, RtFloat *dest)
{
  RtFloat w,k,l,m;

  for (RtInt i=0;i<nbpts*4;i+=4) {
    k=src[0+i]*mtrx[0]+src[1+i]*mtrx[1]+src[2+i]*mtrx[2]+src[3+i]*mtrx[3];
    l=src[0+i]*mtrx[4]+src[1+i]*mtrx[5]+src[2+i]*mtrx[6]+src[3+i]*mtrx[7];
    m=src[0+i]*mtrx[8]+src[1+i]*mtrx[9]+src[2+i]*mtrx[10]+src[3+i]*mtrx[11];
    w=src[0+i]*mtrx[12]+src[1+i]*mtrx[13]+src[2+i]*mtrx[14]+src[3+i]*mtrx[15];
    if (w!=1.0) {
      w=1.0/w;
      k*=w;
      l*=w;
      m*=w;
    }
    dest[0+i]=k;
    dest[1+i]=l;
    dest[2+i]=m;
    dest[3+i]=1.0;
  }
}

#endif
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
