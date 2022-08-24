/*----------------------------------------------------------
  Copyright (C) Lionel Joseph Lacour 2001, 2002
  February 2001 First release
  ----------------------------------------------------------
  This code based on Ken Perlin gradient noise function.
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

#include <stdlib.h>
#include "gmannoise.h"
#include "gmanvector4.h"
#include "gmanmath.h"


float GMANNoise::rn()
{
  return 1.0-2.0*(rand()/(RAND_MAX+0.0));
}

GMANNoise::GMANNoise()
{
  prn=prn1;
  cprn=cprn1;

  srand(808);
  RtInt i,t,u;
  for (i=0; i<N ; i++) {
    prn1[i]=i;
    prn2[i]=i;
    prn3[i]=i;
  }
  for (i=0; i<N; i++) {
    t=prn1[i];
    u=(RtInt) (N*(rand()/(RAND_MAX+1.0)));
    prn1[i]=prn1[u];
    prn1[u]=t;
  }

  for (i=0; i<N; i++) {
    t=prn2[i];
    u=(RtInt) (N*(rand()/(RAND_MAX+1.0)));
    prn2[i]=prn2[u];
    prn2[u]=t;
  }

  for (i=0; i<N; i++) {
    t=prn3[i];
    u=(RtInt) (N*(rand()/(RAND_MAX+1.0)));
    prn3[i]=prn3[u];
    prn3[u]=t;
  }

  i=0;
  while (i != N*4 ) {
    GMANVector4 a(rn(),rn(),rn(),rn());
    if ( a.magnitude() >1.0 || a.magnitude() <0.1) continue;
    a.normalize();
    vect[i+0]=a.getX();
    vect[i+1]=a.getY();
    vect[i+2]=a.getZ();
    vect[i+3]=a.getW();
    i+=4;
  }


  for (i=0; i<CN ; i++) {
    cprn1[i]=i;
    cprn2[i]=i;
    cprn3[i]=i;
  }
  for (i=0; i<CN; i++) {
    t=cprn1[i];
    u=(RtInt) (CN*(rand()/(RAND_MAX+1.0)));
    cprn1[i]=cprn1[u];
    cprn1[u]=t;
  }

  for (i=0; i<CN; i++) {
    t=cprn2[i];
    u=(RtInt) (CN*(rand()/(RAND_MAX+1.0)));
    cprn2[i]=cprn2[u];
    cprn2[u]=t;
  }

  for (i=0; i<CN; i++) {
    t=cprn3[i];
    u=(RtInt) (CN*(rand()/(RAND_MAX+1.0)));
    cprn3[i]=cprn3[u];
    cprn3[u]=t;
  }

  for (i=0; i<CN; i++) {
    cvect[i]=rn();
  }
}

#define SMOOTH(x) (x*x*(3.0-2.0*x))
#define LERP(x,a,b) (a+x*(b-a))
#define RND1(x) prn[(x)&MASK]
#define RND2(x,y) prn[(prn[(x)&MASK]+y)&MASK]
#define RND3(x,y,z) prn[(prn[(prn[(x)&MASK]+y)&MASK]+z)&MASK]
#define RND4(x,y,z,w) prn[(prn[(prn[(prn[(x)&MASK]+y)&MASK]+z)&MASK]+w)&MASK]

/* 1D noise */
RtFloat GMANNoise::noise (RtFloat v)
{
  RtInt i=(RtInt) floor(v);

  RtFloat f0,f1,st;
  f0=v-i;
  f1=f0-1.0;
  st=SMOOTH(f0);

  f0*=vect[4*RND1(i)];
  f1*=vect[4*RND1(i+1)];
  return 0.5+0.5*LERP(st,f0,f1);
}

