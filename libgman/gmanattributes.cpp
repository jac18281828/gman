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

#include "gmanattributes.h"

namespace {
constexpr auto NCOMPS = 3;
}

GMANAttributes::GMANAttributes()
    : areaLight(NULL), atmosphere(NULL), interior(NULL), exterior(NULL), displacement(NULL)

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

  // attempt to load default shaders
  /*
    surfaceModule = new GMANLoadableShader("defaultsurface");
    if(surfaceModule->getType() == GMANShader::SURFACE) {
      surface = surfaceModule->getSurface();
    } else {
      throw(GMANError(RIE_NOSHADER, RIE_SEVERE, "Default surface shader is not a surface shader."));
    }
  */
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

RtVoid GMANAttributes::setSurface(const std::string& name, GMANParameterList const& pl) {
  std::string objectName = "lib";
  objectName += name;
  objectName += ".so";

  // A Surface that fails to load reports RIE_NOSHADER and leaves the
  // default surface, with no module: those parameters belong to a shader
  // that never ran.
  try {
    surfaceModule = std::make_shared<GMANLoadableShader>(objectName.c_str(), pl);
    if (surfaceModule->getType() != GMANShader::SURFACE) {
      throw(GMANError(RIE_NOSHADER, RIE_SEVERE, "Specified surface shader is not a surface shader."));
    }
  } catch (GMANError const& loadError) {
    surfaceModule.reset();
    surface.reset();
    auto const message =
        "Surface \"" + name + "\" failed to load; the default surface shades instead: " + loadError.getMessage();
    GMANError fallback(RIE_NOSHADER, RIE_ERROR, message.c_str());
    GMANHandleError(fallback);
    return;
  }

  surface = std::shared_ptr<GMANSurfaceShader const>(surfaceModule, surfaceModule->getSurface());
}

RtVoid GMANAttributes::setDisplacement(const std::string& name, GMANParameterList const& pl) {
  displacementModule = std::make_shared<GMANLoadableShader>(name.c_str(), pl);
  if (displacementModule->getType() == GMANShader::DISPLACEMENT) {
    displacement = displacementModule->getDisplacement();
  } else {
    throw(GMANError(RIE_NOSHADER, RIE_SEVERE, "Specified displacement shader is not a displacement shader."));
  }
}

RtVoid GMANAttributes::setAtmosphere(const std::string& name, GMANParameterList const& pl) {
  atmosphereModule = std::make_shared<GMANLoadableShader>(name.c_str(), pl);
  if (atmosphereModule->getType() == GMANShader::VOLUME) {
    atmosphere = atmosphereModule->getVolume();
  } else {
    throw(GMANError(RIE_NOSHADER, RIE_SEVERE, "Specified atmosphere shader is not a volume shader."));
  }
}

RtVoid GMANAttributes::setInterior(const std::string& name, GMANParameterList const& pl) {
  interiorModule = std::make_shared<GMANLoadableShader>(name.c_str(), pl);
  if (interiorModule->getType() == GMANShader::VOLUME) {
    interior = interiorModule->getVolume();
  } else {
    throw(GMANError(RIE_NOSHADER, RIE_SEVERE, "Specified interior shader is not a volume shader."));
  }
}

RtVoid GMANAttributes::setExterior(const std::string& name, GMANParameterList const& pl) {
  exteriorModule = std::make_shared<GMANLoadableShader>(name.c_str(), pl);
  if (exteriorModule->getType() == GMANShader::VOLUME) {
    exterior = exteriorModule->getVolume();
  } else {
    throw(GMANError(RIE_NOSHADER, RIE_SEVERE, "Specified exterior shader is not a volume shader."));
  }
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
