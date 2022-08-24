/* This is part of the GNU GMAN Library, a FREE implementation of the
 * RenderMan Interface Specification.
 *
 * Copyright (c) 2001, 2000, 1999  John Cairns 
 *
 * Author: John Cairns <john@2ad.com>
 */

// LJL - Feb 2001 - Complete rewrite for libgmanrib

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

#include <string>
#include "ri.h"
#include "gmanrenderman.h"
#include "gmaninlineparse.h"


GMANRenderMan::GMANRenderMan() : colorNComps(3), lastObjectHandle(0), lastLightHandle(0)
{
  Steps a={RI_BEZIERSTEP,RI_BEZIERSTEP};
  colorNComps=3;
  steps.push(a);
}

GMANRenderMan::~GMANRenderMan()
{
}


extern "C" RtFloat   RiGaussianFilter(RtFloat x, RtFloat y,
				      RtFloat xwidth, RtFloat ywidth)
{ return 1.0; }
extern "C" RtFloat   RiBoxFilter(RtFloat x, RtFloat y,
				 RtFloat xwidth, RtFloat ywidth)
{ return 1.0; }
extern "C" RtFloat   RiTriangleFilter(RtFloat x, RtFloat y,
				      RtFloat xwidth, RtFloat ywidth)
{ return 1.0; }
extern "C" RtFloat   RiCatmullRomFilter(RtFloat x,RtFloat y,
					RtFloat xwidth, RtFloat ywidth)
{ return 1.0; }
extern "C" RtFloat   RiSincFilter(RtFloat x, RtFloat y,
				  RtFloat xwidth, RtFloat ywidth)
{ return 1.0; }

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

RtVoid RiProcDelayedReadArchive (RtPointer data, RtFloat detail) {}
RtVoid RiProcRunProgram (RtPointer data, RtFloat detail) {}
RtVoid RiProcDynamicLoad (RtPointer data, RtFloat detail) {}


// *********************************************************************
// ******* ******* ******* STEPS STACK FUNCTIONS ******* ******* *******
// *********************************************************************
RtVoid GMANRenderMan::push()
{
  steps.push(steps.top());
}
RtVoid GMANRenderMan::pop()
{
  if (steps.size()==0) {
    return;
  }
  steps.pop();
}




// ********************************************************************
// ******* ******* ******* RIB OUTPUT FUNCTIONS ******* ******* *******
// ********************************************************************
RtToken GMANRenderMan::Declare(const char *name, const char *declaration) 
{
  GMANInlineParse ip;
  string a(name);
  string b(declaration);

  b+=" ";
  b+=a;
  ip.parse(b);
  dictionary.addToken(ip.getIdentifier(),ip.getClass(),ip.getType(),ip.getQuantity(),false);

  return const_cast<RtToken> (name);
}
RtObjectHandle GMANRenderMan::ObjectBegin(RtVoid)
{
  RtInt t=lastObjectHandle;
  lastObjectHandle+=1;

  return (RtObjectHandle) t;
}




// ******************************************************************
// ******* ******* ******* ADDITIONAL OPTIONS ******* ******* *******
// ******************************************************************
RtVoid  GMANRenderMan::ColorSamples(RtInt n)
{
  colorNComps=n;
}




// ******************************************************************
// ******* ******* ******* SHADING ATTRIBUTES ******* ******* *******
// ******************************************************************
RtLightHandle GMANRenderMan::LightSource()
{
  RtInt t=lastLightHandle;
  lastLightHandle+=1;
  return (RtLightHandle) t;
}
RtLightHandle GMANRenderMan::AreaLightSource()
{
  RtInt t=lastLightHandle;
  lastLightHandle+=1;
  return (RtLightHandle) t;
}




// *******************************************************************
// ******* ******* ******* GEOMETRY ATTRIBUTES ******* ******* *******
// *******************************************************************
RtVoid  GMANRenderMan::Basis(RtInt ustep, RtInt vstep)
{
  steps.top().uStep=ustep;
  steps.top().vStep=vstep;
}
