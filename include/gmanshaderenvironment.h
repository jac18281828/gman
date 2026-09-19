/* SPDX-License-Identifier: LGPL-2.1-or-later */

/* This is part of GMAN, a RenderMan-compatible renderer.
 *
 * Copyright (c) 2001, 2000, 1999 John Cairns
 *
 * Author: John Cairns <john@2ad.com>
 */

/* LJL - March 2001 - Moved Environment from shaders here */

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

#include <cmath>
#include <vector>

#include "gmancolor.h"
#include "gmanlightsourcemgr.h"
#include "gmanmatrix4.h"
#include "gmannormal.h"
#include "gmanocclude.h"
#include "gmanpoint.h"
#include "gmanslapi.h"
#include "gmanvector.h"
#include "ri.h"

// cl scaled per channel by an occluder's transmission. GMANColor gains no
// operator* for this; diffuse() and specular() share this rather than
// repeating the three get/set pairs each.
inline GMANColor GMANOccludedColor(GMANColor cl, GMANColor const& transmission) {
  cl.setRed(cl.getRed() * transmission.getRed());
  cl.setGreen(cl.getGreen() * transmission.getGreen());
  cl.setBlue(cl.getBlue() * transmission.getBlue());
  return cl;
}

/*
 * The interface a surface shader is written against -- and the interface
 * a future shading-language VM would target, since a C++ shader and an
 * SL-compiled one both need the same inputs and the same builtins. Every
 * field is camera space (see AGENTS.md's "Coordinate spaces"); every
 * shadeop below forwards to the SL runtime in gmannoise.cpp/gmanslapi.cpp,
 * which existed, worked and had no caller before this phase.
 */
struct GMAN_EXPORT GMANSurfaceEnv {
  GMANColor Cs;    // surface color
  GMANColor Os;    // surface opacity
  GMANPoint P;     // surface position
  GMANVector dPdu; // derivative of surface position along u.
  GMANVector dPdv; // derivative of surface position along v.
  GMANNormal N;    // surface shading normal
  GMANNormal Ng;   // surface geometric normal

  RtFloat u; // surface parameters
  RtFloat v; // surface parameters

  RtFloat du; // change in surface parameters
  RtFloat dv; // change in surface parameters

  RtFloat s; // change in surface texture coordinates
  RtFloat t; // change in surface texture coordinates

  GMANPoint E;  // position of the eye
  GMANVector I; // incident ray direction

  RtFloat ncomps; // Number of color components
  RtFloat time;   // current shutter time
  RtFloat dtime;  // amount of time covered by this shading sample
  GMANVector dPdtime;

  GMANColor Ci; // incident ray color
  GMANColor Oi; // incident ray opacity

  // Lights active in the attribute scope this surface was declared in
  // (RiIlluminate), resolved from handle to light at shading time. Not
  // owned: these point into gmanLightSourceMgr()'s storage.
  std::vector<const GMANLight*> lights;

  // World-to-camera's inverse (GMANOptions::getCameraToWorld, set at
  // RiWorldBegin), filled by gman::shade. Identity until then, so a
  // shader run before RiWorldBegin sees the two spaces as one.
  GMANMatrix4 cameraToWorld;

  // The renderer's answer to "does light reach P?" (gmanocclude.h), null
  // by default and last so existing field offsets hold. Null means every
  // light is visible, today's behaviour: the z-buffer passes none, and a
  // shader built against an older header ignores this until rebuilt.
  // diffuse() and specular() consult it; ambient() cannot, having no
  // direction to occlude.
  gman::Occluder const* occluder = nullptr;

  // ---- noise family (gmannoise.cpp), defined in
  // gmanshaderenvironment.cpp ----
  // A single generator shared by every GMANSurfaceEnv, matching real SL's
  // noise() being a pure function of its argument, not of shader
  // instance. gmannoise.cpp's periodic() is RSL's pnoise().
  RtFloat noise(RtFloat v) const;
  RtFloat noise(RtFloat u_, RtFloat v_) const;
  RtFloat noise(const GMANPoint& p) const;
  RtFloat noise(const GMANPoint& p, RtFloat t_) const;

  RtFloat pnoise(RtFloat v, RtFloat pv) const;
  RtFloat pnoise(const GMANPoint& p, const GMANPoint& pp) const;