/* 2D noise */
RtFloat GMANNoise::noise (RtFloat u, RtFloat v)
{
  RtInt i=(RtInt) floor(u);
  RtInt j=(RtInt) floor(v);

  RtFloat fu0,fu1,stu;
  fu0=u-i;
  fu1=fu0-1.0;
  stu=SMOOTH(fu0);

  RtFloat fv0,fv1,stv;
  fv0=v-j;
  fv1=fv0-1.0;
  stv=SMOOTH(fv0);

  RtInt id0=4*RND2(i,j);
  RtInt id1=4*RND2(i+1,j);
  RtInt id2=4*RND2(i,j+1);
  RtInt id3=4*RND2(i+1,j+1);
 
  RtFloat t1,t2,t3,t4;
  t1=fu0*vect[id0] + fv0*vect[id0+1];
  t2=fu1*vect[id1] + fv0*vect[id1+1];
  t3=fu0*vect[id2] + fv1*vect[id2+1];
  t4=fu1*vect[id3] + fv1*vect[id3+1];

  RtFloat a,b;
  a=LERP(stu,t1,t2);
  b=LERP(stu,t3,t4);
  return 0.5+0.5*LERP(stv,a,b);
}

/* 3D noise */
RtFloat GMANNoise::noise (GMANPoint const &p)
{
  RtFloat u=p.getX();
  RtFloat v=p.getY();
  RtFloat w=p.getZ();

  RtInt i=(RtInt) floor(u);
  RtInt j=(RtInt) floor(v);
  RtInt k=(RtInt) floor(w);

  RtFloat fu0,fu1,stu;
  fu0=u-i;
  fu1=fu0-1.0;
  stu=SMOOTH(fu0);

  RtFloat fv0,fv1,stv;
  fv0=v-j;
  fv1=fv0-1.0;
  stv=SMOOTH(fv0);

  RtFloat fw0,fw1,stw;
  fw0=w-k;
  fw1=fw0-1.0;
  stw=SMOOTH(fw0);

  RtInt id0=4*RND3(i,j,k);
  RtInt id1=4*RND3(i+1,j,k);
  RtInt id2=4*RND3(i,j+1,k);
  RtInt id3=4*RND3(i+1,j+1,k);
  RtInt id4=4*RND3(i,j,k+1);
  RtInt id5=4*RND3(i+1,j,k+1);
  RtInt id6=4*RND3(i,j+1,k+1);
  RtInt id7=4*RND3(i+1,j+1,k+1);
  
  RtFloat t1,t2,t3,t4,t5,t6,t7,t8;
  t1=fu0*vect[id0] + fv0*vect[id0+1] + fw0*vect[id0+2];
  t2=fu1*vect[id1] + fv0*vect[id1+1] + fw0*vect[id1+2];
  t3=fu0*vect[id2] + fv1*vect[id2+1] + fw0*vect[id2+2];
  t4=fu1*vect[id3] + fv1*vect[id3+1] + fw0*vect[id3+2];

  t5=fu0*vect[id4] + fv0*vect[id4+1] + fw1*vect[id4+2];
  t6=fu1*vect[id5] + fv0*vect[id5+1] + fw1*vect[id5+2];
  t7=fu0*vect[id6] + fv1*vect[id6+1] + fw1*vect[id6+2];
  t8=fu1*vect[id7] + fv1*vect[id7+1] + fw1*vect[id7+2];

  RtFloat a,b,c,d,e,f;
  a=LERP(stu,t1,t2);
  b=LERP(stu,t3,t4);
  c=LERP(stu,t5,t6);
  d=LERP(stu,t7,t8);

  e=LERP(stv,a,b);
  f=LERP(stv,c,d);
  return 0.5+0.5*LERP(stw,e,f);
}

