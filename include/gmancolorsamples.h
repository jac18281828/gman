/* SPDX-License-Identifier: LGPL-2.1-or-later */

/*----------------------------------------------------------
  Copyright (C) Lionel Joseph Lacour 2000, 2001
  July 2000  First release
  ----------------------------------------------------------
  This class provide color conversion between
  spectral colors to or from RGB
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

#ifndef __GMANCOLORSAMPLES_H
#define __GMANCOLORSAMPLES_H

#include "ri.h"
#include "gmanlog.h"
#include "gmandefaults.h"

class GMAN_EXPORT GMANColorSamples
{
private:
  int *counter;

  RtInt number;
  RtFloat *nRGB;
  RtFloat *RGBn;

  RtVoid copy(RtInt n, RtFloat *ncolor, RtFloat *colorn);
  RtVoid copy(GMANColorSamples const &);
  RtVoid destroy();
public:
  GMANColorSamples ();
  GMANColorSamples (RtInt n, RtFloat *ncolor, RtFloat *colorn);
  GMANColorSamples (GMANColorSamples const &);
  ~GMANColorSamples ();
  GMANColorSamples const &operator=(GMANColorSamples const &);

  int getNumber () { return number; }
};

#endif