  RtFloat cellnoise(RtFloat v) const;
  RtFloat cellnoise(const GMANPoint& p) const;

  // ---- texture() (gmantexture.cpp), defined in
  // gmanshaderenvironment.cpp ----
  // Declared here and defined there because this header is included by
  // every translation unit that shades, and gmantexture.h's own decoder
  // must not follow it in.
  // Forwards to gman::textureCache()'s three-argument sample, which applies
  // the wrap modes RiMakeTexture recorded in the file -- clamp when the
  // file carries none.
  GMANColor texture(const std::string& name, RtFloat s, RtFloat t) const;

  // ---- environment() (gmantexture.cpp) and its world-space transform,
  // defined in gmanshaderenvironment.cpp for the same reason as texture()
  // above ----
  // RSL's vtransform("current", "world", v): a direction, so translation
  // is ignored -- cameraToWorld's upper-left 3x3 only, in AGENTS.md's
  // row-vector convention (p*M, translation in row 3).
  GMANVector toWorld(GMANVector const& v) const;

  // RISpec 3.2 Sec 15.7.2: the map's colour in world-space direction R,
  // "the length of this vector is unimportant." environment() does no
  // space conversion itself -- the shader picks the space, as RSL's does.
  GMANColor environment(std::string const& name, GMANVector const& R) const;

  // ---- gmanslapi.cpp: already free functions, forwarded here so a
  // shader reaches every builtin the same way, through env. Named
  // GMANReflect/GMANRefract/GMANFresnel/GMANFaceForward in gmanslapi.cpp;
  // gmanslapi.h previously declared unprefixed lowercase forms with no
  // definition anywhere -- fixed alongside this, see gmanslapi.h. ----
  GMANVector reflect(const GMANVector& i, const GMANVector& n) const { return GMANReflect(i, n); }
  GMANVector refract(const GMANVector& i, const GMANVector& n, RtFloat eta) const { return GMANRefract(i, n, eta); }
  RtVoid fresnel(const GMANVector& i, const GMANVector& n, RtFloat eta, RtFloat& kr, RtFloat& kt) const {
    GMANFresnel(i, n, eta, kr, kt);
  }
  GMANVector faceforward(const GMANVector& n, const GMANVector& i, const GMANVector& nr) const {
    return GMANFaceForward(n, i, nr);
  }
  RtFloat smoothstep(RtFloat min, RtFloat max, RtFloat value) const { return GMANSmoothStep(min, max, value); }

  template <class T> T spline(const std::string& basis, RtFloat value, RtInt nvals, T fvals[]) const {
    if (basis == "bezier")
      return GMANBezierSpline<T>(value, nvals, fvals);
    if (basis == "bspline")
      return GMANBsplineSpline<T>(value, nvals, fvals);
    if (basis == "hermite")
      return GMANHermiteSpline<T>(value, nvals, fvals);
    if (basis == "linear")
      return GMANLinearSpline<T>(value, nvals, fvals);
    return GMANCatmullSpline<T>(value, nvals, fvals); // "catmull-rom", default
  }

  // ---- illuminance loop, RiSL's own shape: ambient()/diffuse()/
  // specular() sum every currently-active light's contribution at P, so a
  // shader's own body stays the couple of lines matte/plastic/metal are
  // in the RISpec. Blinn-Phong for specular(), the common approximation
  // to the RISpec's own (more expensive) integral. ----
  GMANColor ambient(RtVoid) const {
    GMANColor sum;
    for (std::size_t i = 0; i < lights.size(); ++i) {
      if (lights[i]->getType() != GMAN_LIGHT_AMBIENT) {
        continue;
      }
      GMANVector l;
      GMANColor cl;
      lights[i]->sample(P, l, cl);
      sum += cl;
    }
    return sum;
  }

  GMANColor diffuse(const GMANVector& n) const {
    GMANVector nn(n);
    nn.normalize();
    GMANColor sum;
    for (std::size_t i = 0; i < lights.size(); ++i) {
      if (lights[i]->getType() == GMAN_LIGHT_AMBIENT) {
        continue;
      }
      GMANVector l;
      GMANColor cl;
      lights[i]->sample(P, l, cl);
      // A distant light's l has the length of its from-to vector, which
      // means nothing; every other light's is the distance to it, taken
      // before normalize() below discards it.
      RtFloat const distance = (lights[i]->getType() == GMAN_LIGHT_DISTANT) ? RI_INFINITY : l.magnitude();
      l.normalize();
      RtFloat nDotL = nn.dot(l);
      if (nDotL > 0.0) {
        if (occluder) {
          cl = GMANOccludedColor(cl, occluder->transmission(*lights[i], P, l, distance));
        }
        cl.scale(nDotL);
        sum += cl;
      }
    }
    return sum;
  }

