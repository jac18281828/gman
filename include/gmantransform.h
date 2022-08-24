/*---------------------------------------------------------
  Copyright (C) Lionel Joseph Lacour 2000, 2001
  November 2000  First release
  ---------------------------------------------------------
  A wrapper class for GMANMatrix4
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
#ifndef __GMANTRANSFORM_HH
#define __GMANTRANSFORM_HH 1

#include <set>
#include "ri.h"
#include "gmanmatrix4.h"
#include "gmanpoint.h"
#include "universalsuperclass.h"

class GMANDLL  GMANMatrixStorage : public UniversalSuperClass
{
public:
  virtual ~GMANMatrixStorage();

  virtual GMANMatrix4 interpolate(RtFloat tm)=0;
  virtual RtInt getSamplesQuantity()=0;
};

class GMANDLL GMANOneMatrix : public GMANMatrixStorage
{
private:
  GMANMatrix4 mx;
public:
  GMANOneMatrix(GMANMatrix4 &m);

  GMANMatrix4 interpolate(RtFloat tm);
  RtInt getSamplesQuantity();
};

class GMANDLL GMANMovingMatrix : public GMANMatrixStorage
{
private:
  RtInt nbTimes;
  RtFloat *times;
  GMANMatrix4 *storage;

  RtVoid copy(GMANMovingMatrix const &mm);
public:
  GMANMovingMatrix(RtInt nb, RtFloat *tms);
  GMANMovingMatrix(GMANMovingMatrix const &mm);
  ~GMANMovingMatrix();
  GMANMovingMatrix const &operator=(GMANMovingMatrix const &mm);

  GMANMatrix4 interpolate(RtFloat tm);
  RtInt getSamplesQuantity();
  GMANMatrix4 &get(RtInt nb);
  RtFloat getTime(RtInt n);
};


class GMANDLL GMANTransform : public UniversalSuperClass
{
private:
  GMANMatrixStorage *storage;

  RtVoid copy(GMANTransform const &t);
public:
  GMANTransform();
  GMANTransform(GMANMatrixStorage &m);
  GMANTransform(GMANTransform const &t);
  ~GMANTransform();
  GMANTransform const &operator=(GMANTransform const &t);

  GMANMatrix4 interpolate(RtFloat tm) const;
  RtVoid concat(GMANTransform &t);
  bool isMoving();

  GMANPoint apply(const GMANPoint &p);
};

#endif



