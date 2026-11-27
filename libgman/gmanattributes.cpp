/* SPDX-License-Identifier: LGPL-2.1-or-later */

/*----------------------------------------------------------
  Copyright (C) Lionel Joseph Lacour 2000, 2001
  2000/08/06  First release
  ----------------------------------------------------------
  RenderMan graphic state attributes.
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

#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <utility>

#include "gmanattributes.h"
#include "gmanloadableshader.h"
#include "gmanmatrix4.h"
#include "gmanparameterlist.h"
#include "gmanshader.h"
#include "gmansurfaceshader.h"
#include "gmantrimcurve.h"
#include "ri.h"

namespace {
constexpr auto NCOMPS = 3;

// Binds a resolved module and its already-checked typed accessor together:
// on success module owns the loaded shader and typed points into it; on any
// failure both reset to null, so a kind carries a matched pair or none.
template <typename Typed>
void bindLoadableShader(std::shared_ptr<GMANLoadableShader>& module, Typed*& typed, std::string const& requestName,
                        std::string const& name, GMANParameterList const& pl, GMANShader::ShaderType expected,
                        Typed* (GMANLoadableShader::*getter)(RtVoid)) {
  auto resolved = gman::resolveLoadableShader(requestName, name, pl, expected);
  if (!resolved) {
    module.reset();
    typed = nullptr;
    return;
  }
  module = std::shared_ptr<GMANLoadableShader>(std::move(resolved));
  typed = (module.get()->*getter)();
}

} // namespace

GMANAttributes::GMANAttributes()
    : atmosphere(NULL), interior(NULL), exterior(NULL), displacement(NULL)

{ // Shading attributes
  for (int i = 0; i < NCOMPS; i++) {
    color[i] = 1.0;
    opacity[i] = 1.0;
  }
  setTextureCoordinates(0, 0, 1, 0, 0, 1, 1, 1);
  shadingRate = 1;
  shadingInterpolation = RI_CONSTANT;
  matte = false;

  // Geometry attributes
  setDetailRange(0, 0, 1, 1);
  orientation = RI_OUTSIDE;
  sides = 2;
  objectFlag = false;
}

// The six *Module members are shared_ptr now; each releases its reference
// on its own, deleting the underlying GMANLoadableShader only when this is
// the last GMANAttributes holding it.
GMANAttributes::~GMANAttributes() {}

RtVoid GMANAttributes::setColor(RtColor c) {
  for (int i = 0; i < NCOMPS; i++) {
    color[i] = c[i];
  }
}
RtVoid GMANAttributes::setOpacity(RtColor o) {
  for (int i = 0; i < NCOMPS; i++) {
    opacity[i] = o[i];
  }
}
RtVoid GMANAttributes::setTextureCoordinates(RtFloat sf1, RtFloat tf1, RtFloat sf2, RtFloat tf2, RtFloat sf3,
                                             RtFloat tf3, RtFloat sf4, RtFloat tf4) {
  textureCoordinates.s1 = sf1;
  textureCoordinates.t1 = tf1;
  textureCoordinates.s2 = sf2;
  textureCoordinates.t2 = tf2;
  textureCoordinates.s3 = sf3;
  textureCoordinates.t3 = tf3;
  textureCoordinates.s4 = sf4;
  textureCoordinates.t4 = tf4;
}
// RiLightSource
// RiAreaLightSource
RtVoid GMANAttributes::setIlluminate(RtLightHandle lh, RtBoolean onoff) {
  if (onoff == RI_TRUE)
    lightList.on(lh);
  else
    lightList.off(lh);
}

RtVoid GMANAttributes::setSurface(std::string const& name, GMANParameterList const& pl) {
  auto resolved = gman::resolveLoadableShader("Surface", name, pl, GMANShader::SURFACE);
  if (!resolved) {
    surfaceModule.reset();
    surface.reset();
    return;
  }
  surfaceModule = std::shared_ptr<GMANLoadableShader>(std::move(resolved));
  surface = std::shared_ptr<GMANSurfaceShader const>(surfaceModule, surfaceModule->getSurface());
}

RtVoid GMANAttributes::setDisplacement(std::string const& name, GMANParameterList const& pl) {
  bindLoadableShader(displacementModule, displacement, "Displacement", name, pl, GMANShader::DISPLACEMENT,
                     &GMANLoadableShader::getDisplacement);
}

RtVoid GMANAttributes::setAtmosphere(std::string const& name, GMANParameterList const& pl) {
  bindLoadableShader(atmosphereModule, atmosphere, "Atmosphere", name, pl, GMANShader::VOLUME,
                     &GMANLoadableShader::getVolume);
}

RtVoid GMANAttributes::setInterior(std::string const& name, GMANParameterList const& pl) {
  bindLoadableShader(interiorModule, interior, "Interior", name, pl, GMANShader::VOLUME,
                     &GMANLoadableShader::getVolume);
}

RtVoid GMANAttributes::setExterior(std::string const& name, GMANParameterList const& pl) {
  bindLoadableShader(exteriorModule, exterior, "Exterior", name, pl, GMANShader::VOLUME,
                     &GMANLoadableShader::getVolume);
}

RtVoid GMANAttributes::setShadingRate(RtFloat sz) { shadingRate = sz; }
RtVoid GMANAttributes::setShadingInterpolation(RtToken si) { shadingInterpolation = si; }
RtVoid GMANAttributes::setMatte(RtBoolean on) {
  if (on == RI_TRUE)
    matte = true;
  else
    matte = false;
}

// ******* ******* GEOMETRY ATTRIBUTES ******* *******
RtVoid GMANAttributes::setBound(RtBound b) { bound = GMANBBox(b); }
RtVoid GMANAttributes::setDetail(RtBound d) { detail = GMANBBox(d); }
RtVoid GMANAttributes::setDetailRange(RtFloat minv, RtFloat lt, RtFloat up, RtFloat maxv) {
  detailRange.minVisible = minv;
  detailRange.lowerTransition = lt;
  detailRange.upperTransition = up;
  detailRange.maxVisible = maxv;
}
RtVoid GMANAttributes::setGeometricApproximation(RtToken ga, RtFloat v) {
  geometricApproximation.type = ga;
  geometricApproximation.value = v;
}
RtVoid GMANAttributes::setOrientation(RtToken o) { orientation = o; }
RtVoid GMANAttributes::toggleOrientation() {
  if (orientation == RI_OUTSIDE)
    orientation = RI_INSIDE;
  else if (orientation == RI_INSIDE)
    orientation = RI_OUTSIDE;
  else if (orientation == RI_LH)
    orientation = RI_RH;
  else if (orientation == RI_RH)
    orientation = RI_LH;
}
RtVoid GMANAttributes::setSides(RtInt n) { sides = n; }
RtVoid GMANAttributes::setTrimCurves(GMANTrimCurve const& tc) { trimCurves = tc; }
RtVoid GMANAttributes::setUVBasis(RtBasis uu, RtInt uustep, RtBasis vv, RtInt vvstep) {
  GMANMatrix4 uuBas(uu);

  GMANMatrix4 vvBas(vv);

  uvBasis = GMANBasis(uuBas, uustep, vvBas, vvstep);
}
RtVoid GMANAttributes::setObjectBasis(GMANBasis* b) {
  objectBasis = *b;
  objectFlag = true;
}
RtVoid GMANAttributes::clearObjectFlag() { objectFlag = false; }

namespace gman {

GMANTextureCoordinates resolveParametricCorners(GMANParameterList const& pl, GMANAttributes const& attr) {
  GMANTextureCoordinates corners = attr.getTextureCoordinates();

  std::span<RtFloat const> const st = pl.floats(RI_ST);
  if (st.size() >= 8) {
    corners.s1 = st[0];
    corners.t1 = st[1];
    corners.s2 = st[2];
    corners.t2 = st[3];
    corners.s3 = st[4];
    corners.t3 = st[5];
    corners.s4 = st[6];
    corners.t4 = st[7];
  }
  std::span<RtFloat const> const s = pl.floats(RI_S);
  if (s.size() >= 4) {
    corners.s1 = s[0];
    corners.s2 = s[1];
    corners.s3 = s[2];
    corners.s4 = s[3];
  }
  std::span<RtFloat const> const t = pl.floats(RI_T);
  if (t.size() >= 4) {
    corners.t1 = t[0];
    corners.t2 = t[1];
    corners.t3 = t[2];
    corners.t4 = t[3];
  }
  return corners;
}

} // namespace gman
