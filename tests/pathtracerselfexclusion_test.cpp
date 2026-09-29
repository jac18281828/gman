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
 * A camera-facing emitting disk cannot choose itself as a next-event
 * light: without that exclusion, the disk's own light-choice weight
 * dominates every point on its own surface (it peaks exactly where an
 * emitter can least light itself), so almost every next-event draw picks
 * the disk, contributes nothing, and the rare draw that reaches the real
 * light divides by that draw's own tiny probability -- an unbiased mean
 * riding on huge, rare spikes. This file renders the fixed estimator once
 * and checks its pooled residual mean and standard error; the ratio
 * against the same measurement with the exclusion disabled is a manual,
 * uncommitted comparison.
 */

#include <cmath>
#include <cstdio>
#include <vector>

#include "check.h"
#include "gmanattributes.h"
#include "gmancolor.h"
#include "gmanframebuffer.h"
#include "gmanlightsourcemgr.h"
#include "gmanmatrix4.h"
#include "gmanoptions.h"
#include "gmanparameterlist.h"
#include "gmanpathtracerenderer.h"
#include "gmanpoint.h"
#include "gmanray.h"
#include "gmanraydisk.h"
#include "gmantransform.h"
#include "gmanvsperspective.h"
#include "maketransform.h"
#include "pathtracerscene.h"
#include "ri.h"

namespace {

// The disk's own placement and radiance: large enough relative to the
// distant light that almost every next-event draw at a point on the
// disk's own surface would, without the fix, pick the disk itself.
constexpr RtFloat kDiskRadius = 10.0f;
constexpr RtFloat kDiskZ = 5.0f;
constexpr RtFloat kDiskLe = 100.0f;

// The distant light's own radiance and direction (light travels toward
// +z, +y): its own cosine to the disk's normal (0, 0, -1) is exactly
// 0.8, a fixed angle off head-on.
constexpr RtFloat kDistantCl = 1.0f;
constexpr RtFloat kCosTheta = 0.8f;

constexpr RtFloat kReflectance = 1.0f; // white matte, Kd 1, Cs (1, 1, 1)
constexpr RtFloat kFov = 90.0f;
constexpr RtInt kRes = 9;
constexpr std::uint32_t kSamples = 1024u;

// The pooled standard error's own bound: a flat disk under one distant
// light carries no per-pixel variance once self-selection cannot happen,
// so every measured pixel agrees exactly and the true value is 0; this
// bound stays comfortably above that and comfortably under a tenth of the
// same measurement with the exclusion disabled (the primitive argument
// ignored).
constexpr double kStderrBound = 0.0001;

GMANOptions::ScreenWindowStruct squareWindow() {
  GMANOptions::ScreenWindowStruct sw;
  sw.left = -1.0f;
  sw.right = 1.0f;
  sw.bottom = -1.0f;
  sw.top = 1.0f;
  return sw;
}

// Renders the disk scene and collects each measured pixel's residual
// against the analytic expectation Le + rho * Cl * cosTheta / pi, uniform
// over the whole disk since it is flat and rigidly lit.
void renderAndCollectResiduals(std::vector<double> residuals[3], std::vector<double> expectedByChannel[3],
                               std::size_t& measuredCount, std::size_t& droppedCount) {
  GMANOptions options;
  options.setFormat(kRes, kRes, 1.0f);
  options.setPixelSamples(1.0f, 1.0f);
  options.setPixelFilter(RiBoxFilter, 1.0f, 1.0f);
  options.setPathtracerSamples((RtInt)kSamples);

  GMANMatrix4 const identity;
  gman::VSPerspective viewingSys(kRes, kRes, squareWindow(), identity, kFov, 0.5f, 50.0f);

  GMANPathtraceRenderer renderer;

  GMANMatrix4 place;
  place.trans(0.0f, 0.0f, kDiskZ);
  GMANTransform const transform = makeTransform(place);
  GMANRayDisk* disk = new GMANRayDisk(0.0f, kDiskRadius, 360.0f, GMANParameterList(), transform);

  GMANLight const areaLight(GMAN_LIGHT_AREA, GMANColor(kDiskLe, kDiskLe, kDiskLe), GMANPoint(), GMANVector());
  GMANVector const distantDirection(0.0f, 0.6f, 0.8f);
  GMANLight const distantLight(GMAN_LIGHT_DISTANT, GMANColor(kDistantCl, kDistantCl, kDistantCl), GMANPoint(),
                               distantDirection);

  gman::Appearance appearance;
  appearance.shader = loadShader("matte", matteParams(1.0f));
  appearance.Cs = GMANColor(1.0f, 1.0f, 1.0f);
  appearance.Os = GMANColor(1.0f, 1.0f, 1.0f);
  appearance.areaLight = &areaLight;
  appearance.lights = {&distantLight};
  disk->setAppearance(appearance);
  renderer.getWorldManager()->add(disk);

  GMANFrameBuffer frameBuffer(kRes, kRes, options.getBackground());
  GMANAttributes const attr;
  renderer.render(&frameBuffer, &viewingSys, options, attr);
  droppedCount = renderer.droppedPathCount();

  // A delta light's own Cl already carries the *pi that a Lambert lobe's
  // eval divides out, so the two cancel: no /pi here, matching
  // pathtracerscene.h's own analyticExpected.
  RtFloat const expected = kDiskLe + kReflectance * kDistantCl * kCosTheta;
  GMANColor const expectedColor(expected, expected, expected);

  measuredCount = 0;
  for (int py = 0; py < kRes; ++py) {
    for (int px = 0; px < kRes; ++px) {
      bool onDisk = true;
      for (int dy = 0; dy <= 1 && onDisk; ++dy) {
        for (int dx = 0; dx <= 1 && onDisk; ++dx) {
          GMANRay const corner = viewingSys.cameraRay((RtFloat)(px + dx), (RtFloat)(py + dy));
          GMANHit cornerHit;
          onDisk = disk->intersect(corner, cornerHit);
        }
      }
      if (!onDisk) {
        continue;
      }
      ++measuredCount;
      GMANColor const actual = frameBuffer.getPixel(px, py);
      for (int c = 0; c < 3; ++c) {
        residuals[c].push_back(channel(actual, c) - channel(expectedColor, c));
        expectedByChannel[c].push_back(channel(expectedColor, c));
      }
    }
  }
}

void testSelfExclusionResiduals() {
  std::vector<double> residuals[3];
  std::vector<double> expectedByChannel[3];
  std::size_t measuredCount = 0;
  std::size_t droppedCount = 0;
  renderAndCollectResiduals(residuals, expectedByChannel, measuredCount, droppedCount);

  check(measuredCount == (std::size_t)(kRes * kRes),
        "pathtracerselfexclusion: every pixel is measured, the disk fills the frame");
  check(droppedCount == 0, "pathtracerselfexclusion: no path dropped for a non-finite channel");

  checkResiduals(residuals, expectedByChannel, 1e-3, "pathtracerselfexclusion");

  for (int c = 0; c < 3; ++c) {
    GmanMeanStderr const stat = meanStderr(residuals[c]);
    std::printf("pathtracerselfexclusion channel %d: pooled stderr %.9f (bound %.6f)\n", c, stat.stderrOfMean,
                kStderrBound);
    check(stat.stderrOfMean < kStderrBound,
          "pathtracerselfexclusion: the pooled standard error stays below its own bound, set from this fix");
  }
}

} // namespace

int main() {
  testSelfExclusionResiduals();

  return checkSummary(
      "a point on an emitter's own surface never chooses that emitter, keeping next-event estimation's variance low");
}