  GMANColor specular(const GMANVector& n, const GMANVector& v, RtFloat roughness) const {
    GMANVector nn(n);
    nn.normalize();
    GMANVector vv(v);
    vv.normalize();
    GMANColor sum;
    for (std::size_t i = 0; i < lights.size(); ++i) {
      if (lights[i]->getType() == GMAN_LIGHT_AMBIENT) {
        continue;
      }
      GMANVector l;
      GMANColor cl;
      lights[i]->sample(P, l, cl);
      // See diffuse()'s own comment: distance before normalize() discards
      // a point or spot light's l magnitude.
      RtFloat const distance = (lights[i]->getType() == GMAN_LIGHT_DISTANT) ? RI_INFINITY : l.magnitude();
      l.normalize();
      GMANVector h(l.getX() + vv.getX(), l.getY() + vv.getY(), l.getZ() + vv.getZ());
      h.normalize();
      RtFloat nDotH = nn.dot(h);
      if (nDotH > 0.0) {
        if (occluder) {
          cl = GMANOccludedColor(cl, occluder->transmission(*lights[i], P, l, distance));
        }
        RtFloat exponent = (roughness > RI_EPSILON) ? (1.0 / roughness) : (1.0 / RI_EPSILON);
        cl.scale((RtFloat)pow(nDotH, exponent));
        sum += cl;
      }
    }
    return sum;
  }
};

struct GMANLightEnv {
  GMANPoint P;     // surface position
  GMANVector dPdu; // derivative of surface position along u.
  GMANVector dPdv; // derivative of surface position along v.
  GMANNormal N;    // surface shading normal
  GMANNormal Ng;   // surface geometric normal

  RtFloat u; // surface parameters
  RtFloat v; // surface parameters

  RtFloat du; // change in surface parameters
  RtFloat dv; // change in surface parameters

  RtFloat s; // change in surface texture coordinates
  RtFloat t; // change in surface texture coordinates

  GMANPoint Ps; // Position being illuminated
  GMANPoint E;  // position of the eye

  RtFloat ncomps; // Number of color components
  RtFloat time;   // current shutter time
  RtFloat dtime;  // amount of time covered by this shading sample

  GMANColor Cl; // Outgoing light ray color
  GMANColor Ol; // Outgoing light ray opacity
};

struct GMANVolumeEnv {
  GMANPoint P; // surface position

  GMANVector I; // incident ray direction
  GMANPoint E;  // position of the eye

  GMANColor Ci; // incident ray color
  GMANColor Oi; // incident ray opacity

  RtFloat ncomps; // Number of color components
  RtFloat time;   // current shutter time
  RtFloat dtime;  // amount of time covered by this shading sample
};

struct GMANDisplacementEnv {
  GMANPoint P;     // surface position
  GMANVector dPdu; // derivative of surface position along u.
  GMANVector dPdv; // derivative of surface position along v.
  GMANNormal N;    // surface shading normal
  GMANNormal Ng;   // surface geometric normal

  GMANPoint E; // position of the eye

  RtFloat u; // surface parameters
  RtFloat v; // surface parameters

  RtFloat du; // change in surface parameters
  RtFloat dv; // change in surface parameters

  RtFloat s; // change in surface texture coordinates
  RtFloat t; // change in surface texture coordinates

  RtFloat ncomps; // Number of color components
  RtFloat time;   // current shutter time
  RtFloat dtime;  // amount of time covered by this shading sample
  GMANVector dPdtime;
};

struct GMANImagerEnv {
  GMANPoint P; // Pixel raster position

  GMANColor Ci;  // Pixel color
  GMANColor Oi;  // Pixel opacity
  RtFloat alpha; // fractional pixel coverage

  RtFloat ncomps; // Number of color components
  RtFloat time;   // current shutter time
  RtFloat dtime;  // amount of time covered by this shading sample
};
