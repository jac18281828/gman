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

#include "gmanbitmap.h"
#include "gmancolor.h"
#include "gmanfinitecolor.h"
#include "gmangamma.h"
#include "gmanimagerpass.h"
#include "gmanimagershader.h"
#include "gmanoptions.h"
#include "gmanpoint.h"
#include "gmanshaderenvironment.h"
#include "ri.h"

namespace gman {

namespace {

GMANColor::ColorSampleType coverageOf(GMANAlpha const& alpha) {
  return (alpha.getRed() + alpha.getGreen() + alpha.getBlue()) / static_cast<GMANColor::ColorSampleType>(3.0);
}

} // namespace

void runImager(GMANBitmap& image, GMANImagerShader const& imager, GMANOptions const& options) {
  GMANOptions::ExposureStruct const& exposure = options.getExposure();
  GMANOptions::RasterInfo const raster = options.getRasterInfo();
  GMANOptions::ShutterStruct const& shutter = options.getShutter();

  for (int y = 0; y < image.getHeight(); y++) {
    for (int x = 0; x < image.getWidth(); x++) {
      GMANAlpha const stored = image.getAlpha(x, y);
      GMANColor::ColorSampleType const coverage = coverageOf(stored);

      GMANImagerEnv env{};
      env.P =
          GMANPoint(static_cast<RtFloat>(raster.rxmin + x) + 0.5f, static_cast<RtFloat>(raster.rymin + y) + 0.5f, 0.0f);
      env.Ci = gman::finiteChannels(gman::gammaCorrected(image.getPixel(x, y), exposure.gain, exposure.gamma));
      env.Oi = GMANColor(stored.getRed(), stored.getGreen(), stored.getBlue());
      env.alpha = coverage;
      env.ncomps = 3;
      env.time = shutter.min;
      env.dtime = shutter.max - shutter.min;

      GMANColor const color = imager.computeCi(env);
      imager.computeOi(env);

      image.setPixel(x, y, color);
      if (env.alpha != coverage) {
        image.setAlpha(x, y, GMANAlpha(env.alpha));
      }
    }
  }
}

} // namespace gman
