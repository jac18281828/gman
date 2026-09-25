/* SPDX-License-Identifier: LGPL-2.1-or-later */

/*----------------------------------------------------------
  Copyright (C) Lionel Joseph Lacour 2001, 2002
  February 2001 First release
  ----------------------------------------------------------
  This code based on Ken Perlin gradient noise function.
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

#pragma once

#include <random>

#include "gmanpoint.h"
#include "ri.h"

/* CellNoise */
#define CN 2048
#define CMASK 0x7ff

namespace gman {

class Noise {
private:
  /* Noise and pnoise */
  static constexpr RtInt kN = 256;
  static constexpr RtInt kMask = 0xff;

  // Seeded (808) once per instance, so every instance's noise field is
  // pinned to the same values independent of any other instance or of
  // whatever else in the process draws random numbers. Never sampled
  // through uniform_int_distribution/uniform_real_distribution: their
  // output is implementation-defined per standard library even from an
  // identical seeded engine, so a fixed seed would not draw the same
  // values on every platform. mt19937's own operator() sequence is fully
  // specified by the standard and portable.
  std::mt19937 rng;

  RtInt* prn;
  RtInt prn1[kN]; /* 0 to kN in random order*/
  RtInt prn2[kN];
  RtInt prn3[kN];
  RtFloat vect[4 * kN]; /* kN 4D random unit vectors */

  RtInt* cprn;
  RtInt cprn1[CN];
  RtInt cprn2[CN];
  RtInt cprn3[CN];
  RtFloat cvect[CN];

  RtFloat rn();

public:
  Noise();

  RtFloat noise(RtFloat v);
  RtFloat noise(RtFloat u, RtFloat v);
  RtFloat noise(GMANPoint const& p);
  RtFloat noise(GMANPoint const& p, RtFloat t);

  RtVoid noise(RtFloat v, RtFloat& a, RtFloat& b, RtFloat& c);
  RtVoid noise(RtFloat u, RtFloat v, RtFloat& a, RtFloat& b, RtFloat& c);
  RtVoid noise(GMANPoint const& p, RtFloat& a, RtFloat& b, RtFloat& c);
  RtVoid noise(GMANPoint const& p, RtFloat t, RtFloat& a, RtFloat& b, RtFloat& c);

  RtFloat periodic(RtFloat v, RtFloat pv);
  RtFloat periodic(RtFloat u, RtFloat v, RtFloat pu, RtFloat pv);
  RtFloat periodic(GMANPoint const& p, GMANPoint const& pp);
  RtFloat periodic(GMANPoint const& p, RtFloat t, GMANPoint const& pp, RtFloat pt);

  RtVoid periodic(RtFloat v, RtFloat pv, RtFloat& a, RtFloat& b, RtFloat& c);
  RtVoid periodic(RtFloat u, RtFloat v, RtFloat pu, RtFloat pv, RtFloat& a, RtFloat& b, RtFloat& c);
  RtVoid periodic(GMANPoint const& p, GMANPoint const& pp, RtFloat& a, RtFloat& b, RtFloat& c);
  RtVoid periodic(GMANPoint const& p, RtFloat t, GMANPoint const& pp, RtFloat pt, RtFloat& a, RtFloat& b, RtFloat& c);

  RtFloat cellnoise(RtFloat v);
  RtFloat cellnoise(RtFloat u, RtFloat v);
  RtFloat cellnoise(GMANPoint const& p);
  RtFloat cellnoise(GMANPoint const& p, RtFloat t);

  RtVoid cellnoise(RtFloat v, RtFloat& a, RtFloat& b, RtFloat& c);
  RtVoid cellnoise(RtFloat u, RtFloat v, RtFloat& a, RtFloat& b, RtFloat& c);
  RtVoid cellnoise(GMANPoint const& p, RtFloat& a, RtFloat& b, RtFloat& c);
  RtVoid cellnoise(GMANPoint const& p, RtFloat t, RtFloat& a, RtFloat& b, RtFloat& c);
};

} // namespace gman
