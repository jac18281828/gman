/* This is part of the GNU GMAN Library, a FREE implementation of the
 * RenderMan Interface Specification.
 *
 * Copyright (c) 2001, 2000, 1999  John Cairns 
 *
 * Author: John Cairns <john@2ad.com>
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
  FT COLLINS, CO 80525, USA, or write via E-mail john@2ad.com.
*/

/* Local Headers */
#include "ri.h"      /* RenderMan Interface */
#include "universalsuperclass.h" /* Super class */
#include "gmanslapi.h" /* Declaration Header */
#include "gmanmatrix4.h"

/*
 * RenderMan SL API 
 *
 */

// LJL - February 2001
RtFloat GMANSmoothStep (RtFloat min, RtFloat max, RtFloat value)
{
  if (value < min) return 0.0;
  else if (value >= max) return 1.0;

  RtFloat u=(value-min)/(max-min);
  return (u*u*(3.0-2.0*u));
}

// LJL - March 2001
// distance, ptlined, rotate, faceforward, reflect, refract, fresnel
RtFloat GMANDistance (GMANPoint const &p1, GMANPoint const &p2)
{
  GMANVector res(p1,p2);
  return res.magnitude();
}

RtFloat GMANPTLined(const GMANPoint &p0, const GMANPoint &p1, const GMANPoint &q)
{
  GMANVector p0q(p0,q);
  if (p0==p1) return p0q.magnitude();

  RtFloat u;
  GMANVector p0p1(p0,p1);

  u= p0p1.dot(p0q) / p0p1.dot(p0p1);
  if (u<0) {
    GMANVector res(p0,q);
    return res.magnitude();
  } else if (u>1) {
    GMANVector res(p1,q);
    return res.magnitude();
  } else {
    GMANVector res(p1+p0p1*u,q);
    return res.magnitude();
  }
}

GMANPoint GMANRotate (const GMANPoint &q, RtFloat angle,
		  const GMANPoint &p1, const GMANPoint &p2)
{
  GMANMatrix4 m;
  GMANVector a(p1,p2);
  m.rot(angle, a.getX(), a.getY(), a.getZ());
  GMANVector b(p1,q);
  return p1+b*m;
}

GMANVector GMANFaceForward (const GMANVector &n, const GMANVector &i,
			const GMANVector &nr)
{
  if (nr.dot(i)<0) return n;
  else return -n;
}

GMANVector GMANReflect(const GMANVector &i, const GMANVector &n)
{
  return GMANVector(i-n*i.dot(n)*2);
}

GMANVector GMANRefract (const GMANVector &i, const GMANVector &n, RtFloat eta)
{
  RtFloat cphi=i.dot(n);
  RtFloat k=1-eta*eta*(1-cphi*cphi);
  if (k<0) {
    return GMANVector(0,0,0);
  } else return (i*eta-n*(eta*cphi+sqrt(k)));
}

RtVoid GMANFresnel (const GMANVector &i, const GMANVector &n, RtFloat eta,
		RtFloat &kr, RtFloat &kt)
{
  RtFloat cosphi=i.dot(n);
  RtFloat sinphi=sqrt(1-cosphi*cosphi);
  RtFloat sint=sinphi*eta;
  RtFloat cost=sqrt(1-sint*sint);

  RtFloat sinapb=sinphi*cost + sint*cosphi;
  RtFloat sinamb=sinphi*cost - sint*cosphi;
  RtFloat tanphi=sinphi/cosphi;
  RtFloat tant=sint/cost;
  RtFloat tanapb=(tanphi+tant)/(1-tanphi+tant);
  RtFloat tanamb=(tanphi-tant)/(1+tanphi+tant);

  kr=0.5*((sinamb*sinamb)/(sinapb*sinapb)+(tanamb*tanamb)/(tanapb*tanapb));
  kt=1-kr;
}

RtVoid GMANFresnel (const GMANVector &i, const GMANVector &n, RtFloat eta,
		RtFloat &kr, RtFloat &kt,
		GMANVector &r, GMANVector &t)
{
  RtFloat cosphi=i.dot(n);
  RtFloat sinphi=sqrt(1-cosphi*cosphi);
  RtFloat sint=sinphi*eta;
  RtFloat cost=sqrt(1-sint*sint);

  RtFloat sinapb=sinphi*cost + sint*cosphi;
  RtFloat sinamb=sinphi*cost - sint*cosphi;
  RtFloat tanphi=sinphi/cosphi;
  RtFloat tant=sint/cost;
  RtFloat tanapb=(tanphi+tant)/(1-tanphi+tant);
  RtFloat tanamb=(tanphi-tant)/(1+tanphi+tant);

  kr=0.5*((sinamb*sinamb)/(sinapb*sinapb)+(tanamb*tanamb)/(tanapb*tanapb));
  kt=1-kr;

  r=(i-n*cosphi*2);
  if (cost<0) {
    t=GMANVector(0,0,0);
  } else t = (i*eta-n*(eta*cosphi+cost));
}
