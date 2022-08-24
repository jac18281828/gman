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
  FT COLLINS, CO, 80525, USA, or write via E-mail john@2ad.com.
*/

/* Local Headers */
#include "ri.h"      /* RenderMan Interface */
#include "universalsuperclass.h" /* Super class */
#include "gmancolor.h" /* Declaration Header */


/*
 * RenderMan API GMANColor
 *
 */


/*
 * Weighting factors representing the sensitivity of the
 * human eye to a given color.
 *
 */
const RtFloat		GMANColorRGB::redWeight = 0.30f;
const RtFloat		GMANColorRGB::greenWeight = 0.59f;
const RtFloat		GMANColorRGB::blueWeight = 0.11f;



/*---------------------------------------------------------------
 * January 2001 -- LJL
 * Color space conversions.
 *---------------------------------------------------------------
 *
 * The XYZ matrices are from the 'Colorspace-faq'.
 * HSV and HSL conversions are based on code found in
 * 'Procedural elements for computer graphics' by David F. Rogers
 * Also two web pages help me for this task:
 *  - www.swin.edu.au/astronomy/pbourke/colour/conversion.html
 *  - www.cs.rit.edu/~ncs/color/t_convert.html
 *
 */

#define MINMAX(a,b,c) if (a>b) {max=a;min=b;} else {max=b;min=a;} \
                      if (max<c) max=c; else if (min>c) min=c;

const RtFloat RGBXYZ[]= { 0.412453, 0.357580, 0.180423,
			  0.212671, 0.715160, 0.072169,
			  0.019334, 0.119193, 0.950227};

const RtFloat XYZRGB[]= {  3.240479, -1.537150, -0.498535,
			  -0.969256,  1.875991,  0.041556,
			   0.055648, -0.204043,  1.057311};

const RtFloat RGBYIQ[]= { 0.299,  0.587,  0.144,
			  0.596, -0.275, -0.321,
			  0.212, -0.523,  0.311};

const RtFloat YIQRGB[]= { 1.000,  0.956,  0.621,
			  1.000, -0.272, -0.647,
			  1.000, -1.105,  1.702};

const RtFloat RGBYUV[]= {  0.299,  0.587,  0.114,
			  -0.147, -0.289,  0.437,
			   0.615, -0.515, -0.100};

const RtFloat YUVRGB[]= { 1.0,  0.0,    1.140,
			  1.0, -0.394, -0.581,
			  1.0,  2.028,  0.0};

// ==== HSV ==== //
RtVoid GMANConvertRGBtoHSV (RtFloat *c)
{
  RtFloat min, max, delta;
  RtFloat r=c[0], g=c[1], b=c[2];

  MINMAX(r,g,b)
  delta=max-min;
  c[2]=max;

  if (c[2]==0.0)
    c[1]=0.0;
  else
    c[1]=delta/max;

  if (delta==0.0) {
    c[0]=0.0;
  } else {
    if (r==max)
      c[0]=(g-b)/delta;
    else if (g==max)
      c[0]=2.0+(b-r)/delta;
    else if (b==max)
      c[0]=4.0+(r-g)/delta;

    c[0]*=60;
    if (c[0]<0)
      c[0]+=360.0;
  }    
}

RtVoid GMANConvertHSVtoRGB (RtFloat *c)
{
  RtInt i;
  RtFloat f,m,n,k;

  if (c[1]==0.0) { // achromatic
    c[0]=c[2];
    c[1]=c[2];
    return;
  } else {
    if (c[0]==360.0)
      c[0]=0.0;

    c[0]=c[0]/60.0;
    i=(RtInt) floor(c[0]);
    f=c[0]-i;
    m=c[2] * (1-c[1]);
    n=c[2] * (1-c[1] * f);
    k=c[2] * (1-c[1] * (1-f));
    switch (i) {
    case 0:
      c[0]=c[2];
      c[1]=k;
      c[2]=m;
      break;
    case 1:
      c[0]=n;
      c[1]=c[2];
      c[2]=m;
      break;
    case 2:
      c[0]=m;
      c[1]=c[2];
      c[2]=k;
      break;
    case 3:
      c[0]=m;
      c[1]=n;
      break;
    case 4:
      c[0]=k;
      c[1]=m;
      break;
    case 5:
      c[0]=c[2];
      c[1]=m;
      c[2]=n;
      break;
    }
  } 
}

