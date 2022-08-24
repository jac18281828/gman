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


/*
  This class make possible, concatenation of moving transforms.
  It can handle any number of time samples, and times can be different in
  each Motion Block.
  It also perfectly concatenate on any transform as well.
  
  So you can have:
  
  --- any number of transforms here --- 
  MotionBlock 3   0.1 0.2 0.3
  transforms
  EndBlock
  
  MotionBlock 8   25 27 28 32 56 75 88 99
  transforms
  EndBlock
  --- again some transforms here ---
  
  (again some Motion Block ...)
  
  an object.
  This object then will have a transform that handle all this!


 . When you want to render an object, retrieve its transform,
  and then call:
  
  interpolate( any time )
  
  and you will get the right GMANMatrix4, that's all:) 
  

  -------------------------------------------------------------
  PS: I use matrix interpolation. I think it's a better choice 
  although rotations may look like lines, without enough time samples.
  Anyway, this is not a problem, since samples are not limited.
*/

#include <list>
#if HAVE_STD_NAMESPACE
using std::list;
#endif

#include "gmantransform.h"

// MATRIX STORAGE
GMANMatrixStorage::~GMANMatrixStorage()
{
};


// ONE MATRIX
GMANOneMatrix::GMANOneMatrix(GMANMatrix4 &m)
{
  mx=m; 
}
GMANMatrix4 GMANOneMatrix::interpolate(RtFloat tm)
{
  return mx;
}
RtInt GMANOneMatrix::getSamplesQuantity()
{
  return 1;
}

// MOVING MATRIX
GMANMovingMatrix::GMANMovingMatrix(RtInt nb, RtFloat *tms)
{
  nbTimes=nb;
  times=new RtFloat[nb];
  for(int i=0;i<nb;i++)
    times[i]=tms[i];
  storage=new GMANMatrix4[nb];
}
GMANMovingMatrix::GMANMovingMatrix(GMANMovingMatrix const &mm)
{
  copy(mm);
}
GMANMovingMatrix::~GMANMovingMatrix()
{
  delete times;
  delete [] storage;
}
GMANMovingMatrix const &GMANMovingMatrix::operator=(GMANMovingMatrix const &mm)
{
  if (this!=&mm) {
    delete times;
    delete [] storage;
    copy(mm);
  }
  return *this;
}
RtVoid GMANMovingMatrix::copy(GMANMovingMatrix const &mm)
{
  nbTimes=mm.nbTimes;
  times=new RtFloat[nbTimes];
  storage=new GMANMatrix4[nbTimes];  
  for(int i=0;i<nbTimes;i++) {
    times[i]=mm.times[i];
    storage[i]=mm.storage[i];
  }
}
GMANMatrix4 &GMANMovingMatrix::get(RtInt nb)
{
  return storage[nb];
}
GMANMatrix4 GMANMovingMatrix::interpolate(RtFloat time)
{
  GMANMatrix4 temp,result;
  if (time<=times[0]) {return (storage[0]);}
  if (time>=times[nbTimes-1]) {return (storage[nbTimes-1]);}

  int i;
  for(i=1;i<nbTimes;i++) {
    if (time<=times[i]) break;
  }
  RtFloat t1=times[i-1];
  RtFloat t2=times[i];
  RtFloat n=((time-t1)/(t2-t1));
  temp=storage[i-1];
  result=storage[i];
  return (result*(n)+temp*(1-n));
}
RtInt GMANMovingMatrix::getSamplesQuantity()
{
  return nbTimes;
}

RtFloat GMANMovingMatrix::getTime(RtInt t)
{
  return times[t];
}


// GMAN TRANSFORM ---
RtVoid GMANTransform::copy(GMANTransform const &t)
{
  GMANOneMatrix *om=dynamic_cast<GMANOneMatrix *> (t.storage);
  GMANMovingMatrix *mm=dynamic_cast<GMANMovingMatrix *> (t.storage);
  if (om) {
    storage=new GMANOneMatrix(*om);
  } else if (mm) {
    storage=new GMANMovingMatrix(*mm);
  }
}
GMANTransform::GMANTransform()
{
  GMANMatrix4 a;
  storage=new GMANOneMatrix(a);
}

