/* SPDX-License-Identifier: LGPL-2.1-or-later */

/*---------------------------------------------------------
  Copyright (C) Lionel Joseph Lacour 2000, 2001
  2000/07/26  First release
  ---------------------------------------------------------
  A class to store parameter lists.
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

#ifndef __GMANPARAMETERLIST_H
#define __GMANPARAMETERLIST_H 1

#include <string>
#include "ri.h"
#include "gmanlog.h"
#include "gmandictionary.h"
#include "gmanerror.h"


class GMAN_EXPORT  GMANParameterList
{
private:
  RtInt *counter;
  
  RtInt  number;
  GMANTokenId *id;
  RtPointer *datas;
  GMANDictionary *dic;

  RtVoid copy_float(RtInt number, RtFloat *source, RtFloat *dest, RtInt supplied);
  RtVoid copy_integer(RtInt number, RtInt *source, RtInt *dest, RtInt supplied);
  RtVoid copy_string(RtInt number, char **source, std::string *dest, RtInt supplied);
  RtVoid copy (GMANParameterList const &pl);
  RtVoid destroy ();
public:
  GMANParameterList ();

  // suppliedCounts, when present, is index-aligned with tk/dt: element i is
  // the length the caller actually supplied for dt[i], as opposed to
  // vertex/varying/uniform/facevarying, which say how long it is supposed
  // to be. A short supplied[i] clamps the copy and zero-fills the rest,
  // once, with a warning naming the parameter and both lengths -- reading
  // past dt[i]'s own allocation is the defect this parameter exists to
  // close. NULL (the default) means the caller's arrays are trusted at
  // their declared length, exactly as before this parameter existed: the
  // RIB-parsed path, which does not control what a scene file supplies,
  // passes real counts; a program calling the public RI API directly is
  // trusted with its own memory, and the RISpec gives no way to describe
  // an RtPointer's length regardless.
  GMANParameterList (GMANDictionary &di,
		     RtInt n, RtToken *tk, RtPointer *dt,
		     RtInt vertex=1, RtInt varying=1, RtInt uniform=1,
		     RtInt facevarying=1, const RtInt *suppliedCounts=NULL);
  GMANParameterList (GMANParameterList const &pl);
  GMANParameterList const &operator=(GMANParameterList const &pl);
  ~GMANParameterList ();

  RtPointer getPointer(GMANTokenId tid) const;
};

#endif








