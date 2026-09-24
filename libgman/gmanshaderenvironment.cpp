/* SPDX-License-Identifier: LGPL-2.1-or-later */

/* This is part of GMAN, a RenderMan-compatible renderer.
 *
 * Copyright (c) 2026 John Cairns <john@2ad.com>
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

#include "gmanmath.h"
#include "gmannoise.h"
#include "gmanshaderenvironment.h"
#include "gmantexture.h"

namespace {

// One generator for every GMANSurfaceEnv, matching real SL's noise()
// being a pure function of its argument, not of shader instance.
// Constructing a gman::Noise reseeds the process-global C rand()
// (srand(808)); a fresh instance per shading call would be both wasteful
// and non-deterministic with respect to whatever else in the process
// calls rand().
gman::Noise& noiseGenerator() {
  static gman::Noise generator;
  return generator;
}

} // namespace

RtFloat GMANSurfaceEnv::noise(RtFloat v) const { return noiseGenerator().noise(v); }

RtFloat GMANSurfaceEnv::noise(RtFloat u_, RtFloat v_) const { return noiseGenerator().noise(u_, v_); }

RtFloat GMANSurfaceEnv::noise(const GMANPoint& p) const { return noiseGenerator().noise(p); }

RtFloat GMANSurfaceEnv::noise(const GMANPoint& p, RtFloat t_) const { return noiseGenerator().noise(p, t_); }

RtFloat GMANSurfaceEnv::pnoise(RtFloat v, RtFloat pv) const { return noiseGenerator().periodic(v, pv); }

RtFloat GMANSurfaceEnv::pnoise(const GMANPoint& p, const GMANPoint& pp) const {
  return noiseGenerator().periodic(p, pp);
}

RtFloat GMANSurfaceEnv::cellnoise(RtFloat v) const { return noiseGenerator().cellnoise(v); }

RtFloat GMANSurfaceEnv::cellnoise(const GMANPoint& p) const { return noiseGenerator().cellnoise(p); }

GMANColor GMANSurfaceEnv::texture(const std::string& name, RtFloat s, RtFloat t) const {
  return gman::textureCache().sample(name, s, t);
}

GMANColor GMANSurfaceEnv::trace(GMANVector const& R) const {
  return tracer ? tracer->trace(P, R, Ng, surfaceMagnitude) : GMANColor(0.0f, 0.0f, 0.0f);
}

GMANVector GMANSurfaceEnv::toWorld(GMANVector const& v) const {
  return GMANVector(v.getX() * cameraToWorld[0][0] + v.getY() * cameraToWorld[1][0] + v.getZ() * cameraToWorld[2][0],
                    v.getX() * cameraToWorld[0][1] + v.getY() * cameraToWorld[1][1] + v.getZ() * cameraToWorld[2][1],
                    v.getX() * cameraToWorld[0][2] + v.getY() * cameraToWorld[1][2] + v.getZ() * cameraToWorld[2][2]);
}

// RISpec 3.2 Sec 7.1.2's lat-long picture, inverted: "longitude equal to
// 0 degrees at the left, and 360 degrees at the right. The latitude at the
// bottom is -90 degrees and at the top is 90 degrees," with
// x=cos(lon)cos(lat), y=sin(lon)cos(lat), z=sin(lat). lat = asin(z);
// lon = atan2(y, x) wrapped into [0, 2*PI). s = lon / 2*PI, and
// t = (PI/2 - lat) / PI, since t=0 is gman::Texture's own top row and the
// top of the picture is the north pole. environment() does no space
// conversion; the shader picks the space R is given in, as RSL's does.
GMANColor GMANSurfaceEnv::environment(std::string const& name, GMANVector const& R) const {
  GMANVector r(R);
  if (r.magnitude() < RI_EPSILON) {
    return GMANColor((RtFloat)0.0, (RtFloat)0.0, (RtFloat)0.0);
  }
  r.normalize();

  RtFloat lat = (RtFloat)std::asin(r.getZ());
  RtFloat lon = (RtFloat)std::atan2(r.getY(), r.getX());
  if (lon < 0) {
    lon += (RtFloat)(2.0 * PI);
  }
  RtFloat s = lon / (RtFloat)(2.0 * PI);
  RtFloat t = ((RtFloat)(PI / 2.0) - lat) / (RtFloat)PI;
  return gman::textureCache().sample(name, s, t);
}
