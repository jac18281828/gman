/* SPDX-License-Identifier: LGPL-2.1-or-later */

/* This is part of the GNU GMAN Library, a FREE implementation of the
 * RenderMan Interface Specification.
 *
 * Copyright (c) 2026 John Cairns <john@2ad.com>
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
 * GMANSurfaceEnv's noise family. Isolated in its own translation unit
 * because gmannoise.h #defines N (and MASK) at file scope with no #undef,
 * which collides with GMANSurfaceEnv::N (the shading normal) the moment
 * both are visible together -- see gmanshaderenvironment.h's comment at
 * the forward declaration this file resolves.
 */

#include "gmanshaderenvironment.h"
#include "gmannoise.h"

namespace {

// One generator for every GMANSurfaceEnv, matching real SL's noise()
// being a pure function of its argument, not of shader instance.
// Constructing a GMANNoise reseeds the process-global C rand()
// (srand(808)); a fresh instance per shading call would be both wasteful
// and non-deterministic with respect to whatever else in the process
// calls rand().
GMANNoise &noiseGenerator() {
  static GMANNoise generator;
  return generator;
}

}  // namespace

RtFloat GMANSurfaceEnv::noise(RtFloat v) const {
  return noiseGenerator().noise(v);
}

RtFloat GMANSurfaceEnv::noise(RtFloat u_, RtFloat v_) const {
  return noiseGenerator().noise(u_, v_);
}

RtFloat GMANSurfaceEnv::noise(const GMANPoint &p) const {
  return noiseGenerator().noise(p);
}

RtFloat GMANSurfaceEnv::noise(const GMANPoint &p, RtFloat t_) const {
  return noiseGenerator().noise(p, t_);
}

RtFloat GMANSurfaceEnv::pnoise(RtFloat v, RtFloat pv) const {
  return noiseGenerator().periodic(v, pv);
}

RtFloat GMANSurfaceEnv::pnoise(const GMANPoint &p, const GMANPoint &pp) const {
  return noiseGenerator().periodic(p, pp);
}

RtFloat GMANSurfaceEnv::cellnoise(RtFloat v) const {
  return noiseGenerator().cellnoise(v);
}

RtFloat GMANSurfaceEnv::cellnoise(const GMANPoint &p) const {
  return noiseGenerator().cellnoise(p);
}