GMANTransform::GMANTransform(GMANMatrixStorage &m)
{
  GMANOneMatrix *om=dynamic_cast<GMANOneMatrix *> (&m);
  GMANMovingMatrix *mm=dynamic_cast<GMANMovingMatrix *> (&m);
  if (om) {
    storage=new GMANOneMatrix(*om);
  } else if (mm) {
    storage=new GMANMovingMatrix(*mm);
  }
}

GMANTransform::GMANTransform(GMANTransform const &t)
{
  copy(t);
}

GMANTransform::~GMANTransform()
{
  delete storage;
}
GMANTransform const &GMANTransform::operator=(GMANTransform const &t)
{
  if(this!=&t) {
    delete storage;
    copy(t);
  }
  return *this;
}
GMANMatrix4 GMANTransform::interpolate(RtFloat tm) const
{
  return storage->interpolate(tm);
} 
RtVoid GMANTransform::concat(GMANTransform &t)
{
  RtInt a,b;
  GMANMatrix4 m;
  a=storage->getSamplesQuantity();
  b=t.storage->getSamplesQuantity();
 
  if (a==1 && b==1) {       // =======
    // FIXME: Change transform in place instead of replacing
    m=interpolate(0);
    m.concat(t.storage->interpolate(0));
    GMANOneMatrix *om = new GMANOneMatrix(m);
    delete storage;
    storage=om;
  } else if (a==1 && b>1) { // =======
    GMANMovingMatrix *t1=dynamic_cast<GMANMovingMatrix *> (t.storage);
    GMANMovingMatrix *mm=new GMANMovingMatrix(*t1);
    m=interpolate(0);
    for (RtInt i=0;i<b;i++) {
      mm->get(i) = m;
      mm->get(i).concat(t1->get(i));
    }
    delete storage;
    storage=mm;
  } else if (b==1 && a>1) { // =======
    GMANMovingMatrix *t1=dynamic_cast<GMANMovingMatrix *> (storage);
    m=t.storage->interpolate(0);
    for (int i=0;i<a;i++) {
      t1->get(i).concat(m);
    }
  } else if (a>1 && b>1) {  // =======
    GMANMovingMatrix *t1,*t2;
    t1=dynamic_cast<GMANMovingMatrix *> (storage);
    t2=dynamic_cast<GMANMovingMatrix *> (t.storage);

    list<RtFloat> tm;
    int i;
    for(i=0;i<a;i++)
      tm.push_back(t1->getTime(i));
    for(i=0;i<b;i++)
      tm.push_back(t2->getTime(i));

    GMANMovingMatrix *mm;
    RtFloat *tmp = new RtFloat[tm.size()];
    list<RtFloat>::iterator it;
    for(it=tm.begin(),i=0;it!=tm.end();it++,i++) {
      tmp[i]=*it;
    }
	
    mm=new GMANMovingMatrix(tm.size(),tmp);
    for(unsigned int j=0;j<tm.size();j++) {
      mm->get(j)=t1->interpolate(tmp[j]);
      mm->get(j).concat(t2->interpolate(tmp[j]));
    }
	if(tmp) delete(tmp);
    delete storage;
    storage=mm;
  } 
}

bool GMANTransform::isMoving()
{
  if (storage->getSamplesQuantity()==1) {
    return false;
  } else return true;
}

GMANPoint GMANTransform::apply(const GMANPoint &p)
{
#if 0
  static bool warned1 = false, warned2 = false;
  if (isMoving() && !warned1) {
    debug("GMANTransform::apply is not frame sensitive");
    warned1 = true;
  }
  if (!warned2) {
    debug("GMANTransform::Refactor GMANTransform into homo / nonhomo");
    warned2 = true;
  }

  GMANMatrix4 matrix = storage->interpolate(0.0);
  RtFloat src[] = { p.getX(), p.getY(), p.getZ() };
  RtFloat dest[3];
  matrix.p3m(1, src, dest);
  return GMANPoint(dest[0], dest[1], dest[2]);
#else
  return p;
#endif
}