/* 4D noise */
RtFloat GMANNoise::noise (GMANPoint const &p, RtFloat t)
{
  RtFloat u=p.getX();
  RtFloat v=p.getY();
  RtFloat w=p.getZ();

  RtInt i=(RtInt) floor(u);
  RtInt j=(RtInt) floor(v);
  RtInt k=(RtInt) floor(w);
  RtInt l=(RtInt) floor(t);

  RtFloat fu0,fu1,stu;
  fu0=u-i;
  fu1=fu0-1.0;
  stu=SMOOTH(fu0);

  RtFloat fv0,fv1,stv;
  fv0=v-j;
  fv1=fv0-1.0;
  stv=SMOOTH(fv0);

  RtFloat fw0,fw1,stw;
  fw0=w-k;
  fw1=fw0-1.0;
  stw=SMOOTH(fw0);

  RtFloat ft0,ft1,stt;
  ft0=t-l;
  ft1=ft0-1.0;
  stt=SMOOTH(ft0);

  RtInt id0=4*RND4(i,j,k,l);
  RtInt id1=4*RND4(i+1,j,k,l);
  RtInt id2=4*RND4(i,j+1,k,l);
  RtInt id3=4*RND4(i+1,j+1,k,l);
  RtInt id4=4*RND4(i,j,k+1,l);
  RtInt id5=4*RND4(i+1,j,k+1,l);
  RtInt id6=4*RND4(i,j+1,k+1,l);
  RtInt id7=4*RND4(i+1,j+1,k+1,l);

  RtInt id8=4*RND4(i,j,k,l+1);
  RtInt id9=4*RND4(i+1,j,k,l+1);
  RtInt id10=4*RND4(i,j+1,k,l+1);
  RtInt id11=4*RND4(i+1,j+1,k,l+1);
  RtInt id12=4*RND4(i,j,k+1,l+1);
  RtInt id13=4*RND4(i+1,j,k+1,l+1);
  RtInt id14=4*RND4(i,j+1,k+1,l+1);
  RtInt id15=4*RND4(i+1,j+1,k+1,l+1);

  RtFloat t1,t2,t3,t4,t5,t6,t7,t8;
  RtFloat t9,t10,t11,t12,t13,t14,t15,t16;

  t1=fu0*vect[id0] + fv0*vect[id0+1] + fw0*vect[id0+2] + ft0*vect[id0+3];
  t2=fu1*vect[id1] + fv0*vect[id1+1] + fw0*vect[id1+2] + ft0*vect[id1+3];
  t3=fu0*vect[id2] + fv1*vect[id2+1] + fw0*vect[id2+2] + ft0*vect[id2+3];
  t4=fu1*vect[id3] + fv1*vect[id3+1] + fw0*vect[id3+2] + ft0*vect[id3+3];

  t5=fu0*vect[id4] + fv0*vect[id4+1] + fw1*vect[id4+2] + ft0*vect[id4+3];
  t6=fu1*vect[id5] + fv0*vect[id5+1] + fw1*vect[id5+2] + ft0*vect[id5+3];
  t7=fu0*vect[id6] + fv1*vect[id6+1] + fw1*vect[id6+2] + ft0*vect[id6+3];
  t8=fu1*vect[id7] + fv1*vect[id7+1] + fw1*vect[id7+2] + ft0*vect[id7+3];

  t9=fu0*vect[id8] + fv0*vect[id8+1] + fw0*vect[id8+2] + ft1*vect[id8+3];
  t10=fu1*vect[id9] + fv0*vect[id9+1] + fw0*vect[id9+2] + ft1*vect[id9+3];
  t11=fu0*vect[id10] + fv1*vect[id10+1] + fw0*vect[id10+2] + ft1*vect[id10+3];
  t12=fu1*vect[id11] + fv1*vect[id11+1] + fw0*vect[id11+2] + ft1*vect[id11+3];

  t13=fu0*vect[id12] + fv0*vect[id12+1] + fw1*vect[id12+2] + ft1*vect[id12+3];
  t14=fu1*vect[id13] + fv0*vect[id13+1] + fw1*vect[id13+2] + ft1*vect[id13+3];
  t15=fu0*vect[id14] + fv1*vect[id14+1] + fw1*vect[id14+2] + ft1*vect[id14+3];
  t16=fu1*vect[id15] + fv1*vect[id15+1] + fw1*vect[id15+2] + ft1*vect[id15+3];

  RtFloat a,b,c,d,e,f;
  RtFloat aa,bb,cc,dd,ee,ff;
  a=LERP(stu,t1,t2);
  b=LERP(stu,t3,t4);
  c=LERP(stu,t5,t6);
  d=LERP(stu,t7,t8);

  e=LERP(stv,a,b);
  f=LERP(stv,c,d);
  RtFloat g=LERP(stw,e,f);

  aa=LERP(stu,t9,t10);
  bb=LERP(stu,t11,t12);
  cc=LERP(stu,t13,t14);
  dd=LERP(stu,t15,t16);

  ee=LERP(stv,aa,bb);
  ff=LERP(stv,cc,dd);
  RtFloat h=LERP(stw,ee,ff);
  return 0.5+0.5*LERP(stt,g,h);
}

     /****************************************/
    /* Point, vector, color noise functions */
   /****************************************/
  /* I just change the prn table to get a */
 /* different value in x,y and z         */
