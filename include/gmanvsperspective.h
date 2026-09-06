/* SPDX-License-Identifier: LGPL-2.1-or-later */

/*----------------------------------------------------------
  Copyright (C) Lionel Joseph Lacour 2001, 2002
  February 2001 First release
  ----------------------------------------------------------
  Perspective viewing system.
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

#ifndef __GMANVSPERSPECTIVE_H
#define __GMANVSPERSPECTIVE_H 1

#include "gmanviewingsystem.h"
#include "gmanmatrix4.h"

class GMAN_EXPORT  GMANVSPerspective : public GMANViewingSystem
{
private:
  GMANMatrix4 mtrx;
public:
  GMANVSPerspective(RtInt xr, RtInt yr,
		    const GMANOptions::ScreenWindowStruct &s,
		    const GMANMatrix4 &worldToCamera,
		    RtFloat fov, RtFloat nearDist, RtFloat farDist);
  ~GMANVSPerspective() {}

  GMANPoint project(GMANPoint const &p);
  GMANRay   ray(RtFloat x, RtFloat y);

  bool visible(const GMANFace *face);

  const RtMatrix &getProjMatrix(RtVoid) const;
};

#endif
