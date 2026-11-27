/* SPDX-License-Identifier: LGPL-2.1-or-later
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

/*
 * Proves the three installed libraries work together from one program that
 * links gman::gman, gman::gman_ray and gman::gman_raster. It renders a
 * lit sphere to consumer_all.tif in its working directory through the C
 * interface, calls gman::oneMinus from libgman_ray and calls
 * GMANRasterPrimitiveFactory from libgman_raster. It returns 1 when the
 * render leaves an error in RiLastError or either call answers wrongly.
 */

#include <gman/gmanattributes.h>
#include <gman/gmancolor.h>
#include <gman/gmanobject.h>
#include <gman/gmanoptions.h>
#include <gman/gmanowned.h>
#include <gman/gmanparameterlist.h>
#include <gman/gmanprimitive.h>
#include <gman/gmanrasterprimitivefactory.h>
#include <gman/gmanrayoccluder.h>
#include <gman/gmantransform.h>
#include <gman/ri.h>

namespace {

constexpr RtInt kWidth = 40;
constexpr RtInt kHeight = 30;
constexpr RtFloat kAspect = 1.0f;
constexpr RtFloat kCameraDistance = 5.0f;
constexpr RtFloat kSphereRadius = 1.0f;
constexpr RtFloat kFullSweep = 360.0f;

bool rendersWithoutError() {
  char imageName[] = "consumer_all.tif";
  RiBegin(RI_NULL);
  RiDisplay(imageName, RI_FILE, RI_RGBA, RI_NULL);
  RiFormat(kWidth, kHeight, kAspect);
  RiTranslate(0.0f, 0.0f, kCameraDistance);
  RiWorldBegin();
  RiLightSource("distantlight", RI_NULL);
  RiSphere(kSphereRadius, -kSphereRadius, kSphereRadius, kFullSweep, RI_NULL);
  RiWorldEnd();
  RiEnd();
  return RiLastError == RIE_NOERROR;
}

bool oneMinusInvertsOpacity() {
  const auto opacity = GMANColor(0.25f, 0.5f, 1.0f);
  const auto transmission = gman::oneMinus(opacity);
  return transmission.getRed() == 0.75f && transmission.getGreen() == 0.5f && transmission.getBlue() == 0.0f;
}

bool rasterManagerAnswers() {
  GMANRasterPrimitiveFactory manager;
  GMANOptions options;
  GMANAttributes attributes;
  GMANTransform transform;
  if (manager.getRSPoints(0, GMANParameterList(), options, attributes, transform) != nullptr) {
    return false;
  }
  const gman::Owned<GMANPrimitive> primitive = gman::make<GMANObject>();
  const auto object = dynamic_cast<GMANObject*>(primitive.get());
  return object != nullptr && object->getBody() == nullptr && object->getNext() == nullptr;
}

} // namespace

int main() {
  if (!rendersWithoutError() || !oneMinusInvertsOpacity() || !rasterManagerAnswers()) {
    return 1;
  }
  return 0;
}