/****************************************/
RtVoid GMANNoise::noise (RtFloat v, RtFloat &a, RtFloat &b, RtFloat &c)
{
  a=noise(v);
  prn=prn2;
  b=noise(v);
  prn=prn3;
  c=noise(v);
  prn=prn1;
}

RtVoid GMANNoise::noise (RtFloat u, RtFloat v, RtFloat &a, RtFloat &b, RtFloat &c)
{
  a=noise(u,v);
  prn=prn2;
  b=noise(u,v);
  prn=prn3;
  c=noise(u,v);
  prn=prn1;
}

RtVoid GMANNoise::noise (GMANPoint const &p, RtFloat &a, RtFloat &b, RtFloat &c)
{
  a=noise(p);
  prn=prn2;
  b=noise(p);
  prn=prn3;
  c=noise(p);
  prn=prn1;
}

RtVoid GMANNoise::noise (GMANPoint const &p, RtFloat t, RtFloat &a, RtFloat &b, RtFloat &c)
{
  a=noise(p,t);
  prn=prn2;
  b=noise(p,t);
  prn=prn3;
  c=noise(p,t);
  prn=prn1;
}


  /****************************/
 /* Periodic noise functions */
/****************************/
RtFloat GMANNoise::periodic (RtFloat v, RtFloat pv)
{
  v=GMANMod(v,pv);
  RtInt i=(RtInt) floor(v);

  RtFloat f0,f1,st;
  f0=v-i;
  f1=f0-1.0;
  st=SMOOTH(f0);

  f0*=vect[4*RND1(i)];
  if (i==(pv-1)) i=0; else i++;
  f1*=vect[4*RND1(i)];
  return 0.5+0.5*LERP(st,f0,f1);
}
RtFloat GMANNoise::periodic (RtFloat u, RtFloat v, RtFloat pu, RtFloat pv)
{
  u=GMANMod(u,pu);
  v=GMANMod(v,pv);
  RtInt i=(RtInt) floor(u);
  RtInt j=(RtInt) floor(v);

  RtFloat fu0,fu1,stu;
  fu0=u-i;
  fu1=fu0-1.0;
  stu=SMOOTH(fu0);

  RtFloat fv0,fv1,stv;
  fv0=v-j;
  fv1=fv0-1.0;
  stv=SMOOTH(fv0);

  RtInt wi,wj;
  if (i==(pu-1)) wi=0; else wi=i+1;
  if (j==(pv-1)) wj=0; else wj=j+1;
  RtInt id0=4*RND2(i ,j);
  RtInt id1=4*RND2(wi,j);
  RtInt id2=4*RND2(i ,wj);
  RtInt id3=4*RND2(wi,wj);
 
  RtFloat t1,t2,t3,t4;
  t1=fu0*vect[id0] + fv0*vect[id0+1];
  t2=fu1*vect[id1] + fv0*vect[id1+1];
  t3=fu0*vect[id2] + fv1*vect[id2+1];
  t4=fu1*vect[id3] + fv1*vect[id3+1];

  RtFloat a,b;
  a=LERP(stu,t1,t2);
  b=LERP(stu,t3,t4);
  return 0.5+0.5*LERP(stv,a,b);
}
RtFloat GMANNoise::periodic (GMANPoint const &p, GMANPoint const &pp)
{
  RtFloat pu=pp.getX();
  RtFloat pv=pp.getY();
  RtFloat pw=pp.getZ();
  RtFloat u=GMANMod(p.getX(),pu);
  RtFloat v=GMANMod(p.getY(),pv);
  RtFloat w=GMANMod(p.getZ(),pw);

  RtInt i=(RtInt) floor(u);
  RtInt j=(RtInt) floor(v);
  RtInt k=(RtInt) floor(w);

  RtFloat fu0,fu1,stu;
  fu0=u-i;
  fu1=fu0-1.0;
  stu=SMOOTH(fu0);

  RtFloat fv0,fv1,stv;
  fv0=v-j;
  fv1=fv0-1.0;
  stv=SMOOTH(fv0);

  RtFloat fw0,fw1,stw;
  fw0=w-k;
  fw1=fw0-1.0;
  stw=SMOOTH(fw0);

  RtInt wi,wj,wk;
  if (i==(pu-1)) wi=0; else wi=i+1;
  if (j==(pv-1)) wj=0; else wj=j+1;
  if (k==(pw-1)) wk=0; else wk=k+1;
  RtInt id0=4*RND3(i ,j ,k );
  RtInt id1=4*RND3(wi,j ,k );
  RtInt id2=4*RND3(i ,wj,k );
  RtInt id3=4*RND3(wi,wj,k );
  RtInt id4=4*RND3(i ,j ,wk);
  RtInt id5=4*RND3(wi,j ,wk);
  RtInt id6=4*RND3(i ,wj,wk);
  RtInt id7=4*RND3(wi,wj,wk);
  
  RtFloat t1,t2,t3,t4,t5,t6,t7,t8;
  t1=fu0*vect[id0] + fv0*vect[id0+1] + fw0*vect[id0+2];
  t2=fu1*vect[id1] + fv0*vect[id1+1] + fw0*vect[id1+2];
  t3=fu0*vect[id2] + fv1*vect[id2+1] + fw0*vect[id2+2];
  t4=fu1*vect[id3] + fv1*vect[id3+1] + fw0*vect[id3+2];

  t5=fu0*vect[id4] + fv0*vect[id4+1] + fw1*vect[id4+2];
  t6=fu1*vect[id5] + fv0*vect[id5+1] + fw1*vect[id5+2];
  t7=fu0*vect[id6] + fv1*vect[id6+1] + fw1*vect[id6+2];
  t8=fu1*vect[id7] + fv1*vect[id7+1] + fw1*vect[id7+2];

  RtFloat a,b,c,d,e,f;
  a=LERP(stu,t1,t2);
  b=LERP(stu,t3,t4);
  c=LERP(stu,t5,t6);
  d=LERP(stu,t7,t8);

  e=LERP(stv,a,b);
  f=LERP(stv,c,d);
  return 0.5+0.5*LERP(stw,e,f);
}
RtFloat GMANNoise::periodic (GMANPoint const &p, RtFloat t, GMANPoint const &pp, RtFloat pt)
{
  RtFloat pu=pp.getX();
  RtFloat pv=pp.getY();
  RtFloat pw=pp.getZ();
  RtFloat u=GMANMod(p.getX(),pu);
  RtFloat v=GMANMod(p.getY(),pv);
  RtFloat w=GMANMod(p.getZ(),pw);
  t=GMANMod(t,pt);

  RtInt i=(RtInt) floor(u);
  RtInt j=(RtInt) floor(v);
  RtInt k=(RtInt) floor(w);
  RtInt l=(RtInt) floor(t);

  RtFloat fu0,fu1,stu;
  fu0=u-i;
  fu1=fu0-1.0;
  stu=SMOOTH(fu0);

  RtFloat fv0,fv1,stv;
  fv0=v-j;
  fv1=fv0-1.0;
  stv=SMOOTH(fv0);

  RtFloat fw0,fw1,stw;
  fw0=w-k;
  fw1=fw0-1.0;
  stw=SMOOTH(fw0);

  RtFloat ft0,ft1,stt;
  ft0=t-l;
  ft1=ft0-1.0;
  stt=SMOOTH(ft0);

  RtInt wi,wj,wk,wl;
  if (i==(pu-1)) wi=0; else wi=i+1;
  if (j==(pv-1)) wj=0; else wj=j+1;
  if (k==(pw-1)) wk=0; else wk=k+1;
  if (l==(pt-1)) wl=0; else wl=l+1;
  RtInt id0=4*RND4(i ,j ,k ,l);
  RtInt id1=4*RND4(wi,j ,k ,l);
  RtInt id2=4*RND4(i ,wj,k ,l);
  RtInt id3=4*RND4(wi,wj,k ,l);
  RtInt id4=4*RND4(i ,j ,wk,l);
  RtInt id5=4*RND4(wi,j ,wk,l);
  RtInt id6=4*RND4(i ,wj,wk,l);
  RtInt id7=4*RND4(wi,wj,wk,l);

  RtInt id8=4*RND4(i  ,j ,k ,wl);
  RtInt id9=4*RND4(wi ,j ,k ,wl);
  RtInt id10=4*RND4(i ,wj,k ,wl);
  RtInt id11=4*RND4(wi,wj,k ,wl);
  RtInt id12=4*RND4(i ,j ,wk,wl);
  RtInt id13=4*RND4(wi,j ,wk,wl);
  RtInt id14=4*RND4(i ,wj,wk,wl);
  RtInt id15=4*RND4(wi,wj,wk,wl);

  RtFloat t1,t2,t3,t4,t5,t6,t7,t8;
  RtFloat t9,t10,t11,t12,t13,t14,t15,t16;

  t1=fu0*vect[id0] + fv0*vect[id0+1] + fw0*vect[id0+2] + ft0*vect[id0+3];
  t2=fu1*vect[id1] + fv0*vect[id1+1] + fw0*vect[id1+2] + ft0*vect[id1+3];
  t3=fu0*vect[id2] + fv1*vect[id2+1] + fw0*vect[id2+2] + ft0*vect[id2+3];
  t4=fu1*vect[id3] + fv1*vect[id3+1] + fw0*vect[id3+2] + ft0*vect[id3+3];

  t5=fu0*vect[id4] + fv0*vect[id4+1] + fw1*vect[id4+2] + ft0*vect[id4+3];
  t6=fu1*vect[id5] + fv0*vect[id5+1] + fw1*vect[id5+2] + ft0*vect[id5+3];
  t7=fu0*vect[id6] + fv1*vect[id6+1] + fw1*vect[id6+2] + ft0*vect[id6+3];
  t8=fu1*vect[id7] + fv1*vect[id7+1] + fw1*vect[id7+2] + ft0*vect[id7+3];

  t9=fu0*vect[id8] + fv0*vect[id8+1] + fw0*vect[id8+2] + ft1*vect[id8+3];
  t10=fu1*vect[id9] + fv0*vect[id9+1] + fw0*vect[id9+2] + ft1*vect[id9+3];
  t11=fu0*vect[id10] + fv1*vect[id10+1] + fw0*vect[id10+2] + ft1*vect[id10+3];
  t12=fu1*vect[id11] + fv1*vect[id11+1] + fw0*vect[id11+2] + ft1*vect[id11+3];

  t13=fu0*vect[id12] + fv0*vect[id12+1] + fw1*vect[id12+2] + ft1*vect[id12+3];
  t14=fu1*vect[id13] + fv0*vect[id13+1] + fw1*vect[id13+2] + ft1*vect[id13+3];
  t15=fu0*vect[id14] + fv1*vect[id14+1] + fw1*vect[id14+2] + ft1*vect[id14+3];
  t16=fu1*vect[id15] + fv1*vect[id15+1] + fw1*vect[id15+2] + ft1*vect[id15+3];

  RtFloat a,b,c,d,e,f;
  RtFloat aa,bb,cc,dd,ee,ff;
  a=LERP(stu,t1,t2);
  b=LERP(stu,t3,t4);
  c=LERP(stu,t5,t6);
  d=LERP(stu,t7,t8);

  e=LERP(stv,a,b);
  f=LERP(stv,c,d);
  RtFloat g=LERP(stw,e,f);

  aa=LERP(stu,t9,t10);
  bb=LERP(stu,t11,t12);
  cc=LERP(stu,t13,t14);
  dd=LERP(stu,t15,t16);

  ee=LERP(stv,aa,bb);
  ff=LERP(stv,cc,dd);
  RtFloat h=LERP(stw,ee,ff);
  return 0.5+0.5*LERP(stt,g,h);
}