// ==== HSL ==== //
RtVoid GMANConvertRGBtoHSL (RtFloat *c)
{
  RtFloat min,max,delta;
  RtFloat r=c[0], g=c[1], b=c[2];

  MINMAX(r,g,b);
  delta=(max-min);

  c[1]=(min+max)/2.0;

  if (min==max) {
    c[0]=0.0;
    c[2]=0.0;
  } else {
    if (c[1]<= 0.5)
      c[2]=delta/(max+min);
    else
      c[2]=delta/(2.0-max+min);
  }

  if (r==max)
    c[0]=(g-b)/delta;
  else if (g==max)
    c[0]=2.0+(b-r)/delta;
  else if (b==max)
    c[0]=4.0+(r-g)/delta;
  
  c[0]*=60;
  if (c[0]<0)
    c[0]+=360.0;
}

static RtFloat subRGB(RtFloat a, RtFloat m1, RtFloat m2)
{
  if (a<0) a+=360.0;
  if (a>360) a-=360.0;

  if (a<60) return m1*(m2-m1)*a/60;
  if (a<180) return m2;
  if (a<240) return m1*(m2-m1)*(240-a)/60;
  else return m1;
}

RtVoid GMANConvertHSLtoRGB (RtFloat *c)
{
  RtFloat m1,m2;
  if (c[1]<=0.5)
    m1=c[1]*(1-c[2]);
  else
    m1=c[1]+c[2]-c[1]*c[2];
  m2=2*c[1]-m1;

  if (c[2]==0) {
    c[0]=1.0;
    c[1]=1.0;
    c[2]=1.0;
  } else {
    c[0]=subRGB(c[0]+120.0,m1,m2);
    c[1]=subRGB(c[0],m1,m2);
    c[2]=subRGB(c[0]-120.0,m1,m2);
  }
}


// Bug removed. Thanks to Grendel Drago
static RtVoid convert (RtFloat *c, const RtFloat *mtrx)
{
  RtFloat c0=c[0], c1=c[1];
  c[0]=c[0]*mtrx[0] + c[1]*mtrx[1] + c[2]*mtrx[2];
  c[1]=  c0*mtrx[3] + c[1]*mtrx[4] + c[2]*mtrx[5];
  c[2]=  c0*mtrx[6] +   c1*mtrx[7] + c[2]*mtrx[8];
}

// ==== XYZ ==== //
RtVoid GMANConvertRGBtoXYZ (RtFloat *c)
{
  convert(c,RGBXYZ);
}
RtVoid GMANConvertXYZtoRGB (RtFloat *c)
{
  convert(c,XYZRGB);
}


// ==== xyY ==== //
RtVoid GMANConvertRGBtoXYY (RtFloat *c)
{
  RtFloat X,Y,Z;
  convert(c,RGBXYZ);
  X=c[0];
  Y=c[1];
  Z=c[2];
  c[0]=X/(X+Y+Z);
  c[1]=Y/(X+Y+Z);
  c[2]=Y;
}
RtVoid GMANConvertXYYtoRGB (RtFloat *c)
{
  RtFloat x,y,yy;
  x=c[0];
  y=c[1];
  yy=c[2];
  c[0]=x/y*yy;
  c[1]=yy;
  c[2]=yy/y*(1.0-x-y);
  convert(c,XYZRGB);
}


// ==== YIQ ==== //
RtVoid GMANConvertRGBtoYIQ (RtFloat *c)
{
  convert(c,RGBYIQ);
};
RtVoid GMANConvertYIQtoRGB (RtFloat *c)
{
  convert(c,YIQRGB);
};


// ==== YUV ==== //
RtVoid GMANConvertRGBtoYUV (RtFloat *c)
{
  convert(c,RGBYUV);
};
RtVoid GMANConvertYUVtoRGB (RtFloat *c)
{
  convert(c,YUVRGB);
};