RtVoid  GMANNoise::periodic (RtFloat v, RtFloat pv,
			     RtFloat &a, RtFloat &b, RtFloat &c)
{
  a=periodic(v,pv);
  prn=prn2;
  b=periodic(v,pv);
  prn=prn3;
  c=periodic(v,pv);
  prn=prn1;
}
RtVoid  GMANNoise::periodic (RtFloat u, RtFloat v, RtFloat pu, RtFloat pv,
			     RtFloat &a, RtFloat &b, RtFloat &c)
{
  a=periodic(u,v,pu,pv);
  prn=prn2;
  b=periodic(u,v,pu,pv);
  prn=prn3;
  c=periodic(u,v,pu,pv);
  prn=prn1;
}
RtVoid  GMANNoise::periodic (GMANPoint const &p, GMANPoint const &pp,
			     RtFloat &a, RtFloat &b, RtFloat &c)
{
  a=periodic(p,pp);
  prn=prn2;
  b=periodic(p,pp);
  prn=prn3;
  c=periodic(p,pp);
  prn=prn1;
}
RtVoid  GMANNoise::periodic (GMANPoint const &p, RtFloat t, GMANPoint const &pp,
			     RtFloat pt, RtFloat &a, RtFloat &b, RtFloat &c)
{
  a=periodic(p,t,pp,pt);
  prn=prn2;
  b=periodic(p,t,pp,pt);
  prn=prn3;
  c=periodic(p,t,pp,pt);
  prn=prn1;
}


  /***********************/
 /* CellNoise Functions */
/***********************/
#undef RND1
#undef RND2
#undef RND3
#undef RND4

#define RND1(x) cprn[(x)&CMASK]
#define RND2(x,y) cprn[(cprn[(x)&CMASK]+y)&CMASK]
#define RND3(x,y,z) cprn[(cprn[(cprn[(x)&CMASK]+y)&CMASK]+z)&CMASK]
#define RND4(x,y,z,w) cprn[(cprn[(cprn[(cprn[(x)&CMASK]+y)&CMASK]+z)&CMASK]+w)&CMASK]

RtFloat GMANNoise::cellnoise (RtFloat v)
{
  // TODO: handle the case where v is greater than an int.
  RtInt a=(RtInt) floor(v);
  return cvect[RND1(a)];
}
RtFloat GMANNoise::cellnoise (RtFloat u, RtFloat v)
{
  RtInt a=(RtInt) floor(u);
  RtInt b=(RtInt) floor(v);
  return cvect[RND2(a,b)];
}
RtFloat GMANNoise::cellnoise (GMANPoint const &p)
{
  RtInt a=(RtInt) floor(p.getX());
  RtInt b=(RtInt) floor(p.getY());
  RtInt c=(RtInt) floor(p.getZ());
  return cvect[RND3(a,b,c)];
}
RtFloat GMANNoise::cellnoise (GMANPoint const &p, RtFloat t)
{
  RtInt a=(RtInt) floor(p.getX());
  RtInt b=(RtInt) floor(p.getY());
  RtInt c=(RtInt) floor(p.getZ());
  RtInt d=(RtInt) floor(t);
  return cvect[RND4(a,b,c,d)];
}

RtVoid  GMANNoise::cellnoise (RtFloat v,
			      RtFloat &a, RtFloat &b, RtFloat &c)
{
  a=cellnoise(v);
  cprn=cprn2;
  b=cellnoise(v);
  cprn=cprn3;
  c=cellnoise(v);
  cprn=cprn1;
}
RtVoid  GMANNoise::cellnoise (RtFloat u, RtFloat v,
			      RtFloat &a, RtFloat &b, RtFloat &c)
{
  a=cellnoise(u,v);
  cprn=cprn2;
  b=cellnoise(u,v);
  cprn=cprn3;
  c=cellnoise(u,v);
  cprn=cprn1;
}
RtVoid  GMANNoise::cellnoise (GMANPoint const &p,
			      RtFloat &a, RtFloat &b, RtFloat &c)
{
  a=cellnoise(p);
  cprn=cprn2;
  b=cellnoise(p);
  cprn=cprn3;
  c=cellnoise(p);
  cprn=cprn1;
}
RtVoid  GMANNoise::cellnoise (GMANPoint const &p, RtFloat t,
			      RtFloat &a, RtFloat &b, RtFloat &c)
{
  a=cellnoise(p,t);
  cprn=cprn2;
  b=cellnoise(p,t);
  cprn=cprn3;
  c=cellnoise(p,t);
  cprn=cprn1;
}
